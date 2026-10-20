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

#include "renderer/dom/node.hpp"
#include "renderer/layout/layout_box.hpp"

namespace honeybee::browser {

class RenderPipeline {
public:
    // Parse + extract styles + cascade + layout at viewport_width.
    // Replaces any previous page.
    void render(const std::string& html, float viewport_width);

    // Recompute layout at a new viewport width, keeping the DOM.
    // Called on window resize.
    void relayout(float viewport_width);

    // Paint the current layout tree via Pango.
    void paint(cairo_t* cr) const;

    // The document's <title>, or empty if no page is loaded.
    std::string title() const;

    // True if a page has been rendered.
    bool has_page() const { return doc_ != nullptr; }

private:
    std::unique_ptr<renderer::dom::Document> doc_;
    renderer::layout::LayoutBox layout_root_;
};

} // namespace honeybee::browser
