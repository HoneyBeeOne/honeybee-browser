# ADR-008: CSS Selector Matching

**Status:** Accepted
**Date:** 2026-10-19
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-001 (technology stack), ADR-006 (return to ADR-001), ADR-007 (DOM ownership)

## Context

We now own our DOM (`renderer::dom`, per ADR-007). It is a fully-owned tree of `Node` subclasses with `Element` (tag + attributes) and `TextNode` (text data). lexbor is confined to `renderer/html/convert.cpp`.

Phase 2 (CSS) begins here. The first problem is selector matching: given a parsed CSS rule like `div.content > p { color: red }` and a node in our DOM, does the node match the selector? If so, with what specificity?

ADR-007 deferred this question explicitly:

> **Recommendation:** implement selectors ourselves. This is deferred to a future ADR for the CSS engine, but noted here for context.

This ADR makes that decision, and bounds the scope of "selectors v1."

## Decision

**We implement selector matching ourselves**, in a new module `renderer/css/`. lexbor's selectors module is not used.

### Scope of v1 (this ADR + implementation)

**Selector syntax supported:**

- Type selectors: `div`, `p`, `h1`
- Universal selector: `*`
- Class selectors: `.foo`, `.bar.baz`
- ID selectors: `#main`, `#x.y`
- Attribute selectors:
  - Presence: `[href]`
  - Exact: `[href="x"]`
  - Prefix: `[href^="x"]`
  - Suffix: `[href$="x"]`
  - Contains: `[href*="x"]`
  - Word: `[class~="x"]`
  - Hyphen: `[lang|="en"]`
- Combinators:
  - Descendant: `A B`
  - Child: `A > B`
  - Adjacent sibling: `A + B`
  - General sibling: `A ~ B`
- Grouping: `A, B, C` (selector lists)
- Compound selectors: combinations of the above without combinators, e.g. `div.foo#bar[data-x="1"]`

**Specificity calculation:**

Per CSS 2.1: a triple `(a, b, c)` where

- `a` = count of ID selectors in the compound
- `b` = count of class, attribute, and pseudo-class selectors
- `c` = count of type selectors and pseudo-elements

Compared lexicographically. Universal selector `*` contributes zero to all three.

**Deferred to future ADRs:**

- Pseudo-classes (`:hover`, `:focus`, `:nth-child()`, `:not()`, `:first-child`, `:empty`, `:root`, `:scope`)
- Pseudo-elements (`::before`, `::after`, `::first-line`)
- Namespaces (`svg|rect`)
- CSS Selectors Level 4 extensions (`:has()`, `:is()`, `:where()`, `:matches()`)

## Rationale

### 1. Coupling

lexbor's selectors module (`lexbor/selectors/`) operates on lexbor's tree (`lxb_dom_node_t`). Using it would require either:

- Keeping lexbor's tree alive alongside our DOM (contradicts ADR-007), or
- Reverse-converting our DOM back to lexbor before each match (defeats the point of ADR-007, and is O(n) per match).

Neither is acceptable. The DOM is the seam; the CSS layer must be built against it.

### 2. Specificity is intrinsic to the selector

The cascade needs the specificity of every matching selector. If we delegate matching to lexbor, we still have to parse the selector ourselves to extract specificity. Once we're parsing, we might as well match — the hard part isn't reading `.foo[data-x]`, it's the combinator traversal, and that runs on our tree.

### 3. The tree-walk is needed elsewhere

Phase 3 (layout) and Phase 4 (paint) both walk the DOM in document order, filtering by computed style. The "given a node, walk ancestors/siblings, decide if a pattern holds" operation is the same one the layout engine will use to resolve `display: inline` vs. `display: block` inheritance chains. Building it in `renderer/css/` once and reusing it in layout is cleaner than two similar walkers.

### 4. Educational value

Selector matching is one of the "interesting parts" the project exists to teach. It's small (a few hundred lines), spec-bounded (CSS 2.1 selectors is well-documented), and testable in isolation. Borrowing it would erase a learning opportunity.

### 5. Performance is acceptable

A linear scan over the DOM for each selector, with short-circuit on non-matching compounds, is exactly what browsers did in the 1990s and what our scale (a few thousand nodes per page) can tolerate. Real browsers now use Bloom filters, style sharing, and invalidation — none of which matter at our size. If profiling shows a need, we can optimize later without changing the interface.

## Alternatives Considered

### Use lexbor's selectors module

**Rejected.** Couples Phase 2 to lexbor. Requires reverse-conversion or keeping two trees. Duplicates lexbor's work on specificity anyway.

### Hybrid: parse with our code, delegate hard cases to lexbor

**Rejected.** Two code paths, two test suites, two sets of edge cases. The "hard cases" (`:nth-child` arithmetic, `:not`) are deferred to v2 anyway, so the hybrid saves nothing today.

### Defer selectors to Phase 3, use only inline styles in Phase 2

**Rejected.** Doesn't advance the project. CSS is the next phase, and selector matching is the first thing CSS needs.

## Consequences

- New module `renderer/css/`:
  - `selectors.hpp` — public API
  - `selectors.cpp` — parser + matcher
- Public API (in `namespace honeybee::renderer::css`):

```cpp
struct Specificity {
    unsigned a, b, c;
    friend bool operator<(const Specificity&, const Specificity&);
};

class Selector {
public:
    static std::optional<Selector> parse(std::string_view text);
    bool matches(const dom::Node&) const;
    Specificity specificity() const;
};

class SelectorList {
public:
    static std::optional<SelectorList> parse(std::string_view text);
    // Returns the specificity of the highest-specificity matching
    // selector in the list, or nullopt if none match.
    std::optional<Specificity> matches(const dom::Node&) const;
};
```

- Tests in `tests/test_selectors.cpp`:
  - One test per supported syntax form
  - One test per combinator
  - Specificity arithmetic, including ties and zero-match
  - Malformed selector returns `nullopt` (e.g. `parse("div >")` returns `nullopt`)
  - Combinator traversal on a multi-level DOM tree

- No new lexbor includes. The isolation invariant from ADR-007 consequence #4 still holds.

- Sibling lookup: `renderer::dom::Node` does not currently expose `previous_sibling()`. The matcher will scan `parent()->children()` to locate a node's index, then inspect `children()[index - 1]`. For pages of our scale (tens to hundreds of nodes per parent), this is fine. If profiling shows a bottleneck, a follow-up ADR can add indexed children or sibling pointers to `Node`.

## Open Questions

### CSS variables and custom properties

Custom properties like `--main-color: red` interact with selectors only through the `var()` function, which is resolved at cascade time, not match time. Deferred to the cascade ADR.

### Selector performance at scale

For our scale, linear scan suffices. Real browsers use:

- Bloom filters on tag names and class names per ancestor
- Style sharing (memoizing matched declarations across identical nodes)
- Invalidation (recomputing only on mutation)

None of these are needed today. If profiling shows a bottleneck, a follow-up ADR will address them.

### `:nth-child()` arithmetic

Formulas like `:nth-child(2n+1)` require arithmetic parsing and index lookup. Deferred to v2 along with other pseudo-classes.

## Record of Changes

- **2026-10-19:** Initial decision.
