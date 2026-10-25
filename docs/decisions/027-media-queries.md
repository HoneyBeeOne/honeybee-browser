# ADR-027: @media Block Support

**Status:** Accepted
**Date:** 2026-10-25
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-009 (CSS parser), ADR-010 (cascade), ADR-025 (external CSS loading), ADR-026 (var() resolution)

## Context

ADR-025 shipped external CSS loading. ADR-026 shipped var()
resolution. Neither delivers the sidebar / header / table-of-
contents **layout** transformation that a user would see in a
real browser -- those rules live inside `@media` blocks, and
the parser drops `@media` blocks entirely per ADR-009
(`skip_at_rule`).

### Measured on Wikipedia's CSS (2026-10-25)

**@media block census**: 64 blocks total.

| Query type | Blocks |
|---|---|
| `min-width` only | 24 |
| `max-width` only | 18 |
| `prefers-color-scheme` | 5 |
| `print` | 4 |
| `prefers-reduced-motion` | 1 |
| `min-width` + `max-width` combined | 1 |
| Other (`screen`, `all`, etc.) | 11 |

**Width thresholds observed**: min-width values seen at 420, 640,
1120, 1680; max-width values seen at 640, 1399, and various
`calc()` expressions.

**Viewport 1024 (our test width)**:
- **Fires without calc support**: ~20 blocks (31%)
- **Fires with calc support**: ~33 blocks (52%)

The 13-block delta is entirely `max-width: calc(1120px - 1px)`
blocks (`calc(1120px - 1px)` = 1119, so 1024 <= 1119 fires).
Those blocks are almost certainly the "tablet" mid-size layout
rules -- exactly the Vector-skin rules we're trying to unlock.
Skipping calc would forfeit them.

**Content volume inside @media blocks**:
- 722 rules
- 1845 declarations
- **953 consumed-property declarations (51.6%)** -- properties our layout engine reads
- **106 layout-property declarations (5.7%)** -- grid, flex, column, float, position (which our layout engine currently ignores)
- Remainder: other CSS properties (transition, opacity, etc.)

**Vector skin class coverage inside @media**:
- `.vector-menu` in 8 blocks
- `.vector-header` in 7 blocks
- `.vector-toc` in 5 blocks
- `.vector-column-start` in 2 blocks
- `.vector-body` in 1 block
- `.vector-main-menu` in 1 block

The Vector layout rules we couldn't reach in ADR-025 are
confirmed to exist inside @media blocks.

### Meta-lesson-v3 caveat (built into this ADR from the start)

ADR-026's meta-lesson: **consumed-property coverage is necessary,
not sufficient, for visible change.** The rule containing the
resolved declaration must ALSO win the cascade after parsing
(specificity beats competitors, media-query gating passes).

**Same reasoning applies here.** 953 consumed-property
declarations inside @media is a large number. But entering the
cascade is not the same as winning it -- and winning the cascade
is not the same as producing visible change (layout properties
like grid/flex still get dropped by the layout engine).

ADR-027's **visible delta is measured in Commit 2, not predicted
here**. If it turns out negligible (as ADR-026's var() resolution
did), the ADR is documented as **infrastructure** -- the parser
now has @media rules with concrete properties, and a future
layout-engine ADR (grid/flex support) is what unlocks the
structural transform.

## Decision

We add `@media` block parsing and cascade-time viewport filtering.

### Parser: extract @media blocks

`Stylesheet::parse()` currently calls `skip_at_rule()` which
consumes a `@media ... { ... }` block without processing it. New
behavior:

- Detect `@media` (case-insensitive) at rule-boundary position.
- Parse the prelude text up to `{`.
- Recursively parse the block body as rules (reuse existing rule
  parsing loop).
- Attach a `MediaQuery` to each extracted rule.

### `MediaQuery` type

```cpp
struct MediaQuery {
    // Empty = always matches (e.g. `screen` alone, `all` alone).
    // unsupported = true means "never matches" -> rule dropped.
    bool unsupported = false;
    std::optional<float> min_width_px;
    std::optional<float> max_width_px;
};
```

`Rule` gains `std::optional<MediaQuery> media;` -- nullopt means
"unconditional rule" (top-level, always applies); MediaQuery
present means "only applies if the query matches".

### Prelude classification

A prelude is classified by simple text scanning:

