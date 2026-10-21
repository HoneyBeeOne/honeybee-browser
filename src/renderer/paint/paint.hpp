// =============================================================
// File: paint.hpp
// Purpose: Paint stage (ADR-011, ADR-015).
//
// Walks a LayoutNode tree in document order. For LayoutText,
// iterates line fragments (ADR-015) and invokes the injected
// TextDrawer once per fragment.
//
// Raw cairo_t* keeps paint.cpp on the C API. GTK-facing code
// passes refptr->cobj() at the boundary.
// =============================================================

#pragma once

#include <cairo.h>

#include <functional>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::paint {

// Draws one line fragment of a LayoutText. The LayoutText
// provides font family, size, and color; the fragment provides
// text and position.
using TextDrawer = std::function<void(
    cairo_t*,
    const layout::LineFragment&,
    const layout::LayoutText&)>;

void paint(cairo_t* cr, const layout::LayoutNode& root,
           TextDrawer drawer);

} // namespace honeybee::renderer::paint
