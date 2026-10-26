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
                         Direction parent_dir) {
    LayoutBox box;
    box.element = &el;

    const Direction own_dir = resolve_direction(el, parent_dir);

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
        for (const auto& child : el.children()) {
            if (child->type() == dom::NodeType::Element) {
                const auto& child_el = static_cast<const dom::Element&>(*child);
                const css::ComputedStyle* child_style = lookup(styles, child_el);
                if (display_of(child_style) == Display::None) continue;
                if (display_of(child_style) == Display::Inline) {
                    inline_run.push_back(child.get());
                    continue;
                }
                flush_inline_run();
                auto child_box = std::make_unique<LayoutBox>(
                    layout_element(child_el, styles, measurer, content_x, cursor_y,
                                   content_w, own_font_size, root_font_size,
                                   own_dir));
                cursor_y = child_box->y + child_box->height + child_box->margin[2];
                box.children.push_back(std::move(child_box));
            } else if (child->type() == dom::NodeType::Text) {
                inline_run.push_back(child.get());
            }
        }
        flush_inline_run();
    };

    // ADR-028: grid layout dispatch. If this element's display is
    // grid AND its ComputedStyle declares a parseable template,
    // dispatch to grid children layout. Otherwise (grid without
    // template, or any non-grid display) fall through to block.
    bool grid_handled = false;
    if (display_of(style) == Display::Grid && style) {
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
                                   own_dir));
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

    if (!grid_handled) {
        layout_block_children();
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
                 float viewport_width) {
    const dom::Element* root = find_root_element(doc);
    if (!root) return LayoutBox{};

    const css::ComputedStyle* root_style = lookup(styles, *root);
    const float root_fs =
        font_size_of(root_style, kDefaultFontSize, kDefaultFontSize);

    return layout_element(*root, styles, measurer, 0.0f, 0.0f, viewport_width,
                          kDefaultFontSize, root_fs, Direction::Ltr);
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
