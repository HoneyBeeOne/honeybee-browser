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

struct Rule {
    SelectorList selectors;
    std::vector<Declaration> declarations;
};

struct Stylesheet {
    std::vector<Rule> rules;

    // ADR-026: CSS custom properties defined in :root rules
    // (e.g. `:root { --color-base: #202122 }`). Populated during
    // parse(). Consumed by var() pre-resolution.
    std::unordered_map<std::string, std::string> variables;

    // Parses `text` into a stylesheet. Malformed rules are
    // skipped; the result is always a valid (possibly empty)
    // Stylesheet.
    static Stylesheet parse(std::string_view text);
};

} // namespace honeybee::renderer::css
