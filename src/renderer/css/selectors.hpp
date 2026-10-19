// =============================================================
// File: selectors.hpp
// Purpose: CSS selector parsing and specificity (per ADR-008).
//
// Commit 1 of the selectors work: types, parser, specificity.
// The matcher (matches()) is added in Commit 2.
//
// v1 scope per ADR-008: type, universal, class, id, attribute;
// descendant, child, adjacent-sibling, general-sibling combinators;
// selector lists. Pseudo-classes and pseudo-elements are deferred.
// =============================================================

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace honeybee::renderer::css {

// Specificity per CSS 2.1: (a, b, c) = (ids, classes+attrs, types).
// Compared lexicographically. Universal selector contributes 0.
struct Specificity {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;

    friend bool operator<(const Specificity&, const Specificity&);
    friend bool operator==(const Specificity&, const Specificity&) = default;
};

enum class SimpleKind {
    Type,
    Universal,
    Class,
    Id,
    Attribute,
};

enum class AttributeMatch {
    Presence,   // [href]
    Exact,      // [href="x"]
    Prefix,     // [href^="x"]
    Suffix,     // [href$="x"]
    Contains,   // [href*="x"]
    Word,       // [class~="x"]
    Hyphen,     // [lang|="en"]
};

struct SimpleSelector {
    SimpleKind kind = SimpleKind::Universal;
    std::string name;
    AttributeMatch attr_match = AttributeMatch::Presence;
    std::string attr_value;
};

struct CompoundSelector {
    std::vector<SimpleSelector> simples;
};

enum class Combinator {
    None,
    Descendant,
    Child,
    Adjacent,
    Sibling,
};

struct Selector {
    struct Part {
        Combinator combinator = Combinator::None;
        CompoundSelector compound;
    };
    std::vector<Part> parts;

    static std::optional<Selector> parse(std::string_view text);
    Specificity specificity() const;
};

struct SelectorList {
    std::vector<Selector> selectors;
    static std::optional<SelectorList> parse(std::string_view text);
};

} // namespace honeybee::renderer::css
