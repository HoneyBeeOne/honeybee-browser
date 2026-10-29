#include "render_pipeline.hpp"
#include "net/client.hpp"
#include "base/url.hpp"

// ADR-034: SVG decode + data URL handling.
#include <librsvg/rsvg.h>

#include <cstdio>
#include <cstring>
#include <string_view>

#include <functional>
#include <string>
#include <unordered_set>

#include "pango_text.hpp"
#include "renderer/css/cascade.hpp"
#include "renderer/css/hover_sensitivity.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/dom/node.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/html/css_sources.hpp"
#include "renderer/layout/layout.hpp"
#include "renderer/layout/style_values.hpp"
#include "renderer/paint/paint.hpp"

#include "base/log.hpp"

namespace honeybee::browser {

namespace {

// Collect the concatenated text of all <style> elements in
// `doc`. v1 only supports inline styles; <link rel=stylesheet>
// requires network loading and is deferred.
// ADR-021: walk the layout tree updating paint-only color
// fields from freshly-recomputed styles. Skips dimensions
// entirely -- x, y, width, height are never touched.
//
// Diagnostic finding (see commit message): paint only reads
// LayoutBox::background, LayoutBox::border_color, and
// LineFragment::color. LayoutText::color and LayoutInline::color
// are dead state -- inline nodes are flattened before paint,
// and LayoutText itself is not drawn directly (its fragments
// are). So the walk updates exactly those three fields and
// nothing else; no LayoutText::element backfill was needed
// because every LineFragment already carries its source
// element from ADR-018.
void restyle_walk(renderer::layout::LayoutNode& node,
                  const renderer::css::ComputedStyleMap& styles) {
    using renderer::layout::LayoutBox;
    using renderer::layout::LayoutInline;
    using renderer::layout::LayoutInlineContext;
    using renderer::layout::LayoutKind;
    using renderer::layout::LayoutText;
    using renderer::layout::parse_color;

    switch (node.kind) {
        case LayoutKind::Box: {
            auto& box = static_cast<LayoutBox&>(node);
            if (box.element) {
                auto it = styles.find(box.element);
                if (it != styles.end()) {
                    const auto bg = it->second.get("background-color");
                    box.background = bg
                        ? parse_color(*bg)
                        : renderer::layout::Color::transparent();
                    const auto bc = it->second.get("border-color");
                    box.border_color = bc
                        ? parse_color(*bc)
                        : renderer::layout::Color::transparent();
                }
            }
            for (auto& child : box.children) {
                restyle_walk(*child, styles);
            }
            break;
        }
        case LayoutKind::Text: {
            auto& lt = static_cast<LayoutText&>(node);
            for (auto& frag : lt.lines) {
                if (!frag.element) continue;
                auto it = styles.find(frag.element);
                if (it == styles.end()) continue;
                const auto cv = it->second.get("color");
                if (cv) frag.color = parse_color(*cv);
            }
            break;
        }
        case LayoutKind::Inline: {
            auto& inl = static_cast<LayoutInline&>(node);
            for (auto& child : inl.children) {
                restyle_walk(*child, styles);
            }
            break;
        }
        case LayoutKind::InlineContext: {
            auto& ctx = static_cast<LayoutInlineContext&>(node);
            for (auto& frag : ctx.lines) {
                if (!frag.element) continue;
                auto it = styles.find(frag.element);
                if (it == styles.end()) continue;
                const auto cv = it->second.get("color");
                if (cv) frag.color = parse_color(*cv);
            }
            for (auto& child : ctx.children) {
                restyle_walk(*child, styles);
            }
            break;
        }
    }
}

// ADR-029: viewport-driven feature class injection.
//
// Wikipedia's Vector 2022 skin ships <html class="... vector-
// feature-main-menu-pinned-disabled ..."> when the page is
// fetched without JS. A small inline <script> at page load flips
// specific feature classes from -disabled to -enabled at desktop
// viewports. Without that flip, .vector-main-menu-landmark stays
// visible at 711px, pushing the entire grid container down by
// ~165px (measured: mw-page-container-inner y=585 pre-flex,
// y=750 post-flex without flip).
//
// HoneyBee doesn't run JS. We mimic the flip here: at desktop
// viewport (>=1120, matching the grid @media breakpoint), rewrite
// "vector-feature-main-menu-pinned-disabled" to "-enabled" on
// <html>. This restores the desktop menu layout a real browser
// would show. Also flips back when the viewport shrinks, so a
// desktop->mobile->desktop resize round-trip stays correct.
//
// Deferred: other vector-feature-* flips (language-in-header,
// toc-pinned, page-tools-pinned). Only the main-menu flip is
// needed for ADR-029's visible delta. Future ADRs can extend.
void apply_viewport_feature_classes(
    renderer::dom::Document& doc, float viewport_width) {
    auto* html_el = doc.find_element("html");
    if (!html_el) return;

    // ADR-030: flip table. Each entry maps a -disabled suffix to
    // its -enabled counterpart on <html> at desktop viewport
    // (>=1120, matching grid @media breakpoint and ADR-029's
    // threshold). Undo runs on the same loop when viewport shrinks
    // below threshold, so desktop->mobile->desktop round-trip
    // stays correct.
    //
    // main-menu-pinned:  ADR-029 -- mobile hamburger menu
    //                    visibility: 711px inline -> 0
    // page-tools-pinned: ADR-030 -- Tools panel 687px inline
    //                    -> hidden (article intro moves from
    //                    y=834 to y~=300)
    //
    // clientpref-* flags (toc-pinned, limited-width,
    // custom-font-size, appearance-pinned) are intentionally not
    // flipped: they are user preferences and we don't have
    // localStorage. Flipping would override server-side defaults.
    struct FeatureFlip { const char* disabled; const char* enabled; };
    static constexpr FeatureFlip kDesktopFeatureFlips[] = {
        {"vector-feature-main-menu-pinned-disabled",
         "vector-feature-main-menu-pinned-enabled"},
        {"vector-feature-page-tools-pinned-disabled",
         "vector-feature-page-tools-pinned-enabled"},
    };

    std::string cls = html_el->get_attribute("class");

    auto flip_one = [&](const char* from, const char* to) {
        const std::string from_s(from);
        const std::string to_s(to);
        const auto pos = cls.find(from_s);
        if (pos == std::string::npos) return;
        cls.replace(pos, from_s.size(), to_s);
    };

    const bool desktop = viewport_width >= 1120.0f;
    for (const auto& f : kDesktopFeatureFlips) {
        if (desktop) {
            flip_one(f.disabled, f.enabled);
        } else {
            flip_one(f.enabled, f.disabled);
        }
    }

    html_el->set_attribute("class", cls);
}

} // namespace

void RenderPipeline::render(const std::string& html,
                            float viewport_width) {
    // ADR-025: parse once, extract inline-only CSS, delegate.
    auto doc = renderer::html::parse(html);
    std::string inline_css;
    if (doc) {
        for (const auto& src :
             renderer::html::collect_css_sources(*doc)) {
            if (src.inline_style) {
                inline_css += src.content;
                inline_css += "\n";
            }
        }
    }
    render_with_css(std::move(doc), std::move(inline_css),
                    viewport_width);
}

void RenderPipeline::render_with_css(
    std::unique_ptr<renderer::dom::Document> doc,
    std::string merged_css,
    float viewport_width) {

    // ADR-034: new page load invalidates image cache.
    for (auto& [url, surf] : image_cache_) {
        if (surf) cairo_surface_destroy(surf);
    }
    image_cache_.clear();
    // ADR-018: hovered_element is a raw pointer into doc_. When
    // doc_ is replaced (below), the old DOM is destroyed and
    // hovered_element becomes dangling. The next relayout()
    // would then dereference it during cascade (PseudoHover
    // walks the parent chain), causing a segfault. Clear it
    // before replacing the document.
    state_.hovered_element = nullptr;

    // ADR-022: same for prev_hovered_ (also a raw pointer into
    // doc_) and the cached styles (keyed by DOM element pointers,
    // stale once the DOM is replaced).
    prev_hovered_ = nullptr;
    styles_cache_.clear();
    sheet_cache_ = renderer::css::Stylesheet{};
    hover_level_ = renderer::css::HoverLevel::None;
    hover_sensitive_.clear();

    doc_ = std::move(doc);
    merged_css_ = std::move(merged_css);

    if (!doc_) {
        layout_root_ = renderer::layout::LayoutBox{};
        return;
    }
    relayout(viewport_width);
}

void RenderPipeline::relayout(float viewport_width) {
    if (!doc_) return;

    // ADR-029: viewport-driven feature class flips MUST run
    // before the cascade so selector matching sees the updated
    // <html> class. Idempotent round-trip across resizes.
    apply_viewport_feature_classes(*doc_, viewport_width);

    // ADR-025: sheet_cache_ parses merged_css_ (inline + external).
    // ADR-022: cache the parsed author sheet and the full cascade
    // result. Subsequent restyle() calls mutate styles_cache_ in
    // place rather than re-cascading from scratch.
    // ADR-029 follow-up: hide the JS/`:checked`-controlled language
    // dropdown at desktop viewports. `#p-lang-btn` (140 <li> items)
    // is a `position: absolute` / `:checked` dropdown in real
    // Wikipedia; without those features (ADR-030 territory) its
    // children render as visible blocks in the flow, consuming
    // ~4766px and pushing the article body to y≈5560. Real
    // Wikipedia shows the dropdown only on hover/click, so hiding
    // it at desktop matches the visible behavior. Preserves the
    // article-above-fold claim by removing an unrelated 4766px
    // obstruction. Principled fix is ADR-030.
    std::string viewport_css;
    if (viewport_width >= 1120.0f) {
        // ADR-029 workaround: language dropdown (140 items) is
        // :checked-controlled in real Wikipedia; without :checked
        // support (ADR-031), hide it. Will be removed when ADR-031
        // lands.
        // ADR-030: skip link uses .mw-jump-link:not(:focus) +
        // position:absolute + clip in real Wikipedia; we support
        // none of those. !important beats the :not rule (which
        // we can't match anyway). Will be removed when ADR-031
        // lands with :not + position + clip + future :focus
        // support.
        viewport_css =
            "#p-lang-btn{display:none}"
            ".mw-jump-link{display:none !important}"
            // ADR-031 workaround: .sidebar (the HTML article's nav
            // template) renders as a 1517px flat list inside the
            // narrow zone. It pushes the intro below the fold and
            // grows total layout height by ~9000px. Real Wikipedia
            // renders it as a compact nav using display:grid
            // internals that we do not support. Hiding it lets the
            // infobox float correctly AND the intro take the narrow
            // zone beside it. Will be removed when ADR-032 lands
            // proper nested layout (grid/flex internals for
            // .sidebar's children).
            ".sidebar{display:none !important}";
    }
    sheet_cache_ = renderer::css::Stylesheet::parse(
        merged_css_ + viewport_css);
    last_viewport_width_ = viewport_width;
    HB_LOG(Pipeline, Info, "relayout: viewport=%.0f", viewport_width);
    // ADR-042: cascade_with_pseudo to also populate pseudo_styles.
    {
        auto result = renderer::css::cascade_with_pseudo(
            renderer::css::ua_stylesheet(), sheet_cache_, *doc_, state_,
            viewport_width);
        styles_cache_ = std::move(result.element_styles);
        pseudo_styles_cache_ = std::move(result.pseudo_styles);
    }
    HB_LOG(Pipeline, Debug, "cascade: %zu elements styled, %zu with pseudo",
           styles_cache_.size(), pseudo_styles_cache_.size());

    // ADR-035: thread an image-dimension lookup so <img>
    // elements reserve layout space. Cache hit = zero cost.
    // First render = 8 sync fetches (5.0 -> 5.4-5.8 sec).
    auto image_dims = [this](const std::string& url)
            -> renderer::layout::ImageDim {
        cairo_surface_t* s = get_image_surface(url);
        if (!s) return {};
        const int iw = cairo_image_surface_get_width(s);
        const int ih = cairo_image_surface_get_height(s);
        if (iw <= 0 || ih <= 0) return {};
        return {
            static_cast<float>(iw),
            static_cast<float>(ih),
            true
        };
    };
    // ADR-042: layout_with_pseudo to synthesize ::before / ::after.
    // ADR-051: viewport_height passed as 0. GTK allocated height
    // isn't in scope here; vh/vw resolve to 0 in production. Same
    // "unknown viewport -> 0" semantics as the pre-ADR-051
    // unparsed state. Correctness for calc(100vh - ...) requires
    // future GTK height threading (separate concern).
    layout_root_ = renderer::layout::layout_with_pseudo(
        *doc_, styles_cache_, pseudo_styles_cache_,
        &measure_with_pango, viewport_width, image_dims, 0.0f);
    HB_LOG(Pipeline, Debug, "layout: root h=%.0f",
           layout_root_.height);

    // ADR-023: detect hover level and precompute hover-sensitive
    // elements for Level 1. Costs ~0.1ms per render on Wikipedia
    // (~7,871 elements x 1 anchor-hover rule); pays for itself on
    // the first wrapper-element hover after this render.
    hover_level_ = renderer::css::detect_level(
        renderer::css::ua_stylesheet(), sheet_cache_);
    if (hover_level_ == renderer::css::HoverLevel::AnchorOnly) {
        hover_sensitive_ = renderer::css::compute_hover_sensitive(
            *doc_, renderer::css::ua_stylesheet(), sheet_cache_, state_);
    } else {
        hover_sensitive_.clear();
    }

    // The cache now reflects state_.hovered_element (relayout used
    // the current state_). Sync prev_hovered_ so the next restyle's
    // diff correctly identifies what changed.
    prev_hovered_ = state_.hovered_element;
}

RenderPipeline::~RenderPipeline() {
    for (auto& [url, surf] : image_cache_) {
        if (surf) cairo_surface_destroy(surf);
    }
    image_cache_.clear();
}

namespace {

// ADR-034: percent-decode a data: URL payload.
std::string percent_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            try {
                int v = std::stoi(std::string(s.substr(i+1, 2)), nullptr, 16);
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            } catch (...) {}
        }
        out.push_back(s[i]);
    }
    return out;
}