**Supported (produces a MediaQuery)**:
- `screen`, `all`, `screen and all`, or empty -> `MediaQuery{}` (always matches)
- `(min-width: Npx)` -> `MediaQuery{min_width_px = N}`
- `(max-width: Npx)` -> `MediaQuery{max_width_px = N}`
- `(min-width: calc(Npx - Mpx))` -> `MediaQuery{min_width_px = N - M}`
- `(max-width: calc(Npx - Mpx))` -> `MediaQuery{max_width_px = N - M}`
- `(min-width: calc(Npx + Mpx))` -> `MediaQuery{min_width_px = N + M}`
- Combinations joined by `and`: both constraints set

**Unsupported (rule dropped entirely)**:
- Any prelude containing `print`, `prefers-color-scheme`,
  `prefers-reduced-motion`, `prefers-contrast`, `orientation`,
  `aspect-ratio`, `resolution`, `hover`, `pointer`, range syntax
  (`width >= 640px`)
- Malformed calc expressions
- Anything the parser doesn't recognize

Dropping is safe per CSS spec: a non-matching query means its
rules don't participate in the cascade. Our "unsupported" == "never
matches" == "rules dropped" is correct behavior for a browser that
doesn't implement those query types.

### Cascade: viewport filtering

`cascade()` gains a `float viewport_width` parameter (default
1024.0f for backward compatibility):

```cpp
ComputedStyleMap cascade(
    const Stylesheet& ua,
    const Stylesheet& author,
    const dom::Document& doc,
    const RenderState& state,
    float viewport_width = 1024.0f);
```

`apply_rule()` gains an early-return check:

```cpp
if (rule.media && !media_matches(*rule.media, viewport_width)) {
    return;  // skip this rule entirely
}
```

`media_matches()`:

```cpp
bool media_matches(const MediaQuery& q, float vw) {
    if (q.unsupported) return false;
    if (q.min_width_px && vw < *q.min_width_px) return false;
    if (q.max_width_px && vw > *q.max_width_px) return false;
    return true;
}
```

### Pipeline wiring

`RenderPipeline::relayout(viewport_width)` currently calls
`cascade(ua, sheet, *doc_, state_)`. Change to
`cascade(ua, sheet, *doc_, state_, viewport_width)`. One-line
change. All existing tests pass (viewport_width defaults to 1024).

## Design decisions

### Why calc support in v1 (not deferred)

The `max-width: calc(1120px - 1px)` blocks are the "up to tablet-
width" layout rules. They fire at 1024. Without calc, we skip them
and lose the mid-size Vector layout -- exactly the transform we're
trying to deliver. Calc evaluation is simple arithmetic (~30 lines
for `Npx +/- Mpx`), and it unlocks 13 more firing blocks (20 -> 33
at viewport 1024).

The alternative -- "ship @media without calc, document the miss" --
would be like ADR-025 shipping without external CSS. The
infrastructure would be there but the payoff (Vector layout) would
still be locked behind a missing parser feature.

**Only simple calc is supported**: `calc(Npx - Mpx)`, `calc(Npx +
Mpx)`, plus `calc(M) * 1` if trivial. Any calc with units other
than px, or with more than one operation, is treated as
unsupported -> rule dropped. That covers 100% of what Wikipedia
uses (`calc(1120px - 1px)` and `calc(640px - 1px)` and
`calc(1680px - 1px)` are the only calc patterns in Wikipedia's
media queries).

### Why viewport_width is a cascade parameter, not RenderState field

`RenderState` is per-element state (visited URLs, hovered element,
pointer position). Viewport width is a **global cascade input** --
it's known at `render()` / `relayout()` time and applies uniformly
to all elements in a cascade pass.

Passing it as a parameter keeps `RenderState` focused on the
per-element mutable state that changes per-mouse-event. Viewport
changes trigger a full `relayout()` (already true), which rebuilds
the cascade map from scratch. There's no scenario where viewport
changes without a relayout.

Matches the existing `LayoutEngine` pattern: `layout(doc, styles,
measurer, viewport_width)` takes viewport as a parameter.

### Why unsupported queries drop their rules (not fire them)

CSS spec: `@media print { ... }` only applies when the print
stylesheet is active. For a screen browser, it means "don't apply
these rules." If we fired print rules on screen, we'd get print
layout (`display: none` on navs, page-break rules, monochrome
colors) applied to screen -- broken.

