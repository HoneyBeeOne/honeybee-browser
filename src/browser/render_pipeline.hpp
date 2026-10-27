// =============================================================
// File: render_pipeline.hpp
// Purpose: Full HTML -> layout tree pipeline, owned by one object.
//
// Encapsulates: html::parse -> extract <style> -> cascade ->
// layout. Keeps the DOM alive (layout boxes reference its
// elements). Paint is a separate call so the window can repaint
// without relayout.
// =============================================================

#pragma once

#include <cairo.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>

#include "renderer/dom/node.hpp"
#include "renderer/layout/layout_box.hpp"
#include "renderer/css/cascade.hpp"
#include "renderer/css/render_state.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/css/hover_sensitivity.hpp"

namespace honeybee::browser {

class RenderPipeline {
public:

    // ADR-034: destructor frees cached decoded image surfaces.
    ~RenderPipeline();
    // Parse + extract inline styles + cascade + layout at
    // viewport_width. Replaces any previous page.
    // ADR-025: render(html, width) is now a thin wrapper that
    // parses the HTML, extracts inline-only CSS via
    // collect_css_sources, and delegates to render_with_css with
    // no external CSS. Behavior-preserving vs. pre-ADR-025.
    void render(const std::string& html, float viewport_width);

    // ADR-025: render with pre-parsed DOM and pre-fetched merged
    // CSS (inline <style> content concatenated with external
    // <link rel=stylesheet> content). Used by the window after
    // fetching external stylesheets. Takes ownership of `doc`.
    void render_with_css(
        std::unique_ptr<renderer::dom::Document> doc,
        std::string merged_css,
        float viewport_width);

    // Recompute layout at a new viewport width, keeping the DOM.
    // Called on window resize.
    void relayout(float viewport_width);

    // Paint the current layout tree via Pango.
    void paint(cairo_t* cr) const;

    // The document's <title>, or empty if no page is loaded.
    std::string title() const;

    // True if a page has been rendered.
    bool has_page() const { return doc_ != nullptr; }

    // ADR-021: current layout tree height in CSS pixels. Used by
    // the window to set the DrawingArea's size request so
    // ScrolledWindow shows a scrollbar when content exceeds the
    // viewport. Returns 0 before the first render.
    float layout_height() const;

    // ADR-018 Commit 4b: hit-test against the current layout tree.
    // Delegates to renderer::layout::hit_test. Returns the deepest
    // dom::Element containing (x, y), or nullptr.
    const renderer::dom::Element* hit_test(float x, float y) const;

    // ADR-018: record a successful navigation. Marks the URL as
    // visited and sets it as the base URL for resolving the
    // relative hrefs of the page about to be rendered. Must be
    // called before render() so :visited matches during cascade.
    void record_visit(const std::string& absolute_url);

    // ADR-018: hover state. The element under the mouse cursor,
    // or nullptr. The caller is responsible for triggering a
    // repaint via queue_draw() when it changes.
    void set_hovered_element(const renderer::dom::Element* el);

    // Read-only access for tests and diagnostics.
    const renderer::css::RenderState& state() const { return state_; }
    const renderer::layout::LayoutBox& layout() const { return layout_root_; }
    const renderer::dom::Document* doc() const { return doc_.get(); }
    const renderer::css::ComputedStyleMap& styles() const {
        return styles_cache_;
    }
    const std::unordered_set<const renderer::dom::Element*>&
    hover_sensitive() const {
        return hover_sensitive_;
    }
    renderer::css::HoverLevel hover_level() const { return hover_level_; }

    // ADR-021: incremental restyle. Re-runs the cascade against
    // the current RenderState, then updates paint-only color
    // fields in the existing layout tree in place. Skips layout
    // entirely -- dimensions (x/y/width/height) are unchanged.
    // Called on hover changes; on a Wikipedia-sized page this is
    // ~10ms vs ~1s for relayout().
    void restyle();

private:

    // ADR-034: cache of decoded image surfaces keyed by URL.
    // Cleared on new page load; persists across relayout()
    // (viewport resize doesn't invalidate decoded images).
    // Mutable: cached lookup from const paint().
    mutable std::unordered_map<std::string, cairo_surface_t*> image_cache_;

    // ADR-034: fetch + decode + cache. Returns a surface owned
    // by image_cache_; nullptr on error. Handles PNG (Cairo),
    // SVG (librsvg), and data: URLs (percent-decoded inline).
    cairo_surface_t* get_image_surface(const std::string& url) const;
    std::unique_ptr<renderer::dom::Document> doc_;
    renderer::layout::LayoutBox layout_root_;
    renderer::css::RenderState state_;

    // ADR-022: targeted cascade state.
    //   styles_cache_ -- last cascade result, kept across restyles
    //                    and mutated in place by the targeted path.
    //   sheet_cache_  -- parsed author sheet, only reparsed when
    //                    doc_ is replaced (render). Avoids ~4ms per
    //                    restyle on Wikipedia's 141-rule inline CSS.
    //   prev_hovered_ -- previously-hovered element, used to diff
    //                    against state_.hovered_element in restyle().
    renderer::css::ComputedStyleMap styles_cache_;
    renderer::css::Stylesheet sheet_cache_;
    const renderer::dom::Element* prev_hovered_ = nullptr;

    // ADR-025: merged inline + external CSS, set by
    // render_with_css and preserved across relayout() calls
    // (window resize) without re-fetching.
    std::string merged_css_;

    // ADR-027: viewport width from the last relayout() call.
    // Used by restyle()'s fallback path (which doesn't receive
    // a viewport param) to re-run cascade with the correct
    // @media query filtering.
    float last_viewport_width_ = 1024.0f;

    // ADR-023: hover-sensitivity state, populated in relayout(),
    // cleared in render(). `hover_level_` selects the restyle
    // strategy (None/AnchorOnly/Compound); `hover_sensitive_` is
    // only populated when `hover_level_ == AnchorOnly`.
    renderer::css::HoverLevel hover_level_ = renderer::css::HoverLevel::None;
    std::unordered_set<const renderer::dom::Element*> hover_sensitive_;
};

} // namespace honeybee::browser
