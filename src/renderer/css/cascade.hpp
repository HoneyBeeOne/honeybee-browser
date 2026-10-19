// =============================================================
// File: cascade.hpp
// Purpose: CSS cascade (per ADR-010).
//
// Commit 1 of the cascade work: matching + specificity ordering.
// Commit 2 adds inheritance + typed getters.
//
// Given a parsed Stylesheet and an owned DOM Document, produce a
// per-element ComputedStyle by applying matching rules in
// (important, specificity, source_order) order.
// =============================================================

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "renderer/css/selectors.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/dom/node.hpp"

namespace honeybee::renderer::css {

// One winning declaration for a property, with the coordinate
// that determined the win (useful for debugging and for future
// invalidation logic).
struct CascadeValue {
    std::string value;
    bool important = false;
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

// Computes the cascade for every element in the document.
// Commit 1: matching + specificity ordering only.
// Commit 2 adds inheritance for the seven inherited properties.
ComputedStyleMap cascade(const Stylesheet& sheet, const dom::Document& doc);

} // namespace honeybee::renderer::css