Same for `prefers-color-scheme: dark` -- the browser doesn't have
dark mode support, so those rules should not apply.

Dropping is correct, safe, and matches what a real browser does
when it doesn't support a media feature.

### Why recursive @media is deferred

CSS allows `@media screen { @media (min-width: 640px) { ... } }`.
Wikipedia's CSS has none (verified by scanning all `@media`
preludes for nested `@media` tokens -- zero). Recursive support
would multiply the parser's complexity for no current benefit.

If a future page uses nested @media, the outer block drops (as
unsupported syntax), which is safe. When a page actually needs it,
add it in a follow-up ADR.

### Why 1024 is the cascade default

Preserves the current behavior of every existing test
(`test_layout.cpp`, `test_cascade.cpp`) -- they don't pass a
viewport width, they get 1024, which matches their assumption of
a standard desktop width. The pipeline passes actual width via
`relayout(viewport_width)`.

If a test ever needs to test at a different viewport, it can pass
explicitly. No test breakage from this ADR.

### Meta-lesson-v3 honesty: what "953 consumed declarations" means

ADR-025 measured 27% parser acceptance -> delivered ~40-50% visible
improvement (unexpectedly good).

ADR-026 measured 84.2% consumed-property coverage of var() ->
delivered ~0% visible improvement (cascade loss).

The number tells us **how much CSS enters the cascade**, not how
much wins it or how much is visible. For @media, 953 consumed
declarations across 33 firing blocks is real volume. But whether
it produces visible change depends on:

1. Whether those declarations' rules have enough specificity to
   win against non-@media rules competing for the same properties.
2. Whether the winning declarations target properties our layout
   engine actually consumes.
3. Whether layout engine changes are needed for the remaining
   layout properties (grid-template-columns, flex-*, column-*,
   float, position).

**ADR-027's Implementation section measures the actual visible
delta in Commit 2.** If it's significant, ship and document the
measured delta. If it's negligible, document honestly and note
that the ADR is infrastructure for a future grid/flex layout
engine ADR. Either outcome is a valid completion of ADR-027.

## Deferred

- **Recursive `@media`**: no current use case; safe to drop.
- **`prefers-color-scheme`, `prefers-reduced-motion`,
  `prefers-contrast`**: need OS/UI integration, future ADR.
- **`@media print`**: no print rendering yet.
- **Complex queries**: `aspect-ratio`, `orientation`,
  `resolution`, `hover`, `pointer`, range syntax
  (`width >= 640px`).
- **`calc()` with mixed units** (`calc(50% - 20px)`,
  `calc(10em + 2px)`): our simple calc evaluator only handles
  `Npx +/- Mpx`. Mixed-unit calc falls to unsupported.
- **Range syntax** (`@media (640px <= width <= 1024px)`): CSS
  Media Queries Level 4 syntax. Wikipedia uses only legacy
  `min-width`/`max-width`, so this is free to defer.
- **Nested `@media`**: no current use case.
- **Grid / flex / column layout**: 106 declarations inside
  @media target grid/flex/column/float/position properties.
  Our layout engine currently ignores these. Even after ADR-027
  parses them into the cascade, layout won't render columns or
  grid. A future "CSS Layout Engine" ADR would unlock the
  structural transform fully.

## Consequences

**Better:**
- 33 @media blocks fire at viewport 1024 (52% of 64 blocks).
- ~430-450 rules enter the cascade at that viewport (from 722
  total inside all @media blocks, proportionally).
- ~200-250 consumed-property declarations across those rules
  become cascade candidates.
- Vector skin layout class rules (`vector-menu`,
  `vector-header`, `vector-toc`) become reachable.
- The @media parsing infrastructure is now present for any
  future parser feature that adds more media query types.

**Accepted limitations:**
- Unsupported queries drop their rules silently (correct behavior,
  but users would only notice if a page relies on dark mode or
  print styles).
- Layout properties (grid/flex/column, 5.7% of declarations) still
  get dropped by the layout engine -- no visible structural
  transform from those alone.
- `calc()` support is limited to `Npx +/- Mpx` -- complex calc
  falls to unsupported.
- Visible delta over ADR-026 measured in Commit 2, not predicted
  here. If negligible, ADR-027 is infrastructure for a future
  layout-engine ADR.

