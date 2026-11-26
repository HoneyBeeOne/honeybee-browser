# ADR-009: CSS Rule Parsing

**Status:** Accepted
**Date:** 2026-10-19
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-001 (technology stack), ADR-006 (return to ADR-001), ADR-008 (CSS selector matching)

## Context

ADR-008 gave us selector matching: given a selector text like `div.foo > p` and a `dom::Node`, we can decide whether it matches and what its specificity is.

But browsers do not consume selectors in isolation. They consume **stylesheets** — text like:

```css
h1 { color: navy; font-size: 24px; }
p { margin: 0 0 1em 0; }
a:hover { color: blue; }
```

We need to turn that text into a structured list of rules, each pairing a `SelectorList` (from ADR-008) with a list of declarations (`property: value`). That structured list is what the cascade in Phase 3 will consume.

This ADR decides how we parse CSS text into rules.

## Decision

**We implement a CSS rule parser ourselves**, in `renderer/css/`. lexbor's CSS module is not used.

### Scope of v1

**Rule sets:**

- `selector { declaration; declaration; ... }`
- Selector text delegates to `SelectorList::parse` (ADR-008)
- Braces `{` and `}` delimit the declaration block
- Empty declaration blocks (`{}`) are valid — a rule with no declarations is allowed (used in the wild to reserve hooks)

**Declarations:**

- `property: value;` with optional trailing semicolon on the last declaration
- Property names are identifiers (`[a-zA-Z-][a-zA-Z0-9-]*`)
- Values are raw strings, trimmed of surrounding whitespace
- `!important` at the end of a value sets `Declaration::important = true`
- Whitespace around `:` and `;` is ignored

**Comments:**

- `/* ... */` block comments are stripped during parsing

**Error recovery:**

- Malformed declarations are skipped (not the whole rule)
- Malformed rules are skipped (not the whole stylesheet)
- This matches the CSS 2.1 error-handling model: consumers must ignore what they cannot parse

**Deferred to future ADRs:**

- At-rules: `@media`, `@import`, `@font-face`, `@supports`, `@keyframes`
- Custom properties (`--main-color: #fff`) and `var()` function
- Shorthand expansion (`margin: 0 1em` → four longhands)
- Functions in values (`calc()`, `rgb()`, `hsl()`, `url()`)
- CSS nesting (Level 3)
- `@layer`, cascade layers

## Rationale

### 1. Coupling

lexbor's CSS module (`lexbor/css/`) parses CSS into lexbor's own data structures (`lxb_css_rule_t`, `lxb_css_selector_t`, etc.). Using it would require either:

- Keeping lexbor's CSS tree alive for the cascade phase, or
- Reverse-converting it into our own structures.

Both repeat the coupling mistake that ADR-007 and ADR-008 rejected. The cascade must run on our own `Declaration` and `Rule` types, which we control and extend (e.g., with a `ComputedValue` variant later).

### 2. We already have the selector parser

ADR-008 gave us `SelectorList::parse`. The CSS parser's job is small: find selector text, find `{`, find `}`, split declarations on `;` and `:`. Delegating selector parsing to ADR-008's implementation means the two halves fit together with no impedance mismatch.

### 3. We need our own declaration model

Phase 3 (cascade) and Phase 4 (layout) will attach typed computed values to declarations: `Length`, `Color`, `Keyword`. lexbor's declaration structs are flat `(name, value)` pairs; our future ones will carry typed variants. Owning the parser means owning the type.

### 4. Educational value

CSS rule parsing is another "interesting part" — small, spec-bounded, and testable in isolation. Borrowing it erases the learning opportunity, just as with selectors.

### 5. Performance is acceptable

A stylesheet is typically a few KB. Parsing it once per page load is O(n) in the input size, with no ambiguity. There is no performance reason to delegate.

## Alternatives Considered

### Use lexbor's CSS parser

**Rejected.** Couples Phase 2 to lexbor's data model, same as ADR-007 and ADR-008 rejected. We would still need to walk lexbor's CSS tree and translate it, which is more work than writing the parser directly.

### Defer CSS parsing entirely, hardcode style rules in C++ for the demo

**Rejected.** Doesn't advance the project. The demo would show fixed output, not real CSS processing. The educational goal is to see CSS work end-to-end.

### Only support a minimal subset: selector + one declaration

**Rejected.** Not a real CSS parser. A browser needs to handle real CSS, even if it only computes a subset of properties. The syntax must be complete enough to consume real-world stylesheets.

### Adopt an existing C++ CSS library (e.g., RapidCSS, CSSParser)

**Rejected.** Adding a third-party dependency for ~300 lines of parser code inverts the project's priorities. We vendor lexbor because HTML parsing is genuinely hard (thousands of edge cases in the spec). CSS rule syntax is regular enough to write directly.

## Consequences

- New file `src/renderer/css/stylesheet.hpp` — public types
- New file `src/renderer/css/stylesheet.cpp` — implementation
- New file `tests/test_stylesheet.cpp` — unit tests
- `src/CMakeLists.txt` and `tests/CMakeLists.txt` updated

### Public API

```cpp
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

    // Parses `text` into a stylesheet. Malformed rules and
    // declarations are silently skipped (per CSS 2.1 error handling).
    // Always returns a valid (possibly empty) Stylesheet.
    static Stylesheet parse(std::string_view text);
};

} // namespace honeybee::renderer::css
```

- `Stylesheet::parse` **never fails**. Bad input yields fewer rules, not an exception or null. This matches browser behavior — a page with one broken rule still renders.

### Behavior examples

| Input | Result |
|---|---|
| `h1 { color: red; }` | 1 rule, selector `h1`, 1 declaration `color: red` |
| `h1 {}` | 1 rule, empty declarations — valid |
| `h1 { color: red }` | 1 rule — trailing `;` optional |
| `h1 { color: red; broken ; font-size: 20px; }` | 1 rule, 2 declarations (broken skipped) |
| `{ color: red; }` | 0 rules (empty selector is invalid) |
| `h1 { color }` | 0 declarations (no `:` separator) |
| `h1 { color: red } h2 { color: blue }` | 2 rules |
| `h1 { color: red !important }` | 1 declaration with `important = true` |
| `/* comment */ h1 { color: red; }` | 1 rule, comment ignored |
| `h1 { color: red` | 0 rules (unclosed brace) |

### What stays the same

- **No new lexbor includes.** The isolation invariant from ADR-007 holds: `grep -rl "lexbor/" src/` must still return exactly `src/renderer/html/convert.cpp`.
- **`renderer::dom` is untouched.** CSS parsing operates on text, not DOM nodes.
- **`SelectorList::parse` is untouched.** The CSS parser calls it; it does not reimplement it.

## Open Questions

### Where does the cascade live?

ADR-009 produces a `Stylesheet`. The cascade consumes a `Stylesheet` and a `dom::Document` and produces a map from `dom::Element*` to `ComputedStyle`. That is a separate concern, deferred to ADR-010.

### Do we support `@media` in v1?

No. `@media` requires evaluating a media query against the target device (`screen`, `print`, width, height), which is a Phase 3 concern tied to the viewport. Deferred to ADR-011 (or later).

### Do we support shorthand properties (`margin: 1px 2px`)?

Not in the parser. The parser stores the raw value string. Shorthand expansion happens at **cascade** time, where we can turn `margin: 1px 2px` into four longhand computed values. This keeps the parser simple and the cascade richer.

### What about user-agent stylesheets?

The cascade will combine the UA stylesheet, author stylesheet(s), and (later) user stylesheet, applying the standard cascade order. That's ADR-010's concern.

## Record of Changes

- **2026-10-19:** Initial decision.
