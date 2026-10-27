// =============================================================
// File: paint.hpp
// Purpose: Paint stage (ADR-011, ADR-015, ADR-016).
//
// Walks a LayoutNode tree in document order. For LayoutText and
// LayoutInlineContext nodes, iterates line fragments and invokes
// the LineDrawer once per fragment. Fragments carry their own
// style (font, size, color) so paint has no need for parent
// LayoutText context.
// =============================================================

#pragma once

#include <cairo.h>

#include <functional>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::paint {

using LineDrawer = std::function<void(
    cairo_t*,
    const layout::LineFragment&)>;

void paint(cairo_t* cr, const layout::LayoutNode& root,
           LineDrawer drawer);

// ADR-034: image provider. When non-null, paint_box() will
// fetch/decode/draw <img> elements and background-image URLs.
// The 3-arg paint() above is a thin wrapper calling this with
// nullptr (no images).
struct ImageProvider {
    // Returns a decoded cairo_surface_t* for the given URL, or
    // nullptr on error (fetch, decode, unsupported format). The
    // surface is OWNED by the provider (typically a cache);
    // paint must NOT destroy it.
    std::function<cairo_surface_t*(const std::string&)> get_surface;
    std::string base_url;
};

void paint(cairo_t* cr, const layout::LayoutNode& root,
           LineDrawer drawer, const ImageProvider* images);

} // namespace honeybee::renderer::paint