// ADR-034: decode a data: URL into raw bytes.
std::string decode_data_url(const std::string& url) {
    auto comma = url.find(',');
    if (comma == std::string::npos) return {};
    std::string meta = url.substr(5, comma - 5);
    std::string payload = url.substr(comma + 1);
    if (meta.find(";base64") != std::string::npos) {
        static const std::string table =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(payload.size() * 3 / 4);
        int buf = 0, bits = 0;
        for (char c : payload) {
            if (c == '=') break;
            auto pos = table.find(c);
            if (pos == std::string::npos) continue;
            buf = (buf << 6) | static_cast<int>(pos);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<char>((buf >> bits) & 0xFF));
            }
        }
        return out;
    }
    return percent_decode(payload);
}

}  // namespace

cairo_surface_t* RenderPipeline::get_image_surface(
    const std::string& url) const {
    auto it = image_cache_.find(url);
    if (it != image_cache_.end()) return it->second;

    const std::string resolved =
        url.rfind("data:", 0) == 0
            ? url
            : resolve_url(url, state_.base_url);

    std::string bytes;
    if (resolved.rfind("data:", 0) == 0) {
        bytes = decode_data_url(resolved);
    } else {
        auto resp = net::fetch(resolved);
        if (!resp.success()) return nullptr;
        bytes = std::move(resp.body);
    }
    if (bytes.empty()) return nullptr;

    cairo_surface_t* surf = nullptr;

    // PNG magic: 89 50 4E 47
    if (bytes.size() >= 8 &&
        static_cast<unsigned char>(bytes[0]) == 0x89 &&
        bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        struct ReadCtx { const std::string* s; size_t pos; };
        ReadCtx ctx{&bytes, 0};
        auto cb = [](void* closure, unsigned char* data,
                     unsigned int len) -> cairo_status_t {
            auto* c = static_cast<ReadCtx*>(closure);
            const size_t remain = c->s->size() - c->pos;
            if (remain == 0) return CAIRO_STATUS_READ_ERROR;
            const size_t n = len < remain ? len : remain;
            std::memcpy(data, c->s->data() + c->pos, n);
            c->pos += n;
            return CAIRO_STATUS_SUCCESS;
        };
        surf = cairo_image_surface_create_from_png_stream(cb, &ctx);
        if (surf && cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(surf);
            surf = nullptr;
        }
    }
    // SVG
    else if (bytes.find("<svg") != std::string::npos ||
             bytes.rfind("<?xml", 0) == 0) {
        GError* err = nullptr;
        RsvgHandle* h = rsvg_handle_new_from_data(
            reinterpret_cast<const guint8*>(bytes.data()),
            static_cast<gsize>(bytes.size()), &err);
        if (h && !err) {
            // ADR-034: Wikipedia's wordmark/tagline SVGs declare
            // width="1" height="1" (or use CSS 100%), so their
            // intrinsic size is 1x1. The correct design dimensions
            // are in viewBox -- parse it from raw bytes. Fall back
            // to intrinsic size, then 256x256.
            double iw = 0.0, ih = 0.0;
            {
                auto vb_pos = bytes.find("viewBox");
                if (vb_pos != std::string::npos) {
                    auto eq = bytes.find('=', vb_pos);
                    auto q1 = (eq != std::string::npos)
                        ? bytes.find_first_of("\"'", eq) : std::string::npos;
                    auto q2 = (q1 != std::string::npos)
                        ? bytes.find_first_of("\"'", q1 + 1) : std::string::npos;
                    if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                        std::string vb = bytes.substr(q1 + 1, q2 - q1 - 1);
                        double vx = 0, vy = 0;
                        if (std::sscanf(vb.c_str(), "%lf %lf %lf %lf",
                                        &vx, &vy, &iw, &ih) == 4) {
                            if (iw <= 0.0 || ih <= 0.0) { iw = 0; ih = 0; }
                        }
                    }
                }
            }
            if (iw <= 0.0 || ih <= 0.0) {
                rsvg_handle_get_intrinsic_size_in_pixels(h, &iw, &ih);
            }
            if (iw <= 0.0 || ih <= 0.0) { iw = 256.0; ih = 256.0; }
            // Render at 4x for sharpness when scaled by paint_image.
            int sw = static_cast<int>(iw * 4);
            int sh = static_cast<int>(ih * 4);
            if (sw < 1) sw = 1; if (sw > 4096) sw = 4096;
            if (sh < 1) sh = 1; if (sh > 4096) sh = 4096;
            cairo_surface_t* tmp = cairo_image_surface_create(
                CAIRO_FORMAT_ARGB32, sw, sh);
            cairo_t* tcr = cairo_create(tmp);
            RsvgRectangle viewport = {0, 0,
                static_cast<double>(sw), static_cast<double>(sh)};
            gboolean ok = rsvg_handle_render_document(h, tcr, &viewport, &err);
            cairo_destroy(tcr);
            if (ok && !err) surf = tmp;
            else cairo_surface_destroy(tmp);
        }
        if (h) g_object_unref(h);
        if (err) g_error_free(err);
    }

    image_cache_[url] = surf;
    return surf;
}

