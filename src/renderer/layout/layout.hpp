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
#include <functional>

#include "renderer/css/cascade.hpp"
#include "renderer/dom/node.hpp"
#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::layout {

// ADR-035: image dimension lookup. Called during layout for
// each <img> element to get intrinsic dimensions. Returned dims
// reserve layout space (aspect-derive from either axis, or use
// intrinsic if neither CSS axis is set). nullptr default
// preserves pre-ADR-035 behavior (all <img> at h=0).
struct ImageDim {
    float width = 0.0f;
    float height = 0.0f;
    bool valid = false;
};

using ImageDimLookup = std::function<ImageDim(const std::string&)>;

// Lays out `doc` into a LayoutBox tree.
// `viewport_width` is the DrawingArea's width in pixels.
// The returned LayoutBox has element == nullptr (it is the
// synthetic root that wraps the <html> element's box).
LayoutBox layout(const dom::Document& doc,
                 const css::ComputedStyleMap& styles,
                 TextMeasurer measurer,
                 float viewport_width,
                 ImageDimLookup image_dims = nullptr);

// ADR-042: layout with pseudo-element synthesis. Runs the normal
// layout() tree, then walks it to synthesize ::before / ::after
// boxes from pseudo_styles. Only `content: ''` (empty literal)
// supported in v1; literal text and attr()/counter()/url()
// deferred. Pseudo boxes with position:absolute are positioned
// against their owning element's content box (same rules as
// ADR-042 Commit 1's block-parent absolutes).
LayoutBox layout_with_pseudo(const dom::Document& doc,
                             const css::ComputedStyleMap& styles,
                             const css::PseudoStyleMap& pseudo_styles,
                             TextMeasurer measurer,
                             float viewport_width,
                             ImageDimLookup image_dims = nullptr);

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
