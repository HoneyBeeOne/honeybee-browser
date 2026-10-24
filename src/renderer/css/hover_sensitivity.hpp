#pragma once

// =============================================================
// File: hover_sensitivity.hpp
// Purpose: Hover-sensitivity detection and precompute (ADR-023).
//
// `detect_level` classifies a pair of stylesheets by where
// `:hover` appears in their selectors, choosing the strategy for
// targeted restyle:
//   Level 0 (None):        no `:hover` at all -- skip hover work
//   Level 1 (AnchorOnly):  all `:hover` in anchor compound only
//                          -- precompute hover-sensitive elements
//                          and cascade only those on hover
//   Level 2 (Compound):    `:hover` in non-anchor position -- fall
//                          back to unconditional cascade_subtree
//
// `compute_hover_sensitive` (Level 1 only) returns the set of
// elements that could match a `:hover`-containing selector's
// anchor compound if they were the hovered element. On hover, the
// pipeline walks the dirty subtree and cascades only flagged
// elements, skipping the thousands of descendants that don't
// change style.
// =============================================================

#include <cstdint>
#include <unordered_set>

#include "renderer/css/render_state.hpp"
#include "renderer/css/stylesheet.hpp"

namespace honeybee::renderer::dom {
class Document;
class Element;
} // namespace honeybee::renderer::dom

namespace honeybee::renderer::css {

enum class HoverLevel : std::uint8_t {
    None = 0,       // no :hover rules at all
    AnchorOnly = 1, // :hover only in anchor compound
    Compound = 2,   // :hover in non-anchor position (fallback)
};

// Classify the pair of stylesheets. Returns the maximum level
// required by any selector across both sheets.
HoverLevel detect_level(const Stylesheet& ua,
                        const Stylesheet& author);

// Return elements that would match some anchor-compound `:hover`
// selector if they were the hovered element. Caller must have
// already called `detect_level` and confirmed it returns
// `HoverLevel::AnchorOnly` (undefined behavior on Level 2 -- the
// precompute assumption doesn't hold for compound selectors).
//
// Cost: one pass over `doc` calling `Selector::matches` for each
// anchor-hover selector per element. On Wikipedia (~7,871
// elements, 1 anchor-hover rule in UA), ~0.1ms. The
// `RenderState` copy is done once; `hovered_element` is mutated
// per element inside the walk.
std::unordered_set<const dom::Element*> compute_hover_sensitive(
    const dom::Document& doc,
    const Stylesheet& ua,
    const Stylesheet& author,
    const RenderState& state);

} // namespace honeybee::renderer::css
