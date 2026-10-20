// =============================================================
// File: layout.hpp
// Purpose: The layout algorithm (per ADR-011).
//
// Given an owned DOM and its computed styles, produce a layout
// tree. Block flow only in v1 -- every element stacks
// top-to-bottom inside its parent's content area.
// =============================================================

#pragma once

#include <memory>

#include "renderer/css/cascade.hpp"
#include "renderer/dom/node.hpp"
#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::layout {

// Lays out `doc` into a LayoutBox tree.
// `viewport_width` is the DrawingArea's width in pixels.
// The returned LayoutBox has element == nullptr (it is the
// synthetic root that wraps the <html> element's box).
LayoutBox layout(const dom::Document& doc,
                 const css::ComputedStyleMap& styles,
                 TextMeasurer measurer,
                 float viewport_width);

} // namespace honeybee::renderer::layout
