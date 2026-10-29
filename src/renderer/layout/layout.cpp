#include "renderer/layout/layout.hpp"
#include "renderer/layout/grid_layout.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "renderer/layout/style_values.hpp"
#include "renderer/layout/text_utils.hpp"

#include "base/log.hpp"

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

// ADR-045: shared post-process for inline background-image +
// padding-right. Called from both LayoutText path (single-element
// text runs, where <a> goes today) and InlineContext path
// (inline formatting context with multiple mixed-style words).
void apply_inline_bg_pp(
    std::vector<LineFragment>& frags,
    const css::ComputedStyleMap& styles,
    float font_size, float parent_font_size,
    float root_font_size, float content_w) {
    LengthContext pad_ctx;
    pad_ctx.font_size = font_size;
    pad_ctx.parent_font_size = parent_font_size;
    pad_ctx.root_font_size = root_font_size;
    pad_ctx.parent_content_width = content_w;
    pad_ctx.on_font_size_property = false;

    float cur_line_y = -1.0e30f;
    float accumulated_shift = 0.0f;
    for (auto& frag : frags) {
        if (frag.y != cur_line_y) {
            cur_line_y = frag.y;
            accumulated_shift = 0.0f;
        }
        if (accumulated_shift > 0.0f) {
            frag.x += accumulated_shift;
        }
        if (!frag.element) continue;
        const css::ComputedStyle* fs = lookup(styles, *frag.element);
        if (!fs) continue;

        if (auto bg = fs->get("background-image");
            bg && !bg->empty() && *bg != "none") {
            const auto upos = bg->find("url(");
            if (upos != std::string::npos) {
                auto s = upos + 4;
                while (s < bg->size() &&
                       ((*bg)[s] == ' ' || (*bg)[s] == '\'' ||
                        (*bg)[s] == '"')) ++s;
                auto e = bg->find_first_of(" \t)'\"", s);
                if (e == std::string::npos) e = bg->size();
                frag.bg_image_url = bg->substr(s, e - s);
            }
        }

        if (auto sz = fs->get("background-size");
            sz && !sz->empty() && *sz != "auto") {
            std::string tok;
            for (char c : *sz) {
                if (c == ' ' || c == '\t') break;
                tok.push_back(c);
            }
            if (!tok.empty() && tok != "cover" && tok != "contain"
                && tok.back() != '%') {
                frag.bg_size_w = parse_length(tok, pad_ctx);
            }
        }

        if (auto ps = fs->get("background-position");
            ps && !ps->empty()) {
            std::vector<std::string> toks;
            {
                std::string t;
                for (char c : *ps) {
                    if (c == ' ' || c == '\t') {
                        if (!t.empty()) {
                            toks.push_back(t); t.clear();
                        }
                    } else t.push_back(c);
                }
                if (!t.empty()) toks.push_back(t);
            }
            auto px = [](const std::string& t) -> BgPosX {
                if (t == "right") return BgPosX::Right;
                if (t == "center") return BgPosX::Center;
                return BgPosX::Left;
            };
            auto py = [](const std::string& t) -> BgPosY {
                if (t == "bottom") return BgPosY::Bottom;
                if (t == "center") return BgPosY::Center;
                return BgPosY::Top;
            };
            if (toks.size() == 1) {
                const auto& t = toks[0];
                if (t == "center") {
                    frag.bg_pos_x = BgPosX::Center;
                    frag.bg_pos_y = BgPosY::Center;
                } else if (t == "left" || t == "right") {
                    frag.bg_pos_x = px(t);
                } else if (t == "top" || t == "bottom") {
                    frag.bg_pos_y = py(t);
                }
            } else if (toks.size() >= 2) {
                const auto& t0 = toks[0];
                const auto& t1 = toks[1];
                if (t1 == "left" || t1 == "right") {
                    frag.bg_pos_x = px(t1);
                    frag.bg_pos_y = py(t0);
                } else {
                    frag.bg_pos_x = px(t0);
                    frag.bg_pos_y = py(t1);
                }
            }
        }

        if (auto rr = fs->get("background-repeat");
            rr && !rr->empty()) {
            if (rr->find("no-repeat") != std::string::npos) {
                frag.bg_no_repeat = true;
            }
        }

        if (auto pad = fs->get("padding-right");
            pad && !pad->empty() && *pad != "0" && *pad != "auto") {
            const float pv = parse_length(*pad, pad_ctx);
            if (pv > 0.0f) {
                frag.width += pv;
                accumulated_shift += pv;
            }
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

    // ADR-033: parse position. ADR-042 extends: relative and
    // absolute are now used (relative = containing block marker,
    // absolute = out-of-flow + positioned). Sticky remains
    // stored-but-static. Fixed behavior unchanged (ADR-033
    // special case in layout_block_children).
    if (style) {
        if (auto p = style->get("position")) {
            if (*p == "relative") box.position = Position::Relative;
            else if (*p == "absolute") box.position = Position::Absolute;
            else if (*p == "fixed") box.position = Position::Fixed;
            else if (*p == "sticky") box.position = Position::Sticky;
            // ADR-042: any non-static position is a containing
            // block for absolute descendants.
            if (box.position != Position::Static) {
                box.is_containing_block = true;
            }
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

        // ADR-043: parse box-shadow. Non-inset, non-blurred
        // rendered in Commit 2; inset / blur / multi-shadow
        // parsed and stored but skipped at paint.
        if (auto bs = style->get("box-shadow");
            bs && !bs->empty() && *bs != "none") {
            // Multi-shadow: take first only (comma-separated
            // list; each remaining shadow is silently dropped).
            std::string first = *bs;
            if (auto comma = first.find(',');
                comma != std::string::npos) {
                first = first.substr(0, comma);
            }

            // Tokenize on whitespace.
            std::vector<std::string> tokens;
            {
                std::istringstream iss(first);
                std::string tok;
                while (iss >> tok) tokens.push_back(tok);
            }

            // Optional `inset` keyword at start.
            bool inset = false;
            if (!tokens.empty() && tokens[0] == "inset") {
                inset = true;
                tokens.erase(tokens.begin());
            }

            // Validate: 2-4 length tokens + 1 color token.
            // Length token: "0" exactly, or numeric prefix +
            // px|em|rem|% suffix. Rejects solid / auto / etc.
            auto is_length_token = [](const std::string& t) -> bool {
                if (t == "0") return true;
                auto ends_with = [&](std::string_view suf) {
                    return t.size() > suf.size() &&
                        t.compare(t.size() - suf.size(),
                                  suf.size(), suf) == 0;
                };
                size_t unit_len = 0;
                if (ends_with("rem")) unit_len = 3;
                else if (ends_with("px")) unit_len = 2;
                else if (ends_with("em")) unit_len = 2;
                else if (ends_with("%"))  unit_len = 1;
                else return false;
                const size_t num_end = t.size() - unit_len;
                if (num_end == 0) return false;
                bool has_digit = false;
                for (size_t i = 0; i < num_end; ++i) {
                    const char c = t[i];
                    if (c >= '0' && c <= '9') { has_digit = true; continue; }
                    if (c == '.') continue;
                    if (c == '-' && i == 0) continue;
                    return false;
                }
                return has_digit;
            };

            if (tokens.size() >= 3 && tokens.size() <= 5) {
                bool ok = true;
                std::vector<float> lens;
                for (size_t i = 0; i + 1 < tokens.size(); ++i) {
                    if (!is_length_token(tokens[i])) { ok = false; break; }
                    lens.push_back(parse_length(tokens[i], ctx));
                }
                if (ok && lens.size() >= 2 && lens.size() <= 4) {
                    box.box_shadow_ox     = lens[0];
                    box.box_shadow_oy     = lens[1];
                    box.box_shadow_blur   = lens.size() >= 3 ? lens[2] : 0.0f;
                    box.box_shadow_spread = lens.size() >= 4 ? lens[3] : 0.0f;
                    // Note: transparent-fallback for unknown color
                    // tokens yields an invisible shadow (v1
                    // acceptable; matches "no visual effect").
                    box.box_shadow_color  = parse_color(tokens.back());
                    box.box_shadow_inset  = inset;
                    box.box_shadow_has    = true;
                }
            }
        }

        // ADR-044: parse border-radius. Shorthand (single
        // value) fills all four corners; per-corner longhands
        // override. Percentage and `inherit` deferred (leave 0;
        // documented -- radio buttons display:none in target).
        // Elliptical and multi-value: take first token only
        // (0 uses; documented deviation for future).
        auto parse_radius_token = [&](const char* prop)
                -> std::optional<float> {
            const auto v = style->get(prop);
            if (!v || v->empty()) return std::nullopt;
            if (*v == "inherit" || *v == "unset" || *v == "initial") {
                return std::nullopt;
            }
            std::string first = *v;
            // Elliptical form `5px / 10px`: take before the slash.
            if (auto slash = first.find('/');
                slash != std::string::npos) {
                first = first.substr(0, slash);
            }
            // Multi-value form: take first whitespace-delimited token.
            std::string token;
            for (char c : first) {
                if (c == ' ' || c == '\t') break;
                token.push_back(c);
            }
            if (token.empty()) return std::nullopt;
            // Percentage deferred (needs box's own dimensions,
            // not parent's; parse_length's ctx would give a
            // semantically wrong value). Documented deviation.
            if (token.back() == '%') return std::nullopt;
            return parse_length(token, ctx);
        };

        if (const auto sh = parse_radius_token("border-radius"); sh) {
            box.border_radius[0] = *sh;
            box.border_radius[1] = *sh;
            box.border_radius[2] = *sh;
            box.border_radius[3] = *sh;
        }
        if (const auto v = parse_radius_token("border-top-left-radius"); v)
            box.border_radius[0] = *v;
        if (const auto v = parse_radius_token("border-top-right-radius"); v)
            box.border_radius[1] = *v;
        if (const auto v = parse_radius_token("border-bottom-right-radius"); v)
            box.border_radius[2] = *v;
        if (const auto v = parse_radius_token("border-bottom-left-radius"); v)
            box.border_radius[3] = *v;

        // ADR-047: parse mask-image + mask-size / -position /
        // -repeat. Both -webkit- prefixed and unprefixed forms
        // accepted; unprefixed wins if both present.
        // ADR-047: return by value (owning copy). Returning a
        // pointer into a temporary optional was a use-after-free
        // (SIGSEGV on first parse). Cost: one small-string copy
        // per mask parse -- negligible.
        auto mask_lookup = [&](const char* unprefixed,
                               const char* webkit) -> std::string {
            if (auto v = style->get(unprefixed); v) return *v;
            if (auto v = style->get(webkit); v) return *v;
            return {};
        };
        if (const std::string mv =
                mask_lookup("mask-image", "-webkit-mask-image");
            !mv.empty() && mv != "none") {
            if (mv.find("url(") != std::string::npos) {
                const auto start = mv.find("url(") + 4;
                const auto end = mv.find(')', start);
                if (end != std::string::npos) {
                    std::string url = mv.substr(start, end - start);
                    while (!url.empty() &&
                           (url.front() == ' ' || url.front() == '\t'))
                        url.erase(url.begin());
                    while (!url.empty() &&
                           (url.back() == ' ' || url.back() == '\t'))
                        url.pop_back();
                    if (!url.empty() &&
                        (url.front() == '\'' || url.front() == '"'))
                        url.erase(url.begin());
                    if (!url.empty() &&
                        (url.back() == '\'' || url.back() == '"'))
                        url.pop_back();
                    if (!url.empty()) {
                        box.mask_image_url = url;
                        box.mask_has = true;
                    }
                }
            }
        }
        if (box.mask_has) {
            if (const std::string sz = mask_lookup(
                    "mask-size", "-webkit-mask-size");
                !sz.empty() && sz != "auto") {
                std::string tok;
                for (char c : sz) {
                    if (c == ' ' || c == '\t') break;
                    tok.push_back(c);
                }
                if (!tok.empty() && tok != "cover" && tok != "contain"
                    && tok.back() != '%') {
                    box.mask_size_w = parse_length(tok, ctx);
                }
            }
            if (const std::string ps = mask_lookup(
                    "mask-position", "-webkit-mask-position");
                !ps.empty()) {
                std::vector<std::string> toks;
                {
                    std::string t;
                    for (char c : ps) {
                        if (c == ' ' || c == '\t') {
                            if (!t.empty()) {
                                toks.push_back(t); t.clear();
                            }
                        } else t.push_back(c);
                    }
                    if (!t.empty()) toks.push_back(t);
                }
                auto px = [](const std::string& t) -> BgPosX {
                    if (t == "right") return BgPosX::Right;
                    if (t == "center") return BgPosX::Center;
                    return BgPosX::Left;
                };
                auto py = [](const std::string& t) -> BgPosY {
                    if (t == "bottom") return BgPosY::Bottom;
                    if (t == "center") return BgPosY::Center;
                    return BgPosY::Top;
                };
                if (toks.size() == 1) {
                    const auto& t = toks[0];
                    if (t == "center") {
                        box.mask_pos_x = BgPosX::Center;
                        box.mask_pos_y = BgPosY::Center;
                    } else if (t == "left" || t == "right") {
                        box.mask_pos_x = px(t);
                    } else if (t == "top" || t == "bottom") {
                        box.mask_pos_y = py(t);
                    }
                } else if (toks.size() >= 2) {
                    const auto& t0 = toks[0];
                    const auto& t1 = toks[1];
                    if (t1 == "left" || t1 == "right") {
                        box.mask_pos_x = px(t1);
                        box.mask_pos_y = py(t0);
                    } else {
                        box.mask_pos_x = px(t0);
                        box.mask_pos_y = py(t1);
                    }
                }
            }
            if (const std::string rr = mask_lookup(
                    "mask-repeat", "-webkit-mask-repeat");
                !rr.empty()) {
                if (rr.find("no-repeat") != std::string::npos) {
                    box.mask_no_repeat = true;
                }
            }
        }
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
            apply_inline_bg_pp(fragments, styles, own_font_size,
                               parent_font_size, root_font_size,
                               content_w);
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

        // ADR-045: post-process ctx->lines.
        // 1. Populate background-image / size / position / repeat
        //    from element's ComputedStyleMap.
        // 2. Extend fragment width by inline padding-right and
        //    shift subsequent same-line fragments rightward
        //    (creates the gap the external-link icon draws in).
        // RTL: padding extension handled only for LTR; Wikipedia
        // is LTR-only, so this is a documented deferral.
        //
        // Note: `ctx` in this scope is the unique_ptr to
        // LayoutInlineContext -- NOT the outer LengthContext.
        // Build a fresh LengthContext for parse_length calls.
        {
            LengthContext pad_ctx;
            pad_ctx.font_size = own_font_size;
            pad_ctx.parent_font_size = parent_font_size;
            pad_ctx.root_font_size = root_font_size;
            pad_ctx.parent_content_width = content_w;
            pad_ctx.on_font_size_property = false;

            float cur_line_y = -1.0e30f;
            float accumulated_shift = 0.0f;
            for (auto& frag : ctx->lines) {
                // New line: reset per-line shift.
                if (frag.y != cur_line_y) {
                    cur_line_y = frag.y;
                    accumulated_shift = 0.0f;
                }
                // Apply shift from previous fragments' padding.
                if (accumulated_shift > 0.0f) {
                    frag.x += accumulated_shift;
                }
                if (!frag.element) continue;
                const css::ComputedStyle* fs = lookup(styles, *frag.element);
                if (!fs) continue;

                // background-image: extract url(...)
                if (auto bg = fs->get("background-image");
                    bg && !bg->empty() && *bg != "none") {
                    const auto upos = bg->find("url(");
                    if (upos != std::string::npos) {
                        auto s = upos + 4;
                        while (s < bg->size() &&
                               ((*bg)[s] == ' ' || (*bg)[s] == '\'' ||
                                (*bg)[s] == '"')) ++s;
                        auto e = bg->find_first_of(" \t)'\"", s);
                        if (e == std::string::npos) e = bg->size();
                        frag.bg_image_url = bg->substr(s, e - s);
                    }
                }

                // background-size: first token; auto/cover/contain/
                // percentage all deferred (0 -> auto in paint).
                if (auto sz = fs->get("background-size");
                    sz && !sz->empty() && *sz != "auto") {
                    std::string tok;
                    for (char c : *sz) {
                        if (c == ' ' || c == '\t') break;
                        tok.push_back(c);
                    }
                    if (!tok.empty() && tok != "cover" && tok != "contain"
                        && tok.back() != '%') {
                        // Length token: numeric prefix + unit, or "0".
                        frag.bg_size_w = parse_length(tok, pad_ctx);
                    }
                }

                // background-position: keyword pair (flexible order).
                if (auto ps = fs->get("background-position");
                    ps && !ps->empty()) {
                    std::vector<std::string> toks;
                    {
                        std::string t;
                        for (char c : *ps) {
                            if (c == ' ' || c == '\t') {
                                if (!t.empty()) {
                                    toks.push_back(t); t.clear();
                                }
                            } else t.push_back(c);
                        }
                        if (!t.empty()) toks.push_back(t);
                    }
                    auto px = [](const std::string& t) -> BgPosX {
                        if (t == "right") return BgPosX::Right;
                        if (t == "center") return BgPosX::Center;
                        return BgPosX::Left;
                    };
                    auto py = [](const std::string& t) -> BgPosY {
                        if (t == "bottom") return BgPosY::Bottom;
                        if (t == "center") return BgPosY::Center;
                        return BgPosY::Top;
                    };
                    if (toks.size() == 1) {
                        const auto& t = toks[0];
                        if (t == "center") {
                            frag.bg_pos_x = BgPosX::Center;
                            frag.bg_pos_y = BgPosY::Center;
                        } else if (t == "left" || t == "right") {
                            frag.bg_pos_x = px(t);
                        } else if (t == "top" || t == "bottom") {
                            frag.bg_pos_y = py(t);
                        }
                    } else if (toks.size() >= 2) {
                        const auto& t0 = toks[0];
                        const auto& t1 = toks[1];
                        // CSS allows either order for X/Y keywords.
                        // If t1 is X-only (left/right), swap.
                        if (t1 == "left" || t1 == "right") {
                            frag.bg_pos_x = px(t1);
                            frag.bg_pos_y = py(t0);
                        } else {
                            frag.bg_pos_x = px(t0);
                            frag.bg_pos_y = py(t1);
                        }
                    }
                }

                // background-repeat: no-repeat only (0 uses of
                // repeat/repeat-x/repeat-y in target page).
                if (auto rr = fs->get("background-repeat");
                    rr && !rr->empty()) {
                    if (rr->find("no-repeat") != std::string::npos) {
                        frag.bg_no_repeat = true;
                    }
                }

                // padding-right: extend fragment width, accumulate
                // shift for subsequent same-line fragments. This is
                // the CSS "gap" that the external-link icon draws
                // in (via background-position: center right).
                if (auto pad = fs->get("padding-right");
                    pad && !pad->empty() && *pad != "0" && *pad != "auto") {
                    const float pv = parse_length(*pad, pad_ctx);
                    if (pv > 0.0f) {
                        frag.width += pv;
                        accumulated_shift += pv;
                    }
                }
            }
        }

        ctx->width = max_line_w;
        ctx->height = total_h;
        cursor_y += total_h;
        box.children.push_back(std::move(ctx));
    };

    // ADR-028: block/inline children layout. Reused by the grid
    // path's fallback (grid without a parsed template degrades to
    // block) and by the standard block path.
    // ADR-042: absolute children of a containing block are
    // collected here and positioned against self's content box
    // after normal (in-flow) children finish. Flex/grid parent
    // absolutes are handled separately (deferred -- v1 only
    // handles block-parent absolutes; covers titlebar case).
    std::vector<const dom::Element*> pending_absolutes;

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

                // ADR-042: absolute child of a containing block
                // is out-of-flow; collect for post-positioning.
                if (box.is_containing_block && child_style) {
                    if (auto p = child_style->get("position");
                        p && *p == "absolute") {
                        pending_absolutes.push_back(&child_el);
                        continue;
                    }
                }

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

        // ADR-048: extracted per-line flex resolution. Runs
        // Phases B-E (grow/shrink, justify positioning, per-item
        // layout, cross alignment) with an explicit line context
        // so single-line and multi-line both share one
        // implementation. Returns container cross extent +
        // used main extent for the caller to advance cursor_y
        // (single-line) or stack lines (multi-line, Commit 2).
        struct FlexLineResult {
            float container_cross;
            float used_main;
        };
        auto resolve_flex_line = [&](std::vector<FC>& line_fcs,
                                      float line_main_start,
                                      float line_cross_start,
                                      float line_main_size,
                                      const std::string& justify_str,
                                      const std::string& align_str,
                                      bool line_is_row,
                                      float line_gap) -> FlexLineResult {
            if (line_fcs.empty()) return {0.0f, 0.0f};

            // Phase B: flex resolution (grow/shrink row only;
            // column's container main is intrinsic).
            float total_basis = line_gap *
                static_cast<float>(line_fcs.size() - 1);
            for (const auto& fc : line_fcs) total_basis += fc.basis;

            if (line_is_row) {
                const float free = line_main_size - total_basis;
                if (free < 0.0f) {
                    float total_shrink = 0.0f;
                    for (const auto& fc : line_fcs) {
                        total_shrink += fc.shrink * fc.basis;
                    }
                    if (total_shrink > 0.0f) {
                        for (auto& fc : line_fcs) {
                            const float amt =
                                (fc.shrink * fc.basis / total_shrink) *
                                (-free);
                            fc.final_main =
                                std::max(0.0f, fc.basis - amt);
                        }
                    }
                } else if (free > 0.0f) {
                    float total_grow = 0.0f;
                    for (const auto& fc : line_fcs) {
                        total_grow += fc.grow;
                    }
                    if (total_grow > 0.0f) {
                        for (auto& fc : line_fcs) {
                            fc.final_main =
                                fc.basis +
                                (fc.grow / total_grow) * free;
                        }
                    }
                }
            }

            // Phase C: justify positioning.
            float used_main = line_gap *
                static_cast<float>(line_fcs.size() - 1);
            for (const auto& fc : line_fcs) used_main += fc.final_main;
            const float remaining = line_is_row
                ? std::max(0.0f, line_main_size - used_main) : 0.0f;

            float main_cursor = line_main_start;
            float effective_gap = line_gap;
            if (remaining > 0.0f) {
                if (justify_str == "flex-end" || justify_str == "end") {
                    main_cursor += remaining;
                } else if (justify_str == "center") {
                    main_cursor += remaining / 2.0f;
                } else if (justify_str == "space-between" &&
                           line_fcs.size() > 1) {
                    effective_gap += remaining /
                        static_cast<float>(line_fcs.size() - 1);
                } else if (justify_str == "space-around") {
                    const float around =
                        remaining / static_cast<float>(line_fcs.size());
                    main_cursor += around / 2.0f;
                    effective_gap += around;
                }
            }

            // Phase D: layout per item + track cross extent.
            float container_cross = 0.0f;
            for (auto& fc : line_fcs) {
                const float cw = line_is_row ? fc.final_main
                                             : line_main_size;
                const float x = line_is_row ? main_cursor
                                            : line_cross_start;
                const float y = line_is_row ? 0.0f : main_cursor;

                fc.final_box = std::make_unique<LayoutBox>(
                    layout_element(*fc.el, styles, measurer, x, y, cw,
                                   own_font_size, root_font_size,
                                   own_dir, image_dims));
                const float cross_size = line_is_row
                    ? fc.final_box->height : fc.final_box->width;
                if (cross_size > container_cross) {
                    container_cross = cross_size;
                }
                main_cursor += fc.final_main + effective_gap;
            }

            // Phase E: cross-axis alignment (row only; column's
            // align-items deferred per ADR-029).
            if (line_is_row) {
                for (auto& fc : line_fcs) {
                    float cross_off = 0.0f;
                    if (align_str == "flex-end") {
                        cross_off = container_cross -
                                    fc.final_box->height;
                    } else if (align_str == "center") {
                        cross_off = (container_cross -
                                     fc.final_box->height) / 2.0f;
                    }
                    // stretch: no-op (natural height, top-aligned) —
                    // documented v1 limitation.
                    shift_subtree_y(*fc.final_box,
                                    line_cross_start + cross_off);
                }
            }
            return {container_cross, used_main};
        };

        if (!fcs.empty()) {
            const float line_main_start = is_row ? content_x : content_y;
            const float line_cross_start = is_row ? content_y : content_x;
            const FlexLineResult r = resolve_flex_line(
                fcs, line_main_start, line_cross_start, content_w,
                justify, align, is_row, gap);
            for (auto& fc : fcs) {
                box.children.push_back(std::move(fc.final_box));
            }
            cursor_y = content_y + (is_row ? r.container_cross
                                            : r.used_main);
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

        // ADR-042: position absolute children against self's
        // content box (which is now finalized). Only block
        // parent case is handled in v1; flex/grid parent
        // absolutes fall through their own loops and remain
        // static (documented deferral).
        if (box.is_containing_block && !pending_absolutes.empty()) {
            const float abs_cx =
                box.x + box.border[3] + box.padding[3];
            const float abs_cy =
                box.y + box.border[0] + box.padding[0];

            // ADR-042: box.width / box.height are not yet
            // finalized at this point (they're set after the
            // dispatch block for some elements). Parse explicit
            // width/height from style when present, else fall
            // back to box dimensions.
            LengthContext dim_ctx;
            dim_ctx.font_size = own_font_size;
            dim_ctx.parent_font_size = parent_font_size;
            dim_ctx.root_font_size = root_font_size;
            dim_ctx.parent_content_width = parent_content_width;

            float abs_cw =
                box.width - box.border[1] - box.border[3]
                          - box.padding[1] - box.padding[3];
            float abs_ch =
                box.height - box.border[0] - box.border[2]
                           - box.padding[0] - box.padding[2];
            if (style) {
                if (auto w = style->get("width");
                    w && *w != "auto") {
                    abs_cw = parse_length(*w, dim_ctx);
                }
                if (auto h = style->get("height");
                    h && *h != "auto") {
                    abs_ch = parse_length(*h, dim_ctx);
                }
            }

            LengthContext abs_ctx;
            abs_ctx.font_size = own_font_size;
            abs_ctx.parent_font_size = parent_font_size;
            abs_ctx.root_font_size = root_font_size;
            abs_ctx.parent_content_width = abs_cw;

            for (const auto* abs_el : pending_absolutes) {
                const auto* abs_style = lookup(styles, *abs_el);
                if (!abs_style) continue;

                // Pre-layout at (0,0) to establish natural size.
                auto abs_box = std::make_unique<LayoutBox>(
                    layout_element(*abs_el, styles, measurer,
                                   0.0f, 0.0f, abs_cw,
                                   own_font_size, root_font_size,
                                   own_dir, image_dims));

                const float natural_w = abs_box->width;
                const float natural_h = abs_box->height;

                // Parse offsets (auto = nullopt).
                auto parse_off = [&](const char* prop)
                        -> std::optional<float> {
                    if (auto v = abs_style->get(prop);
                        v && *v != "auto") {
                        return parse_length(*v, abs_ctx);
                    }
                    return std::nullopt;
                };
                const auto top_o    = parse_off("top");
                const auto bottom_o = parse_off("bottom");
                const auto left_o   = parse_off("left");
                const auto right_o  = parse_off("right");

                // Horizontal: compute target_x (x_before_margin)
                // and target_w. Over-constrained: left wins
                // (CSS SS10.3.7 rule 1, correct for LTR).
                float target_x = 0.0f;
                float target_w = natural_w;
                if (left_o && right_o) {
                    target_x = abs_cx + *left_o;
                    target_w = abs_cw - *left_o - *right_o;
                } else if (left_o) {
                    target_x = abs_cx + *left_o;
                } else if (right_o) {
                    target_x = abs_cx + abs_cw - *right_o - natural_w;
                } else {
                    target_x = abs_cx;
                }

                // Vertical.
                float target_y = 0.0f;
                float target_h = natural_h;
                if (top_o && bottom_o) {
                    target_y = abs_cy + *top_o;
                    target_h = abs_ch - *top_o - *bottom_o;
                } else if (top_o) {
                    target_y = abs_cy + *top_o;
                } else if (bottom_o) {
                    target_y = abs_cy + abs_ch - *bottom_o - natural_h;
                } else {
                    target_y = abs_cy;
                }

                // Shift from (0,0)-layout to target position.
                if (target_x != 0.0f) {
                    shift_subtree_x(*abs_box, target_x);
                }
                if (target_y != 0.0f) {
                    shift_subtree_y(*abs_box, target_y);
                }

                // Apply stretch overrides (v1: box size set post-
                // layout; nested content may not reflow perfectly
                // -- documented limitation, no impact for the
                // first-viewport titlebar pseudo).
                if (target_w != natural_w) abs_box->width  = target_w;
                if (target_h != natural_h) abs_box->height = target_h;

                box.children.push_back(std::move(abs_box));
            }
        }
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

// =============================================================
// ADR-042 Commit 2: pseudo-element synthesis (post-walk).
// =============================================================

// ADR-042: synthesize a pseudo-element box for `ps` (the
// pseudo-element's ComputedStyle) and attach it to `parent`.
// Only `content: ''` supported (empty literal); literal text,
// attr(), counter(), url() all skipped (return without box).
// Position: absolute boxes are positioned against parent's
// content box per ADR-042 Commit 1 rules. Position: static
// boxes append after parent's existing children.
void apply_one_pseudo(LayoutBox& parent,
                      const css::ComputedStyle& ps,
                      float parent_font_size,
                      float root_font_size,
                      bool is_before) {
    auto content = ps.get("content");
    if (!content) return;
    const std::string& v = *content;
    // v1: only empty-string literal (`''` or `""`)
    if (v != "''" && v != "\"\"") return;

    // Parse position
    Position pos = Position::Static;
    if (auto p = ps.get("position")) {
        if (*p == "absolute") pos = Position::Absolute;
        else if (*p == "relative") pos = Position::Relative;
    }

    // Parent's content box
    const float parent_cx = parent.x + parent.border[3] + parent.padding[3];
    const float parent_cy = parent.y + parent.border[0] + parent.padding[0];
    const float parent_cw =
        parent.width - parent.border[1] - parent.border[3]
                     - parent.padding[1] - parent.padding[3];
    const float parent_ch =
        parent.height - parent.border[0] - parent.border[2]
                      - parent.padding[0] - parent.padding[2];

    LengthContext ctx;
    ctx.font_size = parent_font_size;
    ctx.parent_font_size = parent_font_size;
    ctx.root_font_size = root_font_size;
    ctx.parent_content_width = parent_cw > 0.0f ? parent_cw : 0.0f;

    // Explicit width/height
    float w = parent_cw > 0.0f ? parent_cw : 0.0f;
    float h = 0.0f;
    if (auto wh = ps.get("width"); wh && *wh != "auto") {
        w = parse_length(*wh, ctx);
    }
    if (auto hh = ps.get("height"); hh && *hh != "auto") {
        h = parse_length(*hh, ctx);
    }

    // Parse offsets
    auto parse_off = [&](const char* prop) -> std::optional<float> {
        if (auto o = ps.get(prop); o && *o != "auto") {
            return parse_length(*o, ctx);
        }
        return std::nullopt;
    };
    const auto top_o    = parse_off("top");
    const auto bottom_o = parse_off("bottom");
    const auto left_o   = parse_off("left");
    const auto right_o  = parse_off("right");

    // Synthesize box
    auto pb = std::make_unique<LayoutBox>();
    pb->element = nullptr;
    pb->position = pos;

    // Background / border / padding
    if (auto bg = ps.get("background-color")) {
        pb->background = parse_color(*bg);
    }
    if (auto bc = ps.get("border-color")) {
        pb->border_color = parse_color(*bc);
    }
    if (auto bw = ps.get("border-width"); bw && *bw != "0") {
        const float wv = parse_length(*bw, ctx);
        pb->border[0] = pb->border[1] = pb->border[2] = pb->border[3] = wv;
    }
    if (auto pad = ps.get("padding")) {
        const float pv = parse_length(*pad, ctx);
        pb->padding[0] = pb->padding[1] = pb->padding[2] = pb->padding[3] = pv;
    }

    if (pos == Position::Absolute) {
        float x = parent_cx, y = parent_cy;
        float w_final = w, h_final = h;

        if (left_o && right_o) {
            x = parent_cx + *left_o;
            w_final = parent_cw - *left_o - *right_o;
        } else if (left_o) {
            x = parent_cx + *left_o;
        } else if (right_o) {
            x = parent_cx + parent_cw - *right_o - w;
        } else {
            x = parent_cx;
        }

        if (top_o && bottom_o) {
            y = parent_cy + *top_o;
            h_final = parent_ch - *top_o - *bottom_o;
        } else if (top_o) {
            y = parent_cy + *top_o;
        } else if (bottom_o) {
            y = parent_cy + parent_ch - *bottom_o - h;
        } else {
            y = parent_cy;
        }

        pb->x = x;
        pb->y = y;
        pb->width  = w_final + pb->padding[3] + pb->padding[1]
                             + pb->border[3] + pb->border[1];
        pb->height = h_final + pb->padding[0] + pb->padding[2]
                             + pb->border[0] + pb->border[2];
    } else {
        // Static: append after existing flow children.
        float flow_y = parent_cy;
        for (const auto& c : parent.children) {
            if (c->kind == LayoutKind::Box) {
                const auto& cb = static_cast<const LayoutBox&>(*c);
                const float bottom = cb.y + cb.height + cb.margin[2];
                if (bottom > flow_y) flow_y = bottom;
            }
        }
        pb->x = parent_cx;
        pb->y = flow_y;
        pb->width  = w + pb->padding[3] + pb->padding[1]
                       + pb->border[3] + pb->border[1];
        pb->height = h + pb->padding[0] + pb->padding[2]
                       + pb->border[0] + pb->border[2];
    }

    // ADR-042: insert at begin for ::before, at end for ::after.
    if (is_before) {
        parent.children.insert(parent.children.begin(), std::move(pb));
    } else {
        parent.children.push_back(std::move(pb));
    }
}

// ADR-042: recursive post-walk. Recurses into children first
// (bottom-up), then synthesizes pseudo boxes for this element.
// Only LayoutBox nodes hold elements; inline/text nodes are
// skipped (pseudo-elements on inline parents deferred).
void apply_pseudo_elements(LayoutBox& box,
                           const css::PseudoStyleMap& pseudo_styles,
                           float parent_font_size,
                           float root_font_size) {
    // Bottom-up: recurse into children first.
    for (auto& child : box.children) {
        if (child && child->kind == LayoutKind::Box) {
            apply_pseudo_elements(static_cast<LayoutBox&>(*child),
                                   pseudo_styles,
                                   parent_font_size,
                                   root_font_size);
        }
    }

    if (!box.element) return;
    auto it = pseudo_styles.find(box.element);
    if (it == pseudo_styles.end()) return;

    // before first (so ::before is visual first in children)
    if (!it->second.before.declarations.empty()) {
        apply_one_pseudo(box, it->second.before,
                         parent_font_size, root_font_size,
                         /*is_before=*/true);
    }
    if (!it->second.after.declarations.empty()) {
        apply_one_pseudo(box, it->second.after,
                         parent_font_size, root_font_size,
                         /*is_before=*/false);
    }
}

LayoutBox layout(const dom::Document& doc,
                 const css::ComputedStyleMap& styles,
                 TextMeasurer measurer,
                 float viewport_width,
                 ImageDimLookup image_dims) {
    HB_LOG(Layout, Info, "layout: viewport=%.0f", viewport_width);
    const dom::Element* root = find_root_element(doc);
    if (!root) {
        HB_LOG(Layout, Warn, "layout: no root element");
        return LayoutBox{};
    }

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

// ADR-042 Commit 2: layout with pseudo-element synthesis.
// Runs the normal layout tree, then walks it bottom-up to
// synthesize ::before / ::after boxes from pseudo_styles.
LayoutBox layout_with_pseudo(const dom::Document& doc,
                             const css::ComputedStyleMap& styles,
                             const css::PseudoStyleMap& pseudo_styles,
                             TextMeasurer measurer,
                             float viewport_width,
                             ImageDimLookup image_dims) {
    LayoutBox root = layout(doc, styles, measurer, viewport_width,
                            image_dims);
    // Root font size defaults to 16 (pseudo font-size inheritance
    // from root isn't needed for content: '' -- no text).
    apply_pseudo_elements(root, pseudo_styles,
                          /*parent_font_size=*/16.0f,
                          /*root_font_size=*/16.0f);
    return root;
}

const dom::Element* hit_test(const LayoutNode& root,
                             float x, float y) {
    return hit_test_node(root, x, y);
}
} // namespace honeybee::renderer::layout
