// =============================================================
// File: cascade.hpp
// Purpose: CSS cascade (per ADR-010, extended per ADR-014).
//
// Per-element ComputedStyle from matching rules in
// (important, origin, specificity, source_order) order, with
// inheritance for the seven properties declared inherited.
// =============================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "renderer/css/selectors.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/dom/node.hpp"

namespace honeybee::renderer::css {

// Forward declaration from render_state.hpp (ADR-018).
struct RenderState;

// Cascade origin per CSS 2.1 section 6.4.1. For normal
// declarations, Author wins over UserAgent regardless of
// specificity. For important declarations, the order inverts
// (UserAgent wins), but v1 has no UA-important rules, so the
// important-origin case is not exercised; a future ADR that
// adds UA !important rules will extend beats() accordingly.
enum class Origin : std::uint8_t { UserAgent, Author };

struct CascadeValue {
    std::string value;
    bool important = false;
    Origin origin = Origin::Author;
    Specificity specificity;
    std::size_t source_order = 0;
};

struct ComputedStyle {
    std::unordered_map<std::string, CascadeValue> declarations;

    bool has(const std::string& property) const;
    std::optional<std::string> get(const std::string& property) const;
};

using ComputedStyleMap =
    std::unordered_map<const dom::Element*, ComputedStyle>;

// ADR-041: per-element pseudo-element ComputedStyles.
struct PseudoStyles {
    ComputedStyle before;
    ComputedStyle after;
};

using PseudoStyleMap =
    std::unordered_map<const dom::Element*, PseudoStyles>;

// ADR-041: full cascade output including pseudo-element styles.
struct CascadeResult {
    ComputedStyleMap element_styles;
    PseudoStyleMap pseudo_styles;
};

bool is_inherited(std::string_view property);

// ADR-041: cascade with pseudo-element synthesis. Also computes
// ::before and ::after ComputedStyles for every element.
CascadeResult cascade_with_pseudo(const Stylesheet& ua_sheet,
                                  const Stylesheet& author_sheet,
                                  const dom::Document& doc,
                                  float viewport_width = 1024.0f);

CascadeResult cascade_with_pseudo(const Stylesheet& ua_sheet,
                                  const Stylesheet& author_sheet,
                                  const dom::Document& doc,
                                  const RenderState& state,
                                  float viewport_width = 1024.0f);

// Two-sheet form (ADR-014): apply ua_sheet first, then
// author_sheet. Author normal declarations override UA normal
// declarations regardless of specificity (CSS 2.1 section
// 6.4.1).
ComputedStyleMap cascade(const Stylesheet& ua_sheet,
                         const Stylesheet& author_sheet,
                         const dom::Document& doc,
                         float viewport_width = 1024.0f);

// ADR-018: two-sheet form with presentation state for
// :link / :visited / :hover matching. Commit 1 delegates to the
// state-free version above.
ComputedStyleMap cascade(const Stylesheet& ua_sheet,
                         const Stylesheet& author_sheet,
                         const dom::Document& doc,
                         const RenderState& state,
                         float viewport_width = 1024.0f);

// Single-sheet form: equivalent to the two-sheet form with an
// empty UA stylesheet. Kept so existing tests and any future
// headless entry point that doesn't want UA defaults compile
// unchanged.
ComputedStyleMap cascade(const Stylesheet& author_sheet,
                         const dom::Document& doc);

// ADR-022: targeted cascade primitives.

// Re-run compute_styles scoped to `el`'s subtree, writing explicit
// (non-inherited) styles into `out`. Existing entries in `out` for
// elements in the subtree are overwritten. Inherited properties are
// NOT filled -- call propagate_inheritance() after to complete them.
void cascade_subtree(const dom::Element& el,
                     const Stylesheet& ua_sheet,
                     const Stylesheet& author_sheet,
                     const RenderState& state,
                     float viewport_width,
                     ComputedStyleMap& out);

// Remove `el` and all its element descendants from `out`. Used
// before cascade_subtree so stale inherited properties from a prior
// restyle don't linger (propagate_inheritance only fills MISSING
// properties, so without erasure it would skip descendants whose
// entries survived from the previous cascade).
void erase_subtree(const dom::Element& el, ComputedStyleMap& out);

// Propagate inherited properties from parents to children across
// `doc`, filling in missing declarations in `out`. Previously an
// internal helper; exposed for ADR-022's targeted restyle.
void propagate_inheritance(const dom::Node& doc,
                           ComputedStyleMap& out);

// ADR-022 test-only: number of apply_rule() invocations since the
// last reset. Used by algorithmic regression guards to assert that
// targeted restyle re-cascades O(subtree * rules) elements rather
// than O(elements * rules). Unconditional (thread_local increment
// per apply_rule call; ~0.02% overhead on a full Wikipedia cascade).
std::size_t apply_rule_call_count();
void reset_apply_rule_call_count();

} // namespace honeybee::renderer::css