**Unchanged:**
- ADR-025 external CSS loading, cache, fetch.
- ADR-026 var() resolution.
- Existing pseudo-class matching, hover behavior, navigation.
- Every test that doesn't explicitly set a viewport -- they
  default to 1024.

## Implementation plan

Three commits on `feat/media-queries`:

1. **`@media` parsing** -- `Rule` gains `optional<MediaQuery>`;
   `Stylesheet::parse` extracts blocks. Tests: classify each
   prelude type (screen, all, min-width, max-width, calc variants,
   print, prefers-*, malformed). Rule storage: MediaQuery nested
   inside Rule.
2. **Cascade filtering** -- `cascade()` gains `viewport_width`
   param; `apply_rule` early-returns on media mismatch.
   Pipeline passes viewport. Tests: match at viewports 500/1024/
   1500 with various min/max widths. **Commit message includes
   the measured visible delta on Wikipedia at 1024.**
3. **Docs** -- ADR-027 status -> Implemented, README + CHANGELOG
   with measured delta.

## Verification

**Unit tests**:
- Parse `@media screen { a { color: red } }` -> rule with
  `MediaQuery{}` (always matches)
- Parse `@media (min-width: 640px) { a { ... } }` -> rule with
  `min_width_px = 640`
- Parse `@media (max-width: 640px) { ... }` -> `max_width_px = 640`
- Parse `@media (min-width: 640px) and (max-width: 1024px)` ->
  both constraints
- Parse `@media print { ... }` -> rule dropped entirely
- Parse `@media (prefers-color-scheme: dark) { ... }` -> dropped
- Parse `@media (min-width: calc(640px - 1px)) { ... }` ->
  `min_width_px = 639`
- Parse `@media (max-width: calc(1120px - 1px)) { ... }` ->
  `max_width_px = 1119`
- Cascade at viewport 1024: 640-min fires, 1120-min doesn't fire,
  640-max doesn't fire, 1399-max fires, 1119-max fires
- Cascade at viewport 500: 640-min doesn't fire, 640-max fires
- Cascade at viewport 1500: 1120-min fires, 1680-min doesn't
- Cascade with `@media print` at any viewport -> rule never fires

**Visual verification (Commit 2)**:
- Load Wikipedia at 1024 viewport, screenshot
- Compare to ADR-026's post-var() screenshot
- Sub-checks:

| Element | ADR-026 state | ADR-027 expected |
|---|---|---|
| Body text | Readable | Same |
| References | Numbered | Same |
| Sidebar layout | Unstyled blob | **uncertain** -- depends on whether @media rules cascade-win and whether they use grid/flex (which layout drops) |
| Header layout | Text row | Same uncertainty |
| TOC layout | Unstyled list | Same uncertainty |

- **Escape hatch (same as ADR-026)**: if visible delta is
  negligible, ADR-027's Implementation section documents it
  honestly and notes that ADR-027 is infrastructure for a future
  layout-engine ADR (grid/flex support). The @media parser is
  real infrastructure regardless of today's visible delta.

## Acceptance criteria

ADR-027 is complete when:

- [ ] `MediaQuery` type added to stylesheet.hpp
- [ ] `Rule` gains `optional<MediaQuery> media`
- [ ] `Stylesheet::parse` extracts @media blocks (recursive body parse)
- [ ] Prelude classifier supports: `screen`/`all`/empty,
      `min-width: Npx`, `max-width: Npx`, `calc(Npx +/- Mpx)`,
      `and` combinations
- [ ] Unsupported preludes (`print`, `prefers-*`, complex) drop rules
- [ ] `cascade()` accepts `viewport_width` param
- [ ] `apply_rule()` skips rules whose MediaQuery doesn't match
- [ ] `RenderPipeline::relayout` passes viewport to cascade
- [ ] Unit tests cover 12+ cases
- [ ] Visible delta measured on Wikipedia (Commit 2 message)
- [ ] `main` is green: 306 + N cases, all pass, zero warnings
- [ ] ADR-027 status -> Implemented in Commit 3

**Explicit claim**: 33 @media blocks fire at viewport 1024 (52%),
delivering ~200-250 consumed-property declarations into the
cascade. Visible delta measured in Commit 2 -- if negligible,
ADR-027 is infrastructure for a future layout-engine ADR.
