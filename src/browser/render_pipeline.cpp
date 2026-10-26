#include "render_pipeline.hpp"

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

    std::string cls = html_el->get_attribute("class");
    const std::string enabled  =
        "vector-feature-main-menu-pinned-enabled";
    const std::string disabled =
        "vector-feature-main-menu-pinned-disabled";

    auto flip = [&](const std::string& from,
                    const std::string& to) {
        const auto pos = cls.find(from);
        if (pos == std::string::npos) return;
        cls.replace(pos, from.size(), to);
        html_el->set_attribute("class", cls);
    };

    if (viewport_width >= 1120.0f) {
        flip(disabled, enabled);
    } else {
        flip(enabled, disabled);
    }
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
        viewport_css = "#p-lang-btn{display:none}";
    }
    sheet_cache_ = renderer::css::Stylesheet::parse(
        merged_css_ + viewport_css);
    last_viewport_width_ = viewport_width;
    styles_cache_ = renderer::css::cascade(
        renderer::css::ua_stylesheet(), sheet_cache_, *doc_, state_,
        viewport_width);

    layout_root_ = renderer::layout::layout(
        *doc_, styles_cache_, &measure_with_pango, viewport_width);

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

void RenderPipeline::paint(cairo_t* cr) const {
    renderer::paint::paint(cr, layout_root_, &draw_line_with_pango);
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
        sheet_cache_ = renderer::css::Stylesheet::parse(merged_css_);
        styles_cache_ = renderer::css::cascade(
            renderer::css::ua_stylesheet(), sheet_cache_, *doc_, state_,
            last_viewport_width_);
        restyle_walk(layout_root_, styles_cache_);
        prev_hovered_ = state_.hovered_element;
        return;
    }

    // ADR-023: Level 0 -- no :hover rules at all. Nothing to do;
    // hover state can't change any style. Bonus speedup for pages
    // with no :hover CSS at all.
    if (hover_level_ == renderer::css::HoverLevel::None) {
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
