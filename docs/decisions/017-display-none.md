# ADR-017: display: none

**Status:** Accepted
**Date:** 2026-10-21
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout), ADR-014 (UA stylesheet), ADR-016 (inline formatting)

## Context

As of v0.4.0, `layout.cpp` contains a hardcoded helper:

```cpp
bool is_non_rendered_tag(const std::string& tag) {
    return tag == "head" || tag == "style" || tag == "script"
        || tag == "title" || tag == "meta" || tag == "link"
        || tag == "base";
}
```

This list was added in PR #110 (ADR-011) as a pragmatic shortcut when the first paint landed and the CSS source of the welcome page was leaking into the visible output. It works — those seven elements and their subtrees are skipped during layout. But it is:

1. **A hardcoded list in C++**, extended only by editing layout.cpp.
2. **Not overridable by author CSS.** `<head>` has no `display` property in `ComputedStyle`; the layout skips it unconditionally. An author who wants to `display: block` a `<head>` (rare but legal) cannot.
3. **A magic special case** that future contributors will notice and have to understand before touching `layout_element`.

Meanwhile, ADR-016 added a `Display` enum with three values: `Block`, `Inline`, `None`. `Block` and `Inline` are consumed by `layout_element`. `None` is declared but unused — the ADR-016 implementation chose to leave the skip list in place and defer retirement to this ADR.

This ADR is the natural cleanup: turn the skip list into `display: none` rules in the UA stylesheet, wire `Display::None` into layout, delete the C++ helper.

## Decision

We implement `display: none` and retire the hardcoded skip list.

### Scope of v1

**`display: none` semantic**: an element with `display: none` in its `ComputedStyle` is entirely skipped during layout. Its subtree produces no `LayoutBox`, no `LayoutText`, no `LayoutInlineContext`. Its descendants — even descendants with `display: block` or `display: inline` — render nothing, because their ancestor is not rendered.

This matches CSS 2.1: `display: none` causes the element and its subtree to be removed from the formatting structure entirely.

