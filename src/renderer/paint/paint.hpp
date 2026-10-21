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

} // namespace honeybee::renderer::paint
