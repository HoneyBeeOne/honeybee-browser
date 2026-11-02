// =============================================================
// File: stylesheet.hpp
// Purpose: CSS stylesheet parsing (per ADR-009).
//
// Commit 1 of the CSS parser work: rule boundary detection,
// delegation to SelectorList::parse (ADR-008), comment
// stripping, and error recovery. Declarations are empty in this
// commit; Commit 2 adds declaration parsing.
//
// Public entry point:
//   Stylesheet::parse(std::string_view) -> Stylesheet
// Never fails. Malformed rules and declarations are silently
// skipped (CSS 2.1 error handling).
// =============================================================

#pragma once

#include <string>
#include <optional>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "renderer/css/selectors.hpp"

namespace honeybee::renderer::css {

struct Declaration {
    std::string property;
    std::string value;
    bool important = false;
};

// ADR-027: @media query conditions supported by the parser.
// Unsupported queries (print, prefers-*, complex syntax) cause
// their rules to be dropped entirely -- they never enter
// result.rules. Therefore a Rule with media.has_value() always
// refers to a SUPPORTED query whose width constraints (if any)
// are populated below.
struct MediaQuery {
    // Minimum viewport width in CSS pixels (from min-width
    // or an "and" combination). Empty = no lower bound.
    std::optional<float> min_width_px;
    // Maximum viewport width in CSS pixels (from max-width
    // or an "and" combination). Empty = no upper bound.
    std::optional<float> max_width_px;
};

struct Rule {
    SelectorList selectors;
    std::vector<Declaration> declarations;

    // ADR-027: nullopt for top-level (unconditional) rules;
    // populated for rules extracted from a supported @media
    // block. The cascade skips rules whose query doesn't match
    // the current viewport.
    std::optional<MediaQuery> media;
};

// ADR-059: rightmost-simple anchor index for cascade queries.
// Buckets store rule indices (size_t), not Rule pointers, so
// natural sort order preserves original rule order -- critical
// for specificity tie-breaking (source_order is derived from
// iteration order). DOM-free interface: callers extract id,
// classes, and tag from the element themselves.
struct RuleIndex {
    std::unordered_map<std::string, std::vector<std::size_t>> by_id;
    std::unordered_map<std::string, std::vector<std::size_t>> by_class;
    std::unordered_map<std::string, std::vector<std::size_t>> by_type;
    std::vector<std::size_t> universal;

    // Returns candidate rule indices for an element with the
    // given id, classes, and tag. Result is sorted and deduped.
    std::vector<std::size_t> candidates_for(
        const std::string& id,
        const std::vector<std::string>& classes,
        const std::string& tag) const;
};

struct Stylesheet {
    std::vector<Rule> rules;

    // ADR-026: CSS custom properties defined in :root rules
    // (e.g. `:root { --color-base: #202122 }`). Populated during
    // parse(). Consumed by var() pre-resolution.
    std::unordered_map<std::string, std::string> variables;

    // ADR-059: built by build_index() at end of parse().
    // Invariant: `rules` must not be mutated post-parse or the
    // index holds stale indices.
    RuleIndex index;

    // Parses `text` into a stylesheet. Malformed rules are
    // skipped; the result is always a valid (possibly empty)
    // Stylesheet.
    static Stylesheet parse(std::string_view text);

    // ADR-059: builds `index` from `rules`. Idempotent
    // (clears + rebuilds). Called at end of parse(); exposed
    // for testability.
    void build_index();
};

} // namespace honeybee::renderer::css
