// =============================================================
// File: selectors.hpp
// Purpose: CSS selector parsing, specificity, and matching
//          (per ADR-008).
//
// v1 scope per ADR-008: type, universal, class, id, attribute
// selectors; descendant, child, adjacent-sibling, general-sibling
// combinators; selector lists. Pseudo-classes and pseudo-elements
// are deferred.
// =============================================================

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace honeybee::renderer::dom { class Node; }

namespace honeybee::renderer::css {

// Forward declaration from render_state.hpp (ADR-018).
struct RenderState;

// Specificity per CSS 2.1: (a, b, c) = (ids, classes+attrs, types).
// Compared lexicographically. Universal selector contributes 0.
struct Specificity {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;

    friend bool operator<(const Specificity&, const Specificity&);
    friend bool operator==(const Specificity&, const Specificity&) = default;
};

// ADR-041: pseudo-element targets for ::before / ::after.
enum class PseudoElement : std::uint8_t { None, Before, After };

enum class SimpleKind {
    Type,
    Universal,
    Class,
    Id,
    Attribute,
    PseudoLink,     // :link    (ADR-018)
    PseudoVisited,  // :visited (ADR-018)
    PseudoHover,    // :hover   (ADR-018)
    PseudoRoot,     // :root    (ADR-026)
    // ADR-038: structural pseudo-classes
    PseudoFirstChild,
    PseudoLastChild,
    PseudoOnlyChild,
    PseudoEmpty,
    PseudoFirstOfType,
    PseudoLastOfType,
    PseudoOnlyOfType,
    PseudoNthChild,
    PseudoNthLastChild,
    PseudoNthOfType,
    PseudoNthLastOfType,
    // ADR-038 Commit 2: logical pseudo-classes
    PseudoNot,
    PseudoIs,
    PseudoWhere,
    // ADR-038 Commit 3: language pseudo-class
    PseudoLang,
    // ADR-041: pseudo-element markers (::before / ::after)
    PseudoElementBefore,
    PseudoElementAfter,
};

enum class AttributeMatch {
    Presence,
    Exact,
    Prefix,
    Suffix,
    Contains,
    Word,
    Hyphen,
};

struct SimpleSelector {
    SimpleKind kind = SimpleKind::Universal;
    std::string name;
    AttributeMatch attr_match = AttributeMatch::Presence;
    std::string attr_value;
    // ADR-038: An+B arguments for :nth-* pseudos (a=0 for plain integers)
    int nth_a = 0;
    int nth_b = 0;
    // ADR-038 Commit 2: argument list for :not() / :is() / :where().
    // Outer vector = OR alternatives (comma list); inner vector =
    // compound (AND of simples). Empty for all non-logical pseudos.
    std::vector<std::vector<SimpleSelector>> sub_args;
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

    // Right-to-left match: the rightmost compound must match `node`,
    // then walk backwards through parents/siblings per combinator.
    bool matches(const dom::Node& node) const;

    // ADR-041: pseudo-element-aware match. Returns true only
    // if the selector's pseudo-element (extracted from the
    // rightmost compound) matches `target`. Selectors with no
    // pseudo-element only match target=None.
    bool matches(const dom::Node& node, const RenderState& state,
                 PseudoElement target) const;

    // ADR-018: state-aware match. Delegates to the state-free
    // version in Commit 1; Commit 2 will use state for
    // :link / :visited / :hover.
    bool matches(const dom::Node& node, const RenderState& state) const;

    Specificity specificity() const;
};

struct SelectorList {
    std::vector<Selector> selectors;

    static std::optional<SelectorList> parse(std::string_view text);

    // Specificity of the highest-specificity selector in the list
    // that matches `node`, or nullopt if none match.
    std::optional<Specificity> matches(const dom::Node& node) const;

    // ADR-041: pseudo-element-aware match.
    std::optional<Specificity> matches(const dom::Node& node,
                                       const RenderState& state,
                                       PseudoElement target) const;

    // ADR-018: state-aware match. Delegates in Commit 1.
    std::optional<Specificity> matches(const dom::Node& node,
                                       const RenderState& state) const;
};

} // namespace honeybee::renderer::css
