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

// Read the display property from a computed style (ADR-016).
// Returns Block when the property is missing, empty, or
// unrecognized. Takes a nullable pointer to match the pattern
// used by lookup() in layout.cpp.
Display display_of(const css::ComputedStyle* style);

// Resolve the text direction for an element (ADR-016).
// Precedence: dir="ltr" -> Ltr, dir="rtl" -> Rtl,
// dir="auto" -> heuristic on the element's flattened text,
// missing -> parent_dir. Default parent_dir is Direction::Ltr
// at <html>.
Direction resolve_direction(const dom::Element& el, Direction parent_dir);


// ADR-018 Commit 4b: hit-test. Returns the deepest dom::Element
// whose layout box contains (x, y), or nullptr if the point is
// outside the root. "Deepest" = the LayoutInline or LayoutBox
// closest to a leaf; text nodes with no element bubble their
// containing block back up to the caller.
const dom::Element* hit_test(const LayoutNode& root, float x, float y);
} // namespace honeybee::renderer::layout
