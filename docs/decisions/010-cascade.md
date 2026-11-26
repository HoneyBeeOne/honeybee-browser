# ADR-010: The Cascade

**Status:** Accepted
**Date:** 2026-10-19
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-001, ADR-006, ADR-007 (DOM), ADR-008 (selectors), ADR-009 (stylesheet parsing)

## Context

We now have three of the pieces needed for the rendering pipeline:

- **ADR-007**: an owned DOM tree (`renderer::dom::Node`, `Element`, `TextNode`, `Document`)
- **ADR-008**: selector parsing and matching (`renderer::css::Selector`, `SelectorList`, `matches()`, `specificity()`)
- **ADR-009**: stylesheet parsing (`renderer::css::Stylesheet`, `Rule`, `Declaration`)

But these three pieces do not talk to each other yet. Given a `Stylesheet` and a `dom::Document`, we cannot answer the question: **"What is the value of `color` for this specific `<h1>` element?"**

That is the job of the **cascade**: given a stylesheet and a DOM, compute the resolved declarations for each element.

## Decision

We implement the cascade ourselves, in a new module `renderer/css/cascade.{hpp,cpp}`.

### Scope of v1

**Cascade order** (per CSS 2.1 §6.4.1, minus UA and user stylesheets):

1. Author declarations marked `!important` beat normal author declarations.
2. Within the same importance level, higher specificity wins.
3. Within the same specificity, later source order wins.

**Inheritance** for the CSS properties that are actually inheritable in v1:

- `color`
- `font-family`, `font-size`, `font-style`, `font-weight`
- `line-height`
- `visibility`

Other properties do not inherit — if no rule applies, they take the initial value (v1 represents the initial value as "unspecified"; typed initial values come in a follow-up ADR).

**Output model**: a `ComputedStyle` per element, stored in an `unordered_map<const dom::Element*, ComputedStyle>`.

```cpp
struct CascadeValue {
    std::string value;
    bool important = false;
    Specificity specificity;
    size_t source_order = 0;
};

struct ComputedStyle {
    std::unordered_map<std::string, CascadeValue> declarations;

    bool has(const std::string& property) const;
    std::optional<std::string> get(const std::string& property) const;
};

using ComputedStyleMap =
    std::unordered_map<const dom::Element*, ComputedStyle>;
```

**Application algorithm** (per element, per property):

1. Walk all rules in source order.
2. For each rule, check whether any of its selectors matches the element.
3. For each matching rule, for each declaration, record `(important, specificity, source_order, value)` if it beats the current winner.
4. After all rules are processed, run the inheritance pass: for each inherited property not explicitly set on this element, copy from the parent's `ComputedStyle` (if the parent has one).
5. Store the resulting `ComputedStyle` in the map.

Two passes are required because inheritance flows from parent to child — parents must be fully computed before children can inherit from them. A pre-order DFS (document order) handles this.

### Deferred to future ADRs

- UA stylesheet and user stylesheet (v1 uses author stylesheet only)
- `@media` queries and conditional rules
- CSS cascade layers (`@layer`)
- `var()` and custom properties
- Shorthand expansion (`margin: 0 1em` → four longhands)
- Typed computed values (`Length`, `Color`, `Keyword` variants)
- Animation and transitions
- Computed-style invalidation on DOM mutation (v1 computes once, then freezes)

## Rationale

### 1. No reverse-conversion

lexbor's CSS module ships its own cascade engine. Using it would require either keeping lexbor's CSS rules alive alongside our own `Stylesheet`, or reverse-converting our `Stylesheet` to lexbor's CSS tree, running lexbor's cascade, and reverse-converting the result. Both are worse than implementing ~250 lines of cascade ourselves. Same reasoning as ADR-007, ADR-008, and ADR-009.

### 2. Our own declaration model

Phase 3 will need typed values: `Length`, `Color`, `Display`, `FontWeight`. Our `Declaration` currently stores `std::string value`, but the plan is to introduce typed variants in a later ADR. lexbor's declaration structs are flat `(name, value)` pairs — no advantage.

### 3. Educational value

The cascade is the "aha" moment in browser internals — the point where CSS text becomes per-element computed values. It is small (a few hundred lines), testable, and produces downstream behavior that is directly observable in layout and paint. This is exactly what the project exists to teach.

### 4. Inheritance is simple in v1

We only inherit seven properties. That is roughly 30 lines of code. Complexity comes from `var()`, `calc()`, CSS-wide keywords (`inherit`, `initial`, `unset`, `revert`), and shorthand expansion — all deferred.

## Alternatives Considered

### Use lexbor's cascade

**Rejected.** Same coupling issue as ADR-007/008/009. Requires reverse-conversion or keeping lexbor's tree alive.

### Apply matching rules only, no inheritance

**Rejected.** Inheritance is fundamental to CSS. Without it, `body { color: navy }` would not affect `<p>Hello</p>` inside the body — visibly wrong behavior for real pages.

### Compute on demand, per element

**Rejected for v1.** Would require walking the stylesheet and DOM tree per query. Computing once per element up front is `O(rules × elements × properties)`, which is fine for our scale. On-demand recomputation for DOM mutation is a Phase 3 concern (deferred to a follow-up ADR).

### Support `!important` only, skip specificity

**Rejected.** Specificity is the whole point of the cascade. Without it, every declaration fights with every other declaration by source order alone — that is not CSS.

## Consequences

- New files: `src/renderer/css/cascade.hpp`, `cascade.cpp`, `tests/test_cascade.cpp`
- Public API in `renderer::css`:
  - `ComputedStyle`, `CascadeValue`, `ComputedStyleMap` types
  - `ComputedStyleMap cascade(const Stylesheet&, const dom::Document&)`
- No new lexbor includes (isolation invariant from ADR-007 holds)
- No changes to `renderer::dom` or `renderer::css::SelectorList`
- `src/CMakeLists.txt` and `tests/CMakeLists.txt` updated

### Behavior examples

| Stylesheet | DOM | Expected |
|---|---|---|
| `p { color: red }` | `<p>Hi</p>` | p: `color == "red"` |
| `p { color: red } p { color: blue }` | `<p>Hi</p>` | p: `color == "blue"` (later wins) |
| `p { color: red !important } p { color: blue }` | `<p>Hi</p>` | p: `color == "red"` (important wins) |
| `#x { color: red } p { color: blue }` | `<p id="x">Hi</p>` | p: `color == "red"` (specificity wins) |
| `body { color: navy }` | `<body><p>Hi</p></body>` | p inherits `color == "navy"` |
| `body { color: navy } p { color: red }` | `<body><p>Hi</p></body>` | p: `color == "red"` (own rule beats inherited) |
| `p {}` | `<p>Hi</p>` | p: empty `ComputedStyle` |

## Open Questions

### UA stylesheet

Browsers ship a default stylesheet that defines `<h1>` as bold and 2em, `<body>` as `display: block`, `<em>` as italic, etc. We need this for anything to look like a real page.

**Deferred to a future ADR** (or the paint phase). The cascade is written so that adding a UA stylesheet later is just prepending its rules to the cascade input — no code changes.

### Typed values

`ComputedStyle::get("color")` returns `std::string` today. Phase 3 will want `Color get_color()` returning a parsed color. That is a follow-up ADR.

### DOM mutation

v1 computes the `ComputedStyleMap` once. Mutations (script-driven or dev-tools-driven) require invalidation. Phase 4+.

## Record of Changes

- **2026-10-19:** Initial decision.