void RenderPipeline::paint(cairo_t* cr) const {
    renderer::paint::ImageProvider images;
    images.get_surface = [this](const std::string& url) {
        return get_image_surface(url);
    };
    images.base_url = state_.base_url;
    renderer::paint::paint(
        cr, layout_root_, &draw_line_with_pango, &images);
}

std::string RenderPipeline::title() const {
    return doc_ ? doc_->title() : std::string{};
}

float RenderPipeline::layout_height() const {
    return layout_root_.height;
}

void RenderPipeline::restyle() {
    if (!doc_) return;

    // Fallback path: cache empty (never rendered, or cleared by a
    // failed render()). Full cascade, same cost as ADR-021.
    if (styles_cache_.empty()) {
        HB_LOG(Pipeline, Info, "restyle: cache empty, full cascade");
        sheet_cache_ = renderer::css::Stylesheet::parse(merged_css_);
        // ADR-042: cascade_with_pseudo for pseudo_styles.
        {
            auto result = renderer::css::cascade_with_pseudo(
                renderer::css::ua_stylesheet(), sheet_cache_, *doc_,
                state_, last_viewport_width_);
            styles_cache_ = std::move(result.element_styles);
            pseudo_styles_cache_ = std::move(result.pseudo_styles);
        }
        restyle_walk(layout_root_, styles_cache_);
        prev_hovered_ = state_.hovered_element;
        return;
    }

    // ADR-023: Level 0 -- no :hover rules at all. Nothing to do;
    // hover state can't change any style. Bonus speedup for pages
    // with no :hover CSS at all.
    if (hover_level_ == renderer::css::HoverLevel::None) {
        HB_LOG(Pipeline, Debug, "restyle: no :hover rules, skipping");
        prev_hovered_ = state_.hovered_element;
        return;
    }

    // ADR-022/ADR-023: dispatch on hover level.
    //   Level 2 (Compound): unconditional cascade_subtree on each
    //     dirty element (ADR-022 behavior, unchanged).
    //   Level 1 (AnchorOnly): filtered walk -- cascade only
    //     elements flagged in hover_sensitive_ within each dirty
    //     element's subtree. Skips the thousands of non-hover-
    //     sensitive descendants (e.g. <span>s, <p>s) that don't
    //     change style when a wrapper is hovered.
    //
    // Both paths: erase first, then cascade -- erased entries have
    // no inherited properties, so the subsequent full
    // propagate_inheritance refills them from the (possibly-changed)
    // parent's style.
    //
    // Known limitation (documented in ADR-022, unchanged): ancestor
    // :hover rules are not handled by the targeted paths. Current
    // stylesheets don't trigger this.
    bool did_work = false;

    if (hover_level_ == renderer::css::HoverLevel::Compound) {
        // ADR-022 behavior: unconditional cascade on dirty subtrees.
        if (prev_hovered_ && prev_hovered_ != state_.hovered_element) {
            renderer::css::erase_subtree(*prev_hovered_, styles_cache_);
            renderer::css::cascade_subtree(
                *prev_hovered_, renderer::css::ua_stylesheet(),
                sheet_cache_, state_, last_viewport_width_,
                styles_cache_);
            did_work = true;
        }
        if (state_.hovered_element) {
            renderer::css::erase_subtree(
                *state_.hovered_element, styles_cache_);
            renderer::css::cascade_subtree(
                *state_.hovered_element, renderer::css::ua_stylesheet(),
                sheet_cache_, state_, last_viewport_width_,
                styles_cache_);
            did_work = true;
        }
    } else {
        // ADR-023 Level 1: filtered walk. Collect hover-sensitive
        // elements in each dirty element's subtree into a dedup'd
        // set, then cascade each once. Non-flagged descendants
        // (the vast majority on real pages) are skipped.
        std::unordered_set<const renderer::dom::Element*> dirty;
        auto collect_flagged = [&](const renderer::dom::Element* root) {
            if (!root) return;
            std::function<void(const renderer::dom::Element&)> walk =
                [&](const renderer::dom::Element& el) {
                    if (hover_sensitive_.count(&el)) {
                        dirty.insert(&el);
                    }
                    for (const auto& child : el.children()) {
                        if (child->type() == renderer::dom::NodeType::Element) {
                            walk(static_cast<const renderer::dom::Element&>(
                                *child));
                        }
                    }
                };
            walk(*root);
        };
        if (prev_hovered_ && prev_hovered_ != state_.hovered_element) {
            collect_flagged(prev_hovered_);
        }
        if (state_.hovered_element) {
            collect_flagged(state_.hovered_element);
        }
        // ADR-024: transition check. Cascade only elements whose
        // :hover match state actually changed between old and new
        // hovered element. ADR-023's measurement showed wrapper
        // hover flags ~2,191 <a> descendants but transitions zero
        // (per ADR-018's matcher, none of them match a:hover when
        // a wrapper is hovered). Without this check, all ~2,191
        // get re-cascaded for nothing.
        //
        // anchor_hover and old_state are constructed once per
        // transition (not per element): sheet_cache_ doesn't
        // change during restyle, and old_state differs from state_
        // only in hovered_element, so one copy suffices.
        const auto anchor_hover =
            renderer::css::collect_anchor_hover_selectors(
                renderer::css::ua_stylesheet(), sheet_cache_);
        renderer::css::RenderState old_state = state_;
        old_state.hovered_element = prev_hovered_;

        for (const auto* el : dirty) {
            const bool old_match =
                renderer::css::matches_any_anchor_hover(
                    *el, old_state, anchor_hover);
            const bool new_match =
                renderer::css::matches_any_anchor_hover(
                    *el, state_, anchor_hover);
            if (old_match == new_match) continue;
            renderer::css::erase_subtree(*el, styles_cache_);
            renderer::css::cascade_subtree(
                *el, renderer::css::ua_stylesheet(),
                sheet_cache_, state_, last_viewport_width_,
                styles_cache_);
            did_work = true;
        }
    }

    if (!did_work) {
        // Neither side changed (both null, or same pointer).
        // Nothing to re-cascade.
        return;
    }

    // Full propagate: refill inherited properties for erased entries
    // from their (possibly-changed) parents.
    renderer::css::propagate_inheritance(*doc_, styles_cache_);

    // Full walk: update paint-only colors on all fragments. ~5ms on
    // Wikipedia; targetable in a future ADR.
    restyle_walk(layout_root_, styles_cache_);

    prev_hovered_ = state_.hovered_element;
}

void RenderPipeline::record_visit(const std::string& absolute_url) {
    state_.base_url = absolute_url;
    state_.visited_urls.insert(absolute_url);
}

void RenderPipeline::set_hovered_element(
    const renderer::dom::Element* el) {
    state_.hovered_element = el;
}

const renderer::dom::Element* RenderPipeline::hit_test(
    float x, float y) const {
    return renderer::layout::hit_test(layout_root_, x, y);
}

} // namespace honeybee::browser
