#include "renderer/layout/layout.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "renderer/layout/style_values.hpp"

namespace honeybee::renderer::layout {

namespace {

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
                const char* top, const char* right,
                const char* bottom, const char* left) {
    if (!style) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    auto px = [&](const char* p) {
        const auto v = style->get(p);
        return v ? parse_px(*v) : 0.0f;
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

float font_size_of(const css::ComputedStyle* style) {
    if (!style) return 16.0f;
    const auto v = style->get("font-size");
    if (!v) return 16.0f;
    const float px = parse_px(*v);
    return px > 0.0f ? px : 16.0f;
}

Color text_color_of(const css::ComputedStyle* style) {
    if (!style) return Color::black();
    const auto v = style->get("color");
    return v ? parse_color(*v) : Color::black();
}

LayoutBox layout_element(const dom::Element& el,
                         const css::ComputedStyleMap& styles,
                         const TextMeasurer& measurer,
                         float x_before_margin,
                         float y_before_margin,
                         float parent_content_width) {
    LayoutBox box;
    box.element = &el;

    const css::ComputedStyle* style = lookup(styles, el);

    fill_sides(box.margin, style,
               "margin-top", "margin-right", "margin-bottom", "margin-left");
    fill_sides(box.border, style,
               "border-top-width", "border-right-width",
               "border-bottom-width", "border-left-width");
    fill_sides(box.padding, style,
               "padding-top", "padding-right", "padding-bottom", "padding-left");

    if (style) {
        const auto bg = style->get("background-color");
        box.background = bg ? parse_color(*bg) : Color::transparent();
        const auto bc = style->get("border-color");
        box.border_color = bc ? parse_color(*bc) : Color::transparent();
    }

    // Border-box position (margin applied from parent's cursor).
    box.x = x_before_margin + box.margin[3];
    box.y = y_before_margin + box.margin[0];

    // Content area position.
    const float content_x = box.x + box.border[3] + box.padding[3];
    const float content_y = box.y + box.border[0] + box.padding[0];

    // Content width: fill parent, minus own horizontal margin+border+padding,
    // or explicit width if set.
    float content_w = parent_content_width
                    - box.margin[3] - box.border[3] - box.padding[3]
                    - box.margin[1] - box.border[1] - box.padding[1];
    if (style) {
        const auto w = style->get("width");
        if (w) {
            const float ew = parse_px(*w);
            if (ew > 0.0f) content_w = ew;
        }
    }
    if (content_w < 0.0f) content_w = 0.0f;

    // Stack children top-to-bottom.
    float cursor_y = content_y;
    for (const auto& child : el.children()) {
        if (child->type() == dom::NodeType::Element) {
            const auto& child_el = static_cast<const dom::Element&>(*child);
            if (is_non_rendered_tag(child_el.tag())) continue;
            auto child_box = std::make_unique<LayoutBox>(
                layout_element(child_el, styles, measurer, content_x, cursor_y,
                               content_w));
            cursor_y = child_box->y + child_box->height + child_box->margin[2];
            box.children.push_back(std::move(child_box));
        } else if (child->type() == dom::NodeType::Text) {
            const auto& text_node = static_cast<const dom::TextNode&>(*child);
            auto lt = std::make_unique<LayoutText>();
            lt->x = content_x;
            lt->y = cursor_y;
            lt->text = text_node.data();
            lt->font_family = font_family_of(style);
            lt->font_size = font_size_of(style);
            lt->color = text_color_of(style);
            const auto [w, h] = measurer(*lt);
            lt->width = w;
            lt->height = h;
            cursor_y = lt->y + lt->height;
            box.children.push_back(std::move(lt));
        }
    }

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

LayoutBox layout(const dom::Document& doc,
                 const css::ComputedStyleMap& styles,
                 TextMeasurer measurer,
                 float viewport_width) {
    const dom::Element* root = find_root_element(doc);
    if (!root) return LayoutBox{};

    return layout_element(*root, styles, measurer, 0.0f, 0.0f, viewport_width);
}

} // namespace honeybee::renderer::layout
