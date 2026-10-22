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

bool is_inherited(std::string_view property);

// Two-sheet form (ADR-014): apply ua_sheet first, then
// author_sheet. Author normal declarations override UA normal
// declarations regardless of specificity (CSS 2.1 section
// 6.4.1).
ComputedStyleMap cascade(const Stylesheet& ua_sheet,
                         const Stylesheet& author_sheet,
                         const dom::Document& doc);

// ADR-018: two-sheet form with presentation state for
// :link / :visited / :hover matching. Commit 1 delegates to the
// state-free version above.
ComputedStyleMap cascade(const Stylesheet& ua_sheet,
                         const Stylesheet& author_sheet,
                         const dom::Document& doc,
                         const RenderState& state);

// Single-sheet form: equivalent to the two-sheet form with an
// empty UA stylesheet. Kept so existing tests and any future
// headless entry point that doesn't want UA defaults compile
// unchanged.
ComputedStyleMap cascade(const Stylesheet& author_sheet,
                         const dom::Document& doc);

} // namespace honeybee::renderer::css
