// =============================================================
// File: paint.hpp
// Purpose: Paint stage of the rendering pipeline (per ADR-011).
//
// Walks a LayoutNode tree in document order and emits Cairo
// draw calls: box backgrounds, borders, and text via an
// injected TextDrawer callback.
//
// Raw cairo_t* is used instead of cairomm's Cairo::RefPtr:
// keeps paint.cpp on the C API and lets GTK-facing code pass
// refptr->cobj() when wiring. ADR-011 will be updated to
// reflect this in the implementation-note commit.
// =============================================================

#pragma once

#include <cairo.h>

#include <functional>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::paint {

// Draws a LayoutText's content at its (x, y) using its font
// and color. The production drawer (window.cpp) uses Pango
// for correct Arabic shaping; tests use a fake that paints a
// colored rectangle at the text position.
using TextDrawer =
    std::function<void(cairo_t*, const layout::LayoutText&)>;

// Draws `root` on `cr`. Children are drawn in document order.
// Transparent colors (alpha == 0) draw nothing. Zero-size
// boxes draw nothing. Null `cr` is a no-op.
void paint(cairo_t* cr, const layout::LayoutNode& root,
           TextDrawer drawer);

} // namespace honeybee::renderer::paint