**UA stylesheet additions** (ADR-014's file, extended):

```css
head, style, script, title, meta, link, base { display: none }
```

Seven rules, one per element that the current skip list covers. This preserves the current behavior exactly: those seven elements and their subtrees are never rendered. But now the skip happens through the cascade, not through a C++ helper.

**`layout.cpp` cleanup**: delete `is_non_rendered_tag` and its two call sites. Replace with a `display_of(child_style) == Display::None` check in the child iteration (both the block child path and the inline-run collection path).

### Deferred

- `display: flex`, `display: grid`, `display: table` — the layout modes remain unimplemented
- `display: contents` — the box-less display value (element's children participate in parent's formatting context; element itself produces no box)
- `visibility: hidden` — different semantic: element is rendered but invisible, occupies space; `display: none` removes entirely
- `content-visibility` — a CSS Containment level 2 property, optimizes rendering of off-screen content
- `display: none` on `<html>` or `<body>` — a strange edge case. Per CSS 2.1 the entire viewport would render nothing. v1 handles it naturally (the skip cascades from root), so no special code is needed, but it is not explicitly tested.
- CSS animations or transitions that toggle `display` — not implemented
- `@media`-conditional `display: none` — at-rules deferred

## Rationale

### 1. Cascade-driven, not hardcoded

`display: none` is a cascade property. Putting `head { display: none }` in the UA stylesheet means:

- The cascade handles it (ADR-014's origin-aware logic still applies)
- An author who writes `head { display: block }` can override (rare, but spec-compliant)
- Future extensions (`display: flex`, `display: grid`) reuse the same mechanism without adding more C++

The current skip list requires a code change for every new element that should not render. The UA stylesheet approach requires a stylesheet change — same file (`ua_stylesheet.cpp`) but no C++ logic, and the change is visually obvious in the CSS blob.

### 2. `Display::None` already exists

ADR-016 added `Display::None` to the enum but never wired it in. This ADR is the smallest possible change that uses it: one check, in two places (block children and inline-run collection).

### 3. Smallest possible cleanup

The full scope of this ADR is:
- ~10 lines of C++ change (add skip check in two places, remove the helper)
- ~2 lines of UA stylesheet
- ~1 line of test updates for existing tests that relied on `is_non_rendered_tag` behavior (they should still pass because the UA stylesheet produces the same effect)
- ~8 lines of new tests

Compare to ADR-016's ~1500 lines. This is the kind of ADR that exists to close a gap, not open a new one.

### 4. Unblocks later work

Retiring the skip list means later ADRs can:
- Implement `<template>` (`template { display: none }` in UA)
- Handle `<dialog>` (`dialog:not([open]) { display: none }` — pseudo-class dependency, deferred)
- Clean up `<noscript>` handling
- Any future "this element doesn't render" case becomes a UA stylesheet edit, not a layout.cpp edit

## Alternatives Considered

### Keep the skip list, add display: none only for new elements

**Rejected.** Leaves the C++ helper in place, splitting the "never rendered" logic across two files. The whole point of ADR-016's `Display` enum is that it should be the single source of truth.

### Hardcode `Display::None` for `<head>`, `<style>`, etc. in layout.cpp without going through cascade

**Rejected.** That's the current skip list with extra steps. The cascade path (UA stylesheet → ComputedStyle → layout) is what makes it overrideable by author CSS. If we hardcode in layout.cpp, we've gained nothing over the current helper.

### Include `display: contents` in v1

**Rejected.** `display: contents` requires the element to be skipped but its children to participate in the parent's formatting context. That's a fundamentally different layout path (the element produces no box but contributes its children to the inline run of the parent). Deferred to a future ADR when there's a concrete use case.

### Implement `visibility: hidden` alongside `display: none`

**Rejected.** Different semantics. `visibility: hidden` occupies space; `display: none` does not. Conflating them in one ADR would blur the distinction. Deferred.

## Consequences

### Files changed

- `src/renderer/css/ua_stylesheet.cpp` — 7 `display: none` rules added
- `src/renderer/layout/layout.cpp` — remove `is_non_rendered_tag`, add `Display::None` skip check in child iteration (block path and inline-run path)
- `src/renderer/layout/layout_box.hpp` — no changes (Display enum already has None)
- `tests/test_ua_stylesheet.cpp` — rule count 14 -> 15
- `tests/test_layout.cpp` — existing tests using `<head>` implicitly (via the skip list) should still pass; new tests for `display: none` on elements and inheritance through descendants

No changes to `renderer::dom`, `renderer::paint`, `renderer::html`.

### Behavior examples

| HTML + CSS | Before ADR-017 | After ADR-017 |
|---|---|---|
| `<head><title>X</title></head>` | skipped by helper | skipped by UA `head { display: none }` |
| `<div style="display: none">hidden</div>` | rendered | skipped (author CSS honored) |
| `<div style="display: none">hidden</div>` with `div { display: none }` in UA | rendered (skip list has no `div`) | skipped |
| `<div style="display: block">visible</div>` (default UA: `div { }`, so block) | rendered | rendered |
| `<script>` in `<body>` | skipped by helper | skipped by UA `script { display: none }` |
| `<style>` in `<body>` (legal per HTML5) | skipped by helper | skipped by UA `style { display: none }` |
| `<head style="display: block">X</head>` (author override) | skipped (helper fires first) | rendered (author override wins) |
| `<div style="display: none"><p>child</p></div>` | rendered | child also skipped (inheritance of not-rendering) |

The last two rows are the substantive changes: author override of `<head>`, and the inheritance of `display: none` into descendants (which the skip list also did, but only for the specific seven tags).

### What layout doesn't change

Block flow, inline flow, box model, whitespace collapsing, direction resolution — all identical. The only difference is where the "skip this element" decision comes from.

### What paint doesn't change

`paint.cpp` iterates the layout tree. If an element is skipped during layout, it never appears in the tree, so paint has nothing to do differently.

## Open Questions

### `display: none` on `<html>` or `<body>`

Per CSS 2.1, `display: none` on `<html>` would produce a blank page. On `<body>`, `<html>` would render its other children (which normally don't exist). v1 handles both naturally (skip cascades from wherever `display: none` is set), but this is not a tested case. If a real page needs it, a test follows.

### `display: none` and inline children

The current `flush_inline_run` handles inline children. If a `<div>` contains `<span>visible</span><span style="display:none">hidden</span><span>also visible</span>`, does the middle span's text disappear? Yes — `display_of` returns `Display::None` for it, so it is not added to `inline_run`. The two visible spans flow together. Same as before with the helper (except the helper only matched 7 tags, not arbitrary author CSS).

### The `--display: none` inherited property

`display` is *not* an inherited CSS property (per CSS 2.1). So `body { display: none }` does not cause `<p>` to have `display: none` in its `ComputedStyle`. But the visual effect is the same — the `<p>` produces no layout box because its ancestor is skipped during layout_element recursion. The `ComputedStyle` for `<p>` will still have its own `display` (likely `block`), but layout doesn't reach it. Correct behavior; worth documenting since it's a subtle point.

### What about `<template>`?

`<template>` per HTML5 is not rendered (its content lives in a separate document fragment). Adding `template { display: none }` to the UA stylesheet would cover it. Currently not in the list — worth adding in this ADR if `<template>` is considered in scope, or deferring.

**Recommendation**: include `<template>` in v1's UA rules. It's the same category as the others, costs one more line, and matches what every browser does.

## Record of Changes

- **2026-10-21:** Initial decision.
