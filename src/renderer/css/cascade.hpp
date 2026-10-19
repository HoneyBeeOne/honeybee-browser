// =============================================================
// File: cascade.hpp
// Purpose: CSS cascade (per ADR-010).
//
// Computes per-element ComputedStyle by applying matching
// rules in (important, specificity, source_order) order, then
// propagating the seven inherited properties down the tree.
// =============================================================

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "renderer/css/selectors.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/dom/node.hpp"

namespace honeybee::renderer::css {

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

// True for the seven properties inherited in v1 per ADR-010:
// color, font-family, font-size, font-style, font-weight,
// line-height, visibility.
bool is_inherited(std::string_view property);

ComputedStyleMap cascade(const Stylesheet& sheet, const dom::Document& doc);

} // namespace honeybee::renderer::css
