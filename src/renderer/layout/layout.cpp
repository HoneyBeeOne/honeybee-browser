#include "renderer/layout/layout.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "renderer/layout/style_values.hpp"
#include "renderer/layout/text_utils.hpp"

namespace honeybee::renderer::layout {

namespace {

constexpr float kDefaultFontSize = 16.0f;

bool is_non_rendered_tag(const std::string& tag) {
    return tag == "head" || tag == "style" || tag == "script"
        || tag == "title" || tag == "meta" || tag == "link"
        || tag == "base";
}

const css::ComputedStyle* lookup(const css::ComputedStyleMap& styles,
                                 const dom::Element& el) {
    auto it = styles.find(&el);
    return it == styles.end() ? nullptr : &it->second;
}

void fill_sides(float out[4], const css::ComputedStyle* style,
                const LengthContext& ctx,
                const char* top, const char* right,
                const char* bottom, const char* left) {
    if (!style) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    auto px = [&](const char* p) {
        const auto v = style->get(p);
        return v ? parse_length(*v, ctx) : 0.0f;
    };
    out[0] = px(top);
    out[1] = px(right);
    out[2] = px(bottom);
    out[3] = px(left);
}

std::string font_family_of(const css::ComputedStyle* style) {
    if (!style) return {};
    return style->get("font-family").value_or("");
}

float font_size_of(const css::ComputedStyle* style,
                   float parent_font_size, float root_font_size) {
    if (!style) return parent_font_size;
    const auto v = style->get("font-size");
    if (!v) return parent_font_size;
    LengthContext ctx;
    ctx.font_size = parent_font_size;
    ctx.parent_font_size = parent_font_size;
    ctx.root_font_size = root_font_size;
    ctx.on_font_size_property = true;
    const float px = parse_length(*v, ctx);
    return px > 0.0f ? px : parent_font_size;
}

Color text_color_of(const css::ComputedStyle* style) {
    if (!style) return Color::black();
    const auto v = style->get("color");
    return v ? parse_color(*v) : Color::black();
}

void flatten_inline_text(const dom::Node& node, std::string& out) {
    if (node.type() == dom::NodeType::Text) {
        const auto& tn = static_cast<const dom::TextNode&>(node);
        out += tn.data();
    } else if (node.type() == dom::NodeType::Element) {
        for (const auto& child : node.children()) {
            flatten_inline_text(*child, out);
        }
    }
}

LayoutBox layout_element(const dom::Element& el,
                         const css::ComputedStyleMap& styles,
                         const TextMeasurer& measurer,
                         float x_before_margin,
                         float y_before_margin,
                         float parent_content_width,
                         float parent_font_size,
                         float root_font_size) {
    LayoutBox box;
    box.element = &el;

    const css::ComputedStyle* style = lookup(styles, el);

    const float own_font_size =
        font_size_of(style, parent_font_size, root_font_size);

    LengthContext ctx;
    ctx.font_size = own_font_size;
    ctx.parent_font_size = parent_font_size;
    ctx.root_font_size = root_font_size;
    ctx.parent_content_width = parent_content_width;
    ctx.on_font_size_property = false;

    fill_sides(box.margin, style, ctx,
               "margin-top", "margin-right", "margin-bottom", "margin-left");
    fill_sides(box.border, style, ctx,
               "border-top-width", "border-right-width",
               "border-bottom-width", "border-left-width");
    fill_sides(box.padding, style, ctx,
               "padding-top", "padding-right", "padding-bottom", "padding-left");

    if (style) {
        const auto bg = style->get("background-color");
        box.background = bg ? parse_color(*bg) : Color::transparent();
        const auto bc = style->get("border-color");
        box.border_color = bc ? parse_color(*bc) : Color::transparent();
    }

    box.x = x_before_margin + box.margin[3];
    box.y = y_before_margin + box.margin[0];

    float content_x = box.x + box.border[3] + box.padding[3];
    float content_y = box.y + box.border[0] + box.padding[0];

    float content_w = parent_content_width
                    - box.margin[3] - box.border[3] - box.padding[3]
                    - box.margin[1] - box.border[1] - box.padding[1];
    if (style) {
        const auto w = style->get("width");
        if (w && !is_auto(*w)) {
            const float ew = parse_length(*w, ctx);
            if (ew > 0.0f) content_w = ew;
        }
    }
    if (content_w < 0.0f) content_w = 0.0f;

    // ADR-013: auto horizontal margins center the box.
    if (style) {
        const auto ml_s = style->get("margin-left");
        const auto mr_s = style->get("margin-right");
        const bool ml_auto = ml_s && is_auto(*ml_s);
        const bool mr_auto = mr_s && is_auto(*mr_s);
        if (ml_auto || mr_auto) {
            const float available = parent_content_width
                                  - content_w
                                  - box.padding[3] - box.padding[1]
                                  - box.border[3]  - box.border[1];
            const float each = available > 0.0f
                             ? available / (ml_auto && mr_auto ? 2.0f : 1.0f)
                             : 0.0f;
            if (ml_auto) box.margin[3] = each;
            if (mr_auto) box.margin[1] = each;
            // Recompute position from the final margins.
            box.x = x_before_margin + box.margin[3];
            box.y = y_before_margin + box.margin[0];
            content_x = box.x + box.border[3] + box.padding[3];
            content_y = box.y + box.border[0] + box.padding[0];
        }
    }

    float cursor_y = content_y;

    // ADR-016 Block B: inline runs flow together as one LayoutText.
    // We collect consecutive inline children (text nodes and
    // display:inline elements) into inline_run, then flatten them
    // into a single text when a block child interrupts or when the
    // loop ends. Per-fragment style (Block C) will need
    // LayoutInlineContext instead; for uniform style, flattening to
    // one LayoutText produces the same visual output.
    std::vector<const dom::Node*> inline_run;

    auto flush_inline_run = [&]() {
        if (inline_run.empty()) return;

        std::string raw;
        for (const auto* n : inline_run) {
            flatten_inline_text(*n, raw);
        }
        std::string collapsed = collapse_whitespace(raw);
        inline_run.clear();
        if (collapsed.empty()) return;

        const std::string family = font_family_of(style);
        const bool rtl = detect_rtl(collapsed);
        auto fragments = break_into_lines(
            collapsed, family, own_font_size, content_w, measurer);
        if (fragments.empty()) return;

        float max_line_w = 0.0f;
        float total_h = 0.0f;
        for (auto& f : fragments) {
            f.font_family = family;
            f.font_size = own_font_size;
            f.color = text_color_of(style);
            f.x = rtl ? content_x + content_w - f.width : content_x;
            f.y += cursor_y;
            if (f.width > max_line_w) max_line_w = f.width;
            total_h += f.height;
        }

        auto lt = std::make_unique<LayoutText>();
        lt->x = content_x;
        lt->y = cursor_y;
        lt->width = max_line_w;
        lt->height = total_h;
        lt->text = std::move(collapsed);
        lt->font_family = family;
        lt->font_size = own_font_size;
        lt->color = text_color_of(style);
        lt->rtl = rtl;
        lt->lines = std::move(fragments);
        cursor_y += total_h;
        box.children.push_back(std::move(lt));
    };

    for (const auto& child : el.children()) {
        if (child->type() == dom::NodeType::Element) {
            const auto& child_el = static_cast<const dom::Element&>(*child);
            if (is_non_rendered_tag(child_el.tag())) continue;

            const css::ComputedStyle* child_style = lookup(styles, child_el);
            if (display_of(child_style) == Display::Inline) {
                inline_run.push_back(child.get());
                continue;
            }
            flush_inline_run();
            auto child_box = std::make_unique<LayoutBox>(
                layout_element(child_el, styles, measurer, content_x, cursor_y,
                               content_w, own_font_size, root_font_size));
            cursor_y = child_box->y + child_box->height + child_box->margin[2];
            box.children.push_back(std::move(child_box));
        } else if (child->type() == dom::NodeType::Text) {
            inline_run.push_back(child.get());
        }
    }
    flush_inline_run();

    box.width = content_w
              + box.padding[3] + box.padding[1]
              + box.border[3] + box.border[1];
    box.height = (cursor_y - content_y)
               + box.padding[0] + box.padding[2]
               + box.border[0] + box.border[2];

    return box;
}

const dom::Element* find_root_element(const dom::Document& doc) {
    for (const auto& child : doc.children()) {
        if (child->type() == dom::NodeType::Element) {
            return static_cast<const dom::Element*>(child.get());
        }
    }
    return nullptr;
}

} // namespace

Display display_of(const css::ComputedStyle* style) {
    if (!style) return Display::Block;
    const auto v = style->get("display");
    if (!v) return Display::Block;
    if (*v == "inline") return Display::Inline;
    if (*v == "none") return Display::None;
    return Display::Block;
}

LayoutBox layout(const dom::Document& doc,
                 const css::ComputedStyleMap& styles,
                 TextMeasurer measurer,
                 float viewport_width) {
    const dom::Element* root = find_root_element(doc);
    if (!root) return LayoutBox{};

    const css::ComputedStyle* root_style = lookup(styles, *root);
    const float root_fs =
        font_size_of(root_style, kDefaultFontSize, kDefaultFontSize);

    return layout_element(*root, styles, measurer, 0.0f, 0.0f, viewport_width,
                          kDefaultFontSize, root_fs);
}

} // namespace honeybee::renderer::layout
