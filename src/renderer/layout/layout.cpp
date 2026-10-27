#include "renderer/layout/layout.hpp"
#include "renderer/layout/grid_layout.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "renderer/layout/style_values.hpp"
#include "renderer/layout/text_utils.hpp"

namespace honeybee::renderer::layout {

namespace {

constexpr float kDefaultFontSize = 16.0f;

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

bool is_ws_byte(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

// Recursively walk an inline subtree, emitting StyledWord tokens.
// Text nodes inherit the current style parameters. Elements with
// their own color in ComputedStyle override the color for their
// descendants. Font family and font size are inherited uniformly
// (per ADR-016, font-weight/style render is deferred).
// prev_had_space is threaded through siblings so that whitespace
// at the boundary between inline elements is preserved.
void flatten_inline_to_words(
    const dom::Node& node,
    const css::ComputedStyleMap& styles,
    const std::string& family,
    float size,
    const Color& default_color,
    bool& prev_had_space,
    std::vector<StyledWord>& out,
    const dom::Element* current_element) {

    if (node.type() == dom::NodeType::Text) {
        const auto& tn = static_cast<const dom::TextNode&>(node);
        const std::string_view data = tn.data();
        bool pending_ws = false;
        std::size_t i = 0;
        while (i < data.size()) {
            if (is_ws_byte(data[i])) {
                pending_ws = true;
                ++i;
                continue;
            }
            const std::size_t start = i;
            while (i < data.size() && !is_ws_byte(data[i])) ++i;
            StyledWord w;
            w.text = std::string(data.substr(start, i - start));
            w.font_family = family;
            w.font_size = size;
            w.color = default_color;
            w.preceded_by_space =
                !out.empty() && (prev_had_space || pending_ws);
            w.element = current_element;
            pending_ws = false;
            prev_had_space = false;
            out.push_back(std::move(w));
        }
        if (pending_ws) prev_had_space = true;
    } else if (node.type() == dom::NodeType::Element) {
        const auto& el = static_cast<const dom::Element&>(node);
        auto it = styles.find(&el);

        // ADR-026 fix: display: none check in inline flattening.
        //
        // Block-level iteration skips display: none elements
        // before they can enter inline_run (in layout_element's
        // child loop), but once inside an inline run, recursion
        // into a display: none descendant (e.g. a <style> nested
        // inside a <span>) had no such guard. Wikipedia ships 18
        // <style> tags in <body>; several live inside inline
        // elements and were being rendered as visible CSS text
        // before this fix.
        if (it != styles.end()) {
            const auto dv = it->second.get("display");
            if (dv && *dv == "none") return;
        }

        Color color = default_color;
        if (it != styles.end()) {
            const auto cv = it->second.get("color");
            if (cv) {
                const Color c = parse_color(*cv);
                if (c.a > 0.0f) color = c;
            }
        }
        for (const auto& child : el.children()) {
            flatten_inline_to_words(*child, styles, family, size, color,
                                    prev_had_space, out, &el);
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
                         float root_font_size,
                         Direction parent_dir,
                         ImageDimLookup image_dims) {
    LayoutBox box;
    box.element = &el;

    const Direction own_dir = resolve_direction(el, parent_dir);

    const css::ComputedStyle* style = lookup(styles, el);
        // ADR-034: extract background-image url(...) if present.
    // Paint fetches + decodes (PNG via Cairo, SVG via librsvg)
    // and draws at box rect. Handles data: URLs (search icon).
    if (style) {
        if (auto bg = style->get("background-image")) {
            const std::string& v = *bg;
            if (v.find("url(") != std::string::npos) {
                const auto start = v.find("url(") + 4;
                const auto end = v.find(')', start);
                if (end != std::string::npos) {
                    std::string url = v.substr(start, end - start);
                    while (!url.empty() && (url.front() == ' ' || url.front() == '\t')) url.erase(url.begin());
                    while (!url.empty() && (url.back() == ' ' || url.back() == '\t')) url.pop_back();
                    if (!url.empty() && (url.front() == '\'' || url.front() == '"')) url.erase(url.begin());
                    if (!url.empty() && (url.back() == '\'' || url.back() == '"')) url.pop_back();
                    box.background_image_url = url;
                }
            }
        }
    }

    // ADR-033: parse position. Relative/Absolute/Sticky are
    // stored but treated as Static in layout (v1 limitation).
    // Fixed is honored by layout_block_children (skip from flow
    // + viewport anchor).
    if (style) {
        if (auto p = style->get("position")) {
            if (*p == "relative") box.position = Position::Relative;
            else if (*p == "absolute") box.position = Position::Absolute;
            else if (*p == "fixed") box.position = Position::Fixed;
            else if (*p == "sticky") box.position = Position::Sticky;
        }
    }

    // ADR-032: compute paint-authoritative visibility. Layout
    // still lays out hidden elements (visibility:hidden preserves
    // their space per CSS spec); paint respects LayoutBox::visible
    // and skips the subtree.
    if (style) {
        if (auto v = style->get("visibility")) {
            if (*v == "hidden" || *v == "collapse") {
                box.visible = false;
            }
        }
        if (box.visible) {
            if (auto o = style->get("opacity")) {
                try {
                    if (std::stof(*o) <= 0.0001f) {
                        box.visible = false;
                    }
                } catch (...) {}
            }
        }
    }

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

        const std::string family = font_family_of(style);
        const Color tc = text_color_of(style);

        // Determine whether any inline element (as opposed to only
        // text nodes) is present in the run.
        bool has_inline_element = false;
        for (const auto* n : inline_run) {
            if (n->type() == dom::NodeType::Element) {
                has_inline_element = true;
                break;
            }
        }

        if (!has_inline_element) {
            // Text-only run: LayoutText path (ADR-015).
            std::string raw;
            for (const auto* n : inline_run) {
                flatten_inline_text(*n, raw);
            }
            std::string collapsed = collapse_whitespace(raw);
            inline_run.clear();
            if (collapsed.empty()) return;

            const bool rtl = (own_dir == Direction::Rtl);
            auto fragments = break_into_lines(
                collapsed, family, own_font_size, content_w, measurer);
            if (fragments.empty()) return;

            float max_line_w = 0.0f;
            float total_h = 0.0f;
            for (auto& f : fragments) {
                f.font_family = family;
                f.font_size = own_font_size;
                f.color = tc;
                f.x = rtl ? content_x + content_w - f.width : content_x;
                f.y += cursor_y;
                f.element = &el;
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
            lt->color = tc;
            lt->rtl = rtl;
            lt->lines = std::move(fragments);
            cursor_y += total_h;
            box.children.push_back(std::move(lt));
            return;
        }

        // Inline-element run: LayoutInlineContext path (ADR-016).
        std::vector<StyledWord> words;
        bool prev_had_space = false;
        for (const auto* n : inline_run) {
            flatten_inline_to_words(*n, styles, family, own_font_size,
                                    tc, prev_had_space, words, &el);
        }
        inline_run.clear();
        if (words.empty()) return;

        std::string run_text;
        for (const auto& w : words) {
            if (!run_text.empty()) run_text += ' ';
            run_text += w.text;
        }
        const bool rtl = (own_dir == Direction::Rtl);

        auto styled_lines =
            break_styled_words_into_lines(words, content_w, measurer);
        if (styled_lines.empty()) return;

        auto ctx = std::make_unique<LayoutInlineContext>();
        ctx->x = content_x;
        ctx->y = cursor_y;
        ctx->rtl = rtl;

        float max_line_w = 0.0f;
        float total_h = 0.0f;
        float line_y = cursor_y;
        for (auto& line : styled_lines) {
            const float line_start_x =
                rtl ? content_x + content_w - line.width : content_x;
            for (auto& frag : line.fragments) {
                if (rtl) {
                    frag.x = content_x + content_w - frag.x - frag.width;
                } else {
                    frag.x = line_start_x + frag.x;
                }
                frag.y = line_y;
                ctx->lines.push_back(frag);
            }
            if (line.width > max_line_w) max_line_w = line.width;
            line_y += line.height;
            total_h += line.height;
        }

        ctx->width = max_line_w;
        ctx->height = total_h;
        cursor_y += total_h;
        box.children.push_back(std::move(ctx));
    };

    // ADR-028: block/inline children layout. Reused by the grid
    // path's fallback (grid without a parsed template degrades to
    // block) and by the standard block path.
    auto layout_block_children = [&]() {
        // ADR-031: float tracking. A floated child is laid out at
        // its natural size, positioned at the left or right edge
        // of the block parent, and does NOT advance cursor_y.
        // Following block children that start within the float's
        // vertical extent have their content width narrowed to
        // avoid overlapping the float. Full per-line text-wrap is
        // deferred to ADR-032; this is a block-narrowing
        // approximation that covers the infobox case.
        float float_left_w = 0.0f;
        float float_right_w = 0.0f;
        float float_bottom = 0.0f;

        for (const auto& child : el.children()) {
            if (child->type() == dom::NodeType::Element) {
                const auto& child_el = static_cast<const dom::Element&>(*child);
                const css::ComputedStyle* child_style = lookup(styles, child_el);
                if (display_of(child_style) == Display::None) continue;

                // Read float direction
                std::string float_dir = "none";
                if (child_style) {
                    if (auto f = child_style->get("float")) {
                        float_dir = *f;
                    }
                }

                // ADR-033: position:fixed children are out of
                // flow. Layout at (0,0), then shift to viewport
                // anchor. Don't advance cursor_y (parent height
                // excludes fixed children). No float interaction
                // (CSS spec: float is ignored on positioned
                // elements).
                if (child_style) {
                    if (auto p = child_style->get("position");
                        p && *p == "fixed") {
                        flush_inline_run();
                        auto fixed_box = std::make_unique<LayoutBox>(
                            layout_element(child_el, styles, measurer,
                                           0.0f, 0.0f, content_w,
                                           own_font_size, root_font_size,
                                           own_dir, image_dims));
                        float top_off = 0.0f, left_off = 0.0f;
                        if (auto t = child_style->get("top");
                            t && *t != "auto") {
                            top_off = parse_length(*t, ctx);
                        }
                        if (auto l = child_style->get("left");
                            l && *l != "auto") {
                            left_off = parse_length(*l, ctx);
                        }
                        if (left_off != 0.0f) {
                            shift_subtree_x(*fixed_box, left_off);
                        }
                        if (top_off != 0.0f) {
                            shift_subtree_y(*fixed_box, top_off);
                        }
                        box.children.push_back(std::move(fixed_box));
                        continue;
                    }
                }

                // Float: right|left. Layout at edge, don't advance
                // cursor_y, track vertical extent for follow-ups.
                if (float_dir == "left" || float_dir == "right") {
                    flush_inline_run();
                    // ADR-036: clear on a float forces it below prior
                    // same-direction (or both-direction) floats.
                    std::string clear_dir = "none";
                    if (child_style) {
                        if (auto c = child_style->get("clear")) {
                            clear_dir = *c;
                        }
                    }
                    const bool must_jump =
                        (clear_dir == "both") ||
                        (clear_dir == "right" && float_dir == "right") ||
                        (clear_dir == "left"  && float_dir == "left");
                    if (must_jump && float_bottom > cursor_y) {
                        cursor_y = float_bottom;
                    }
                    auto probe = std::make_unique<LayoutBox>(
                        layout_element(child_el, styles, measurer,
                                       content_x, cursor_y, content_w,
                                       own_font_size, root_font_size,
                                       own_dir, image_dims));
                    if (float_dir == "right") {
                        // Shift right edge to content right edge.
                        const float target_right =
                            content_x + content_w - probe->margin[1];
                        const float current_right =
                            probe->x + probe->width;
                        shift_subtree_x(*probe, target_right - current_right);
                        if (probe->width > float_right_w) {
                            float_right_w = probe->width;
                        }
                    } else {
                        // Float left: natural position already at
                        // content_x + margin[3]; no shift needed.
                        if (probe->width > float_left_w) {
                            float_left_w = probe->width;
                        }
                    }
                    const float fbox_bottom =
                        probe->y + probe->height + probe->margin[2];
                    if (fbox_bottom > float_bottom) {
                        float_bottom = fbox_bottom;
                    }
                    box.children.push_back(std::move(probe));
                    continue;
                }

                if (display_of(child_style) == Display::Inline) {
                    inline_run.push_back(child.get());
                    continue;
                }
                flush_inline_run();

                // ADR-036: clear on a non-float block forces it
                // below any active float of the same (or both)
                // direction.
                {
                    std::string clear_dir = "none";
                    if (child_style) {
                        if (auto c = child_style->get("clear")) {
                            clear_dir = *c;
                        }
                    }
                    const bool clear_right =
                        (clear_dir == "both" || clear_dir == "right");
                    const bool clear_left =
                        (clear_dir == "both" || clear_dir == "left");
                    if ((clear_right && float_right_w > 0.0f) ||
                        (clear_left  && float_left_w  > 0.0f)) {
                        if (float_bottom > cursor_y) {
                            cursor_y = float_bottom;
                        }
                    }
                }

                // ADR-031: narrow block content if its actual
                // starting y (cursor_y + margin_top) falls within
                // the float's vertical zone. Reading margin-top
                // explicitly is needed because layout_element
                // applies margins internally -- checking cursor_y
                // alone misjudges blocks that push themselves
                // below the float via margin-top.
                // Approximate: whole block is narrowed uniformly
                // (per-line wrap is ADR-032).
                float child_margin_top = 0.0f;
                if (child_style) {
                    if (auto m = child_style->get("margin-top")) {
                        child_margin_top = parse_length(*m, ctx);
                    }
                }
                const float start_y = cursor_y + child_margin_top;
                float narrow_x = content_x;
                float narrow_w = content_w;
                if (start_y < float_bottom) {
                    narrow_x = content_x + float_left_w;
                    narrow_w = content_w - float_left_w - float_right_w;
                    if (narrow_w < 0.0f) narrow_w = 0.0f;
                }

                auto child_box = std::make_unique<LayoutBox>(
                    layout_element(child_el, styles, measurer, narrow_x, cursor_y,
                                   narrow_w, own_font_size, root_font_size,
                                   own_dir, image_dims));
                cursor_y = child_box->y + child_box->height + child_box->margin[2];
                box.children.push_back(std::move(child_box));
            } else if (child->type() == dom::NodeType::Text) {
                inline_run.push_back(child.get());
            }
        }
        flush_inline_run();
    };

    // ADR-029: flex layout dispatch. Single-line flex: row | column,
    // justify-content, align-items (cross-axis row only), flex-grow /
    // shrink / basis, gap. Multi-line flex (wrap) deferred per ADR-029.
    bool flex_handled = false;
    if (style &&
        (display_of(style) == Display::Flex ||
         display_of(style) == Display::InlineFlex)) {
        auto get_prop = [&](const std::string& key,
                            std::string_view fallback) -> std::string {
            const auto v = style->get(key);
            return v ? std::string(*v) : std::string(fallback);
        };
        const std::string direction = get_prop("flex-direction", "row");
        const std::string justify = get_prop("justify-content", "flex-start");
        const std::string align = get_prop("align-items", "stretch");
        const bool is_row =
            (direction != "column" && direction != "column-reverse");

        // Gap: column-gap for row, row-gap for column; fall back to gap.
        auto parse_gap_longhand = [&](const char* longhand) -> float {
            if (const auto v = style->get(longhand)) {
                if (*v != "normal" && *v != "auto") {
                    return parse_length(*v, ctx);
                }
            }
            return -1.0f;
        };
        float gap = parse_gap_longhand(is_row ? "column-gap" : "row-gap");
        if (gap < 0.0f) {
            if (const auto v = style->get("gap")) {
                if (*v != "normal" && *v != "auto") {
                    std::string_view gs = *v;
                    const auto sp = gs.find(' ');
                    if (sp != std::string_view::npos) gs = gs.substr(0, sp);
                    gap = parse_length(gs, ctx);
                }
            }
            if (gap < 0.0f) gap = 0.0f;
        }

        struct FC {
            const dom::Element* el;
            float basis;       // border-box main-axis size
            float final_main;
            float grow;
            float shrink;
            std::unique_ptr<LayoutBox> final_box;
        };
        std::vector<FC> fcs;

        for (const auto& child : el.children()) {
            if (child->type() != dom::NodeType::Element) continue;
            const auto& child_el =
                static_cast<const dom::Element&>(*child);
            const css::ComputedStyle* cs = lookup(styles, child_el);
            if (display_of(cs) == Display::None) continue;

            float grow = 0.0f, shrink = 1.0f;
            std::string basis_str = "auto";
            if (cs) {
                if (const auto g = cs->get("flex-grow")) {
                    try { grow = std::stof(std::string(*g)); } catch (...) {}
                }
                if (const auto s = cs->get("flex-shrink")) {
                    try { shrink = std::stof(std::string(*s)); } catch (...) {}
                }
                if (const auto b = cs->get("flex-basis")) {
                    basis_str = *b;
                }
            }

            // First pass: natural size for basis resolution.
            LayoutBox natural = layout_element(
                child_el, styles, measurer, 0.0f, 0.0f, content_w,
                own_font_size, root_font_size, own_dir, image_dims);
            const float natural_main =
                is_row ? natural.width : natural.height;

            float basis;
            if (basis_str != "auto" && basis_str != "content") {
                // ADR-029: honor explicit basis (including 0)
                // per CSS flex shorthand semantics.
                basis = parse_length(basis_str, ctx);
            } else {
                basis = natural_main;
            }

            FC fc;
            fc.el = &child_el;
            fc.basis = basis;
            fc.final_main = basis;
            fc.grow = grow;
            fc.shrink = shrink;
            fcs.push_back(std::move(fc));
        }

        if (!fcs.empty()) {
            // Sum basis + gaps.
            float total_basis = gap * static_cast<float>(fcs.size() - 1);
            for (const auto& fc : fcs) total_basis += fc.basis;

            // Grow / shrink (row only; column's container main is
            // intrinsic so no distribution applies).
            if (is_row) {
                const float free = content_w - total_basis;
                if (free < 0.0f) {
                    float total_shrink = 0.0f;
                    for (const auto& fc : fcs) {
                        total_shrink += fc.shrink * fc.basis;
                    }
                    if (total_shrink > 0.0f) {
                        for (auto& fc : fcs) {
                            const float amt =
                                (fc.shrink * fc.basis / total_shrink) *
                                (-free);
                            fc.final_main =
                                std::max(0.0f, fc.basis - amt);
                        }
                    }
                } else if (free > 0.0f) {
                    float total_grow = 0.0f;
                    for (const auto& fc : fcs) total_grow += fc.grow;
                    if (total_grow > 0.0f) {
                        for (auto& fc : fcs) {
                            fc.final_main =
                                fc.basis +
                                (fc.grow / total_grow) * free;
                        }
                    }
                }
            }

            // Remaining after resolution, for justify positioning.
            float used_main = gap * static_cast<float>(fcs.size() - 1);
            for (const auto& fc : fcs) used_main += fc.final_main;
            const float remaining = is_row
                ? std::max(0.0f, content_w - used_main) : 0.0f;

            // Main-axis start + effective gap per justify-content.
            float main_cursor = is_row ? content_x : content_y;
            float effective_gap = gap;
            if (remaining > 0.0f) {
                if (justify == "flex-end" || justify == "end") {
                    main_cursor += remaining;
                } else if (justify == "center") {
                    main_cursor += remaining / 2.0f;
                } else if (justify == "space-between" &&
                           fcs.size() > 1) {
                    effective_gap += remaining /
                        static_cast<float>(fcs.size() - 1);
                } else if (justify == "space-around") {
                    const float around =
                        remaining / static_cast<float>(fcs.size());
                    main_cursor += around / 2.0f;
                    effective_gap += around;
                }
            }

            // Second pass: final layout per child.
            float container_cross = 0.0f;
            for (auto& fc : fcs) {
                const float cw = is_row ? fc.final_main : content_w;
                const float x = is_row ? main_cursor : content_x;
                const float y = is_row ? 0.0f : main_cursor;

                fc.final_box = std::make_unique<LayoutBox>(
                    layout_element(*fc.el, styles, measurer, x, y, cw,
                                   own_font_size, root_font_size,
                                   own_dir, image_dims));
                const float cross_size =
                    is_row ? fc.final_box->height : fc.final_box->width;
                if (cross_size > container_cross) {
                    container_cross = cross_size;
                }
                main_cursor += fc.final_main + effective_gap;
            }

            // Cross-axis alignment + append (row only; column's
            // align-items deferred per ADR-029).
            if (is_row) {
                for (auto& fc : fcs) {
                    float cross_off = 0.0f;
                    if (align == "flex-end") {
                        cross_off = container_cross -
                                    fc.final_box->height;
                    } else if (align == "center") {
                        cross_off = (container_cross -
                                     fc.final_box->height) / 2.0f;
                    }
                    // stretch: no-op (natural height, top-aligned) —
                    // documented v1 limitation.
                    shift_subtree_y(*fc.final_box, content_y + cross_off);
                    box.children.push_back(std::move(fc.final_box));
                }
                cursor_y = content_y + container_cross;
            } else {
                for (auto& fc : fcs) {
                    box.children.push_back(std::move(fc.final_box));
                }
                cursor_y = content_y + used_main;
            }
        }

        flex_handled = true;
    }

    // ADR-028: grid layout dispatch. If this element's display is
    // grid AND its ComputedStyle declares a parseable template,
    // dispatch to grid children layout. Otherwise (grid without
    // template, or any non-grid display) fall through to block.
    bool grid_handled = false;
    if (!flex_handled && display_of(style) == Display::Grid && style) {
        auto tmpl = parse_grid_template(*style, ctx);
        if (tmpl && !tmpl->columns.empty()) {
            // Parse column-gap (fallback 0).
            float col_gap = 0.0f;
            if (const auto g = style->get("column-gap");
                g && !is_auto(*g)) {
                col_gap = parse_length(*g, ctx);
            }

            const auto col_widths = compute_column_widths(
                tmpl->columns, content_w, col_gap);

            struct PlacedChild {
                std::unique_ptr<LayoutBox> box;
                int row = 0;
                int col = 0;
                int row_span = 1;
                int col_span = 1;
            };
            std::vector<PlacedChild> placed;

            // Layout each direct child at y=0 within its cell's
            // horizontal bounds. Y position comes after row heights
            // are computed.
            for (const auto& child : el.children()) {
                if (child->type() != dom::NodeType::Element) continue;
                const auto& child_el =
                    static_cast<const dom::Element&>(*child);
                const css::ComputedStyle* child_style =
                    lookup(styles, child_el);
                if (display_of(child_style) == Display::None) continue;

                if (!child_style) continue;
                const auto ga = child_style->get("grid-area");
                if (!ga) continue;  // v1: children without grid-area skipped
                auto area_it = tmpl->areas.find(*ga);
                if (area_it == tmpl->areas.end()) continue;

                const int row = area_it->second.row;
                const int col = area_it->second.col;
                const int row_span = area_it->second.row_span;
                const int col_span = area_it->second.col_span;

                // Cell X position = content_x + sum of widths before
                // + gaps before.
                float cell_x = content_x;
                for (int c = 0;
                     c < col && c < static_cast<int>(col_widths.size());
                     ++c) {
                    cell_x += col_widths[c] + col_gap;
                }
                float cell_w = 0.0f;
                for (int c = col;
                     c < col + col_span &&
                     c < static_cast<int>(col_widths.size());
                     ++c) {
                    cell_w += col_widths[c];
                }
                if (col_span > 1) cell_w += col_gap * (col_span - 1);

                auto child_box = std::make_unique<LayoutBox>(
                    layout_element(child_el, styles, measurer, cell_x, 0.0f,
                                   cell_w, own_font_size, root_font_size,
                                   own_dir, image_dims));
                PlacedChild pc;
                pc.box = std::move(child_box);
                pc.row = row;
                pc.col = col;
                pc.row_span = row_span;
                pc.col_span = col_span;
                placed.push_back(std::move(pc));
            }

            // Compute row heights: max natural content height across
            // children placed at each row. For Fr and Auto rows,
            // natural content height is the flow-layout resolution
            // (ADR-028 v1).
            int num_rows = static_cast<int>(tmpl->rows.size());
            if (num_rows < 1) num_rows = 1;
            for (const auto& pc : placed) {
                if (pc.row + pc.row_span > num_rows) {
                    num_rows = pc.row + pc.row_span;
                }
            }
            std::vector<float> row_heights(num_rows, 0.0f);
            for (const auto& pc : placed) {
                if (pc.row < num_rows &&
                    pc.box->height > row_heights[pc.row]) {
                    row_heights[pc.row] = pc.box->height;
                }
            }

            // Row Y positions: cumulative from content_y, no row gap
            // by default (Wikipedia uses column-gap only).
            std::vector<float> row_y(num_rows, content_y);
            for (int r = 1; r < num_rows; ++r) {
                row_y[r] = row_y[r-1] + row_heights[r-1];
            }

            // Shift each child to its row's Y position and append.
            for (auto& pc : placed) {
                if (pc.row < num_rows) {
                    shift_subtree_y(*pc.box, row_y[pc.row]);
                }
                box.children.push_back(std::move(pc.box));
            }

            // Update cursor_y for the final box.height computation.
            float total_h = 0.0f;
            for (float h : row_heights) total_h += h;
            cursor_y = content_y + total_h;

            grid_handled = true;
        }
    }

    if (!grid_handled && !flex_handled) {
        layout_block_children();
    }

    // ADR-035: reserve layout space for <img> from intrinsic
    // dimensions. Explicit CSS width (or height) wins; the other
    // axis is aspect-derived. Neither set: use intrinsic (CSS
    // spec fallback). Callback null/invalid: existing (h=0).
    if (image_dims && el.tag() == "img") {
        const std::string src_attr = el.get_attribute("src");
        if (!src_attr.empty()) {
            const ImageDim d = image_dims(src_attr);
            if (d.valid && d.width > 0.0f && d.height > 0.0f) {
                float want_w = 0.0f;
                float want_h = 0.0f;
                if (style) {
                    if (auto w = style->get("width");
                        w && !w->empty() && *w != "auto") {
                        const float p = parse_length(*w, ctx);
                        if (p > 0.0f) want_w = p;
                    }
                    if (auto h = style->get("height");
                        h && !h->empty() && *h != "auto") {
                        const float p = parse_length(*h, ctx);
                        if (p > 0.0f) want_h = p;
                    }
                }
                if (want_w > 0.0f && want_h > 0.0f) {
                    // both explicit
                } else if (want_w > 0.0f) {
                    want_h = want_w * (d.height / d.width);
                } else if (want_h > 0.0f) {
                    want_w = want_h * (d.width / d.height);
                } else {
                    want_w = d.width;
                    want_h = d.height;
                }
                if (want_w > content_w && content_w > 0.0f) {
                    want_h = want_h * (content_w / want_w);
                    want_w = content_w;
                }
                box.width = want_w
                          + box.padding[3] + box.padding[1]
                          + box.border[3] + box.border[1];
                box.height = want_h
                           + box.padding[0] + box.padding[2]
                           + box.border[0] + box.border[2];
                return box;
            }
        }
    }

    // ADR-039: box model completion.

    // (1) min-width / max-width clamps. Applied after the width
    // resolution above. auto/none are no-ops. vw/vh in
    // max-width skipped -- ADR-037 defers those units; a
    // max-width that resolves to 0 due to vw would collapse
    // the element (worse than current no-clamp behavior).
    if (style) {
        if (auto mw = style->get("min-width")) {
            if (!is_auto(*mw) && *mw != "none") {
                const float min_w = parse_length(*mw, ctx);
                if (min_w > content_w) content_w = min_w;
            }
        }
        if (auto xw = style->get("max-width")) {
            if (!is_auto(*xw) && *xw != "none" &&
                xw->find("vw") == std::string::npos &&
                xw->find("vh") == std::string::npos) {
                const float max_w = parse_length(*xw, ctx);
                if (max_w > 0.0f && max_w < content_w) content_w = max_w;
            }
        }
    }

    // (2) explicit height from ComputedStyle. Applies to all
    // non-early-return boxes (regular elements + img with failed
    // decode; img with valid dims returns early from the img
    // block above). This is ADR-037's deferred height gap --
    // search icon w=20 h=0 becomes w=20 h=20.
    //
    // v1 limitation: percentage heights skipped. LengthContext
    // has only parent_content_width, no parent_content_height.
    // `height: 100%` on <html>/<body> would resolve to viewport
    // WIDTH (1774px) which is catastrophic. Deferred until
    // parent_content_height threading (ADR-040).
    if (style) {
        if (auto h = style->get("height");
            h && !is_auto(*h) && h->find('%') == std::string::npos) {
            const float eh = parse_length(*h, ctx);
            if (eh > 0.0f) cursor_y = content_y + eh;
        }
    }

    // (3) min-height / max-height clamps. Same vw/vh skip rule.
    if (style) {
        if (auto mh = style->get("min-height")) {
            if (!is_auto(*mh) && *mh != "none" &&
                mh->find('%') == std::string::npos) {
                const float min_h = parse_length(*mh, ctx);
                if (min_h > (cursor_y - content_y)) {
                    cursor_y = content_y + min_h;
                }
            }
        }
        if (auto xh = style->get("max-height")) {
            if (!is_auto(*xh) && *xh != "none" &&
                xh->find("vh") == std::string::npos &&
                xh->find("vw") == std::string::npos &&
                xh->find('%') == std::string::npos) {
                const float max_h = parse_length(*xh, ctx);
                if (max_h > 0.0f && max_h < (cursor_y - content_y)) {
                    cursor_y = content_y + max_h;
                }
            }
        }
    }

    // (4) overflow: hidden -> paint clips children to content
    // bounds. "hidden" and "hidden auto" both set the flag
    // (v1 treats two-value form as hidden on both axes).
    if (style) {
        if (auto ov = style->get("overflow")) {
            if (ov->find("hidden") != std::string::npos) {
                box.clip_children = true;
            }
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

Display display_of(const css::ComputedStyle* style) {
    if (!style) return Display::Block;
    const auto v = style->get("display");
    if (!v) return Display::Block;
    if (*v == "inline") return Display::Inline;
    if (*v == "none") return Display::None;
    if (*v == "grid") return Display::Grid;       // ADR-028
    if (*v == "flex") return Display::Flex;       // ADR-029
    if (*v == "inline-flex") return Display::InlineFlex;  // ADR-029
    return Display::Block;
}

Direction resolve_direction(const dom::Element& el, Direction parent_dir) {
    const auto dir_attr = el.get_attribute("dir");
    if (dir_attr == "ltr") return Direction::Ltr;
    if (dir_attr == "rtl") return Direction::Rtl;
    if (dir_attr == "auto") {
        std::string flat;
        flatten_inline_text(el, flat);
        return detect_rtl(flat) ? Direction::Rtl : Direction::Ltr;
    }
    return parent_dir;
}

LayoutBox layout(const dom::Document& doc,
                 const css::ComputedStyleMap& styles,
                 TextMeasurer measurer,
                 float viewport_width,
                 ImageDimLookup image_dims) {
    const dom::Element* root = find_root_element(doc);
    if (!root) return LayoutBox{};

    const css::ComputedStyle* root_style = lookup(styles, *root);
    const float root_fs =
        font_size_of(root_style, kDefaultFontSize, kDefaultFontSize);

    return layout_element(*root, styles, measurer, 0.0f, 0.0f, viewport_width,
                          kDefaultFontSize, root_fs, Direction::Ltr, image_dims);
}


namespace {

const dom::Element* hit_test_node(const LayoutNode& node,
                                  float x, float y) {
    // Point must be inside this node's half-open box.
    if (x < node.x || x >= node.x + node.width ||
        y < node.y || y >= node.y + node.height) {
        return nullptr;
    }
    switch (node.kind) {
        case LayoutKind::Box: {
            const auto& box = static_cast<const LayoutBox&>(node);
            for (const auto& child : box.children) {
                if (const auto* hit = hit_test_node(*child, x, y)) {
                    return hit;
                }
            }
            return box.element;
        }
        case LayoutKind::Inline: {
            const auto& inl = static_cast<const LayoutInline&>(node);
            for (const auto& child : inl.children) {
                if (const auto* hit = hit_test_node(*child, x, y)) {
                    return hit;
                }
            }
            return inl.element;
        }
        case LayoutKind::InlineContext: {
            const auto& ctx =
                static_cast<const LayoutInlineContext&>(node);
            for (const auto& child : ctx.children) {
                if (const auto* hit = hit_test_node(*child, x, y)) {
                    return hit;
                }
            }
            // ADR-018 Commit 4b: flattened inline text lives as
            // LineFragments with per-fragment element identity.
            // Return the element of the fragment containing the
            // point; nullptr (falls through to containing Box)
            // if the point is in the inline run's whitespace.
            for (const auto& frag : ctx.lines) {
                if (x >= frag.x && x < frag.x + frag.width &&
                    y >= frag.y && y < frag.y + frag.height) {
                    return frag.element;
                }
            }
            return nullptr;
        }
        case LayoutKind::Text:
            // LayoutText has no element; containing LayoutInline
            // or LayoutBox provides it.
            return nullptr;
    }
    return nullptr;
}

} // namespace

const dom::Element* hit_test(const LayoutNode& root,
                             float x, float y) {
    return hit_test_node(root, x, y);
}
} // namespace honeybee::renderer::layout
