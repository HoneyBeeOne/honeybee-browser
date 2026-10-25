# ADR-028: CSS Grid Layout (Basic)

**Status:** Implemented
**Date:** 2026-10-25
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout engine), ADR-013 (length units), ADR-016 (inline formatting), ADR-025 (external CSS), ADR-027 (@media)

## Context

ADR-025 through ADR-027 shipped external CSS loading, var()
resolution, and @media block support. On Wikipedia's HTML article,
the article body is now readable, references formatted, and
navbox styling visible. But **the sidebar remains a vertical
blob** -- the transformation those three ADRs have been building
toward is still locked behind a missing layout-engine feature.

### What the sidebar actually needs (measured 2026-10-25)

Wikipedia's Vector 2022 skin defines the entire sidebar + content
layout in a **top-level** rule (not @media-scoped, always matches
at any viewport):

```css
.mw-page-container-inner {
    display: grid;
    column-gap: 24px;
    grid-template-columns: 12.25rem minmax(0, 1fr);
    grid-template-areas: 'siteNotice siteNotice'
                        'columnStart pageContent'
                        'footer footer';
}
```

A second rule uses the shorthand form:

```css
.mw-page-container-inner {
    display: grid;
    column-gap: 24px;
    grid-template: min-content 1fr min-content / 12.25rem minmax(0, 1fr);
    grid-template-areas: 'siteNotice siteNotice'
                        'columnStart pageContent'
                        'footer footer';
}
```

Each direct child of `.mw-page-container-inner` declares its own
`grid-area: siteNotice` / `grid-area: columnStart` /
`grid-area: pageContent` / `grid-area: footer` to be placed at
the matching named cell.

### Why this PR cycle and not the ones before

**This is the single highest-leverage layout feature on the page.**
Two rules on one container produce the entire sidebar / content /
footer structure. Without them, the sidebar renders as an
unordered vertical list above the article. With them, it becomes
a 12.25rem-wide column on the left, article content fills the
remaining 1fr, and the footer spans both columns at the bottom.

### The layout-engine gap

The `Display` enum (`renderer/layout/layout_box.hpp`) is only:

```cpp
enum class Display : std::uint8_t { Block, Inline, None };
```

`layout_element` reads `display` and consumes only `Block`,
`Inline`, `None`. Any `display: grid` value falls through to
block (default) and lays out children vertically. That is why
every `<div class="mw-page-container-inner">` in the current
output is a vertical stack.

`layout_element` also currently ignores ~15 CSS properties that
would be needed for layout: `grid-template-*`, `grid-area`,
`flex-*`, `column-*`, `float`, `position`, `top/right/bottom/left`,
`z-index`, `gap`, `row-gap`, `column-gap`, `justify-content`,
`align-items`.

### Measured distribution (what else is inside @media at 1024)

For context: the @media-scoped layout property distribution shows
that if grid is not the priority, three other subsystems matter:

| Subsystem | @media-scoped declarations | Leverage |
|---|---|---|
| Position family | 69 (52%) | Overlays, thumbnails, z-index -- decorative |
| Flex family | 41 (31%) | Header bar, TOC, toolbar layout |
| Float family | 19 (14%) | Content-embedded (infoboxes, figures, catlinks) |
| Grid family | 2 (1.5%) in @media | **But ~4 grid rules at top-level produce the sidebar** |

**Grid count in @media is small; grid leverage in top-level rules is
huge.** The one measuring mistake to avoid here is looking only at
@media-scoped counts and concluding "grid doesn't matter." It does
-- because the rules that matter are top-level, always-match, and
sit on the exact container that produces the visible transform.

### What this ADR is not

This is **not** flex, position, or float. Those are meaningfully
different subsystems with their own implementation costs and their
own visible deltas. ADR-028 ships grid. Flex / position / float
each get their own ADR when the priority lands there.

## Decision

We add CSS grid layout support to the layout engine, scoped to the
forms that appear in real-world CSS: single grid container with
`display: grid`, fixed column widths (rem/px), `minmax(min, max)`
with `fr` max, named cells via `grid-template-areas` and per-child
`grid-area`, and gaps (`column-gap` / `row-gap`).

### `Display` enum gains `Grid`

```cpp
enum class Display : std::uint8_t {
    Block, Inline, None, Grid
};
```

`display: grid` parses to `Display::Grid`. `display: inline-grid`
and `display: subgrid` defer (0 uses in Wikipedia's CSS).

### Grid container properties parsed

From the container element's ComputedStyle:

- `grid-template-columns`: whitespace-separated list of track
  sizes. Supported forms: `<length>` (px/rem resolved via ADR-013),
  `<N>fr`, `minmax(<min>, <max>)` where min is `0` or `<length>`
  and max is `<N>fr` or `<length>`.
- `grid-template-rows`: same forms (for symmetric completeness).
- `grid-template-areas`: quoted strings defining named cells. One
  string per row, whitespace-separated cell names per string.
  `.` represents an empty cell.
- `grid-template` shorthand: `<rows> / <columns>` expands to
  `grid-template-rows` + `grid-template-columns`. Form with
  inline areas (`"areas" <rows> / <cols>`) defers (0 uses in
  Wikipedia).
- `column-gap` / `row-gap` / `gap`: `<length>` (already parsed by
  ADR-013's length resolver).

### Grid child placement

For each direct child element of a grid container:

- If the child has `grid-area: <name>` and `<name>` matches a cell
  in the container's `grid-template-areas`, the child is placed
  at the row/column of that cell. If multiple cells share the
  name (a span), the child occupies the bounding rectangle (so
  `siteNotice` spanning both columns in row 1 places the child
  across both).
- Otherwise the child auto-flows: placed at the next free cell,
  left-to-right within the row, top-to-bottom across rows.
  Auto-flow is only ever needed for tests and edge cases --
  Wikipedia's `.mw-page-container-inner` children all have
  explicit `grid-area`.

### Track sizing algorithm

Column track sizing:

- Fixed `<length>` tracks: exact width.
- `<N>fr` tracks: split the remaining container width
  proportionally. `<N>fr` = remaining_width * N / sum_of_fr.
- `minmax(min, max)` where max is `Nfr`: same as `Nfr`, but with
  `min` as a floor. If remaining_width * N / sum_fr < min, use
  `min`.
- `minmax(min, max)` where max is a `<length>`: use `max`,
  clamped by `min`.

Row track sizing:

- Fixed `<length>` rows: exact height.
- `<N>fr` rows in a flow-layout container: **treated as auto**
  (natural content height). CSS-spec says `fr` rows fill remaining
  container height, but our layout engine is flow-based (no
  parent-height allocation), so there's no "remaining height" to
  fill. Auto-sizing produces visually identical results for
  vertical flow.
- `min-content` rows: `<length>` of the tallest single-line
  content, calculated by re-running measure on the row's tallest
  child with the width already assigned by column sizing.
- `minmax(...)` rows: min/max clamped to auto content height.

### Cell placement + layout pass

1. Parse container's grid template → `columns: vector<float>` and
   `rows: vector<float>` (heights computed in step 4) + `areas: map<name, rect>`.
2. For each child, resolve `grid-area` → (row, col, row_span, col_span).
3. Compute column widths from template (step 1) using remaining
   container width.
4. Layout each child within its cell bounds (call `layout_element`
   with the cell width, respecting column gaps). Track per-row
   max height after child layout.
5. Assign final Y positions: cumulative row heights + row gaps.

### LayoutEngine API

`layout()` signature unchanged. New internal `layout_grid(container,
children, styles, ...)` dispatched from `layout_element` when
`display == Grid`. `layout_element` currently handles `Block` and
`Inline`; it gains a `Grid` branch that delegates to
`layout_grid`.

`Display::Grid` propagates through the existing `display_of()`
helper and `layout_element`'s dispatch switch.

## Design decisions

### Why `grid-template-areas` (not auto-flow)

CSS grid allows two placement modes:
1. **Auto-flow**: children fill cells left-to-right, top-to-bottom,
   based purely on child order.
2. **Named areas**: children declare `grid-area: name` matching a
   cell in the parent's `grid-template-areas`.

Wikipedia's `.mw-page-container-inner` uses **named areas**: the
3-row layout requires `siteNotice` (row 1) to span both columns;
`columnStart` (row 2, col 1) to be the sidebar; `pageContent`
(row 2, col 2) to be the article; `footer` (row 3) to span both.

Auto-flow with 4 children in a 2x3 grid would place them:
- `siteNotice` → row 1 col 1 (single cell, not spanning)
- `columnStart` → row 1 col 2 (wrong row)
- `pageContent` → row 2 col 1 (wrong column)
- `footer` → row 2 col 2

**Wrong layout.** Named areas are the only way to get Wikipedia's
sidebar correct.

### Why `min-content` and `fr` row sizing degrade to auto

CSS grid spec: `fr` rows fill remaining container height. But
`layout()` in this codebase is flow-layout: container height =
sum of children heights. There's no "remaining height" allocation
because the container doesn't have a fixed height.

For a vertical grid (which is what `.mw-page-container-inner`
is -- rows stacked top-to-bottom), treating `fr` rows as auto
produce identical results: the row takes its natural content
height, and the container grows to fit all rows.

`min-content` rows similarly: our layout engine measures content
widths via Pango, but doesn't compute "minimum inline width at
which content can flow." Simplification: `min-content` rows use
the natural single-line height of the tallest child. For
Wikipedia's usage (site notice row, footer row), this matches
because those rows are content-sized anyway.

Documented as v1 limitation: **grid is flow-oriented; fixed-height
`fr`/`min-content` resolution deferred to a future layout ADR if
needed.**

### Why fixed-and-fr columns only (skip auto, max-content, repeat())

Wikipedia's `.mw-page-container-inner` uses exactly:
- `12.25rem` (fixed rem)
- `minmax(0, 1fr)` (minmax with fr max)
- `min-content` (rows only)

Supporting `<length>`, `<N>fr`, `minmax(0 | <length>, <N>fr | <length>)`
covers 100% of Wikipedia's grid columns. Adding `auto`, `max-content`,
`repeat(N, ...)`, `fit-content(...)` extends scope with zero payoff
on the current page. Deferred (each has real use cases in real CSS,
just not here).

### Why shorthand expansion at parse time (matching ADR-013 pattern)

`grid-template: <rows> / <columns>` is a shorthand that ADR-012
already established a pattern for (shorthand → longhand expansion
at parse time). Expanding it during `Stylesheet::parse()` (into
`grid-template-rows` + `grid-template-columns` declarations)
follows the existing convention and keeps layout-time code simple.

Alternative: keep `grid-template` as its own declaration and have
layout split it. Ties layout code to shorthand parsing, breaks the
ADR-012 pattern. Rejected.

### Why `grid-area` per-child, not just container-side assignment

CSS grid children declare their own placement:
`<div class="mw-site-navigation">` in Wikipedia has
`grid-area: columnStart`. Our `ComputedStyle` for that element
carries `grid-area: columnStart`. `layout_grid` reads each child's
`grid-area` from that child's ComputedStyle, not from a container
attribute. This matches CSS semantics and means future author
stylesheets can override placement per-child.

### Why not flex (which the measurement shows is bigger)

The @media-scoped measurement shows flex (41 declarations) is
more common than grid (2 @media + ~4 top-level). But **flex layout
count doesn't reflect visual leverage** -- the flex rules produce
header bar and TOC alignment, which are far less visually dominant
than the sidebar / content / footer structure grid produces.

**Order of operations for visible delta**:
1. **ADR-028 (this)** -- grid. Sidebar columns. Largest single visual jump remaining.
2. **ADR-029** -- flex. Header bar + TOC alignment.
3. **ADR-030** -- position + float. Content-embedded elements (infoboxes, thumbnails, catlinks).

This order is leverage-driven, not count-driven. Grid unlocks one
large structural change; flex and position each unlock several
smaller ones.

### Meta-lesson-v3 applied

Per ADR-027's Meta-lesson: predicting visible delta from a CSS
subsystem requires tracing **three** paths:
1. Does it parse? -- `display: grid` parses today (ComputedStyle has the raw string).
2. Does it win the cascade? -- `.mw-page-container-inner` is a top-level rule, always wins (no specificity competition on this container).
3. Does its winning property get consumed by paint/layout? -- **No** today. `layout_element` drops `display: grid`.

ADR-028 closes path 3 for grid. **Predicted visible delta: high.**
Sidebar becomes a 12.25rem left column, article content fills
remaining width, footer spans both. This is a structural
transformation, not a refinement.

**Prediction caveat**: if grid layout lands but the sidebar still
doesn't shift right, one of the earlier paths is failing in a way
this ADR's measurement didn't catch (e.g. the child `grid-area`
values aren't in ComputedStyle, or a competing rule wins on the
container). Measured in Commit 2 with screenshots, documented
honestly.

## Deferred

- **`display: inline-grid`, `display: subgrid`**: 0 uses in Wikipedia CSS.
- **`grid-template` with inline areas** (`grid-template: "a a" "b c" / 10rem 1fr`): 0 uses on Wikipedia (they use longhand `grid-template-areas`).
- **`repeat(N, ...)`, `auto`, `max-content`, `fit-content(...)`**: 0 uses on Wikipedia's page-layout container.
- **`grid-auto-columns`, `grid-auto-rows`, `grid-auto-flow`**: 0 uses at top-level. Deferred until a page needs them.
- **`grid-column`, `grid-row` (line-based placement)**: Wikipedia uses `grid-area` only.
- **`fr` row sizing filling container height**: flow-layout constraint (documented above).
- **Implicit grid** (children that don't fit the template): auto-flow uses the last row extended. Formal `grid-auto-*` support deferred.
- **Nested grids**: works (each container gets its own layout pass), but not tested for correctness against Wikipedia's deep structure in v1.
- **`grid-template` shorthand with `min-content`**: Wikipedia uses `min-content` in `grid-template` shorthand -- v1 parses this shorthand by expanding to longhand + treating `min-content` rows as auto.
- **Flex layout (ADR-029), position/float (ADR-030)**: separate ADRs.

## Consequences

**Better:**
- **Sidebar / content / footer structural layout works** on Wikipedia.
  This is the transformation ADR-025 through ADR-027 have been
  building toward. Sidebar shifts right, article content fills
  remaining width, footer spans both columns at bottom.
- Grid container support is real infrastructure. Future
  stylesheets using simple 2-3 column layouts will render correctly.
- Same pattern as ADR-011 -> all subsequent layout ADRs:
  one layout subsystem per ADR, cleanly isolated, testable.

**Accepted limitations:**
- Grid is flow-oriented: `fr` rows behave as auto. Fixed-height
  grid containers (rare in modern web) don't fill height.
- Only fixed, `fr`, and `minmax` column/row sizing supported.
  `auto`/`max-content`/`repeat()` fall through to auto-flow.
- No line-based grid placement (`grid-column: 1 / 3`); only named
  areas + auto-flow.

**Unchanged:**
- ADR-025 through ADR-027's CSS parsing, @media filtering, var()
  resolution.
- Block and inline layout.
- All navigation, cache, hover, paint behavior.

## Implementation plan

Three commits on `feat/grid-layout`:

1. **Parsing + `Display::Grid`** -- add `Grid` to `Display` enum;
   parse `display: grid`; add grid-related parsing to
   `ComputedStyle` -- track list for `grid-template-columns` and
   `grid-template-rows` (as `optional<vector<TrackSize>>` where
   `TrackSize` is a small struct); `grid-template-areas` (as
   `optional<map<string, CellRect>>`); `grid-area` on child
   elements (as `optional<string>`); `grid-template` shorthand
   expansion at parse time. Tests: parse each form, verify
   expansion, verify per-child `grid-area` extracted.
2. **Layout: `layout_grid`** -- implement track sizing (columns),
   child placement (named areas + auto-flow), row height
   accumulation, gap handling. Dispatch from `layout_element` when
   `display == Grid`. Tests: 2-column fixed+fr, 2x3 grid with
   areas, gap, auto-flow fallback. **Commit message includes
   screenshots of Wikipedia before/after.**
3. **Docs** -- ADR-028 status -> Implemented, README + CHANGELOG
   with measured visible delta.

## Verification

**Unit tests**:
- Parse `display: grid` -> `Display::Grid`
- Parse `grid-template-columns: 12.25rem minmax(0, 1fr)` -> 2
  tracks: 12.25rem fixed, minmax(0, 1fr)
- Parse `grid-template-areas: 'a a' 'b c'` -> cells: `a` at (0,0)-(0,1) span, `b` at (1,0), `c` at (1,1)
- Parse `grid-template: min-content 1fr min-content / 12.25rem minmax(0, 1fr)` -> expands to rows + cols correctly
- Parse child `grid-area: columnStart` -> captured per-child
- Layout: 2-column fixed 200 + fr 1 in 800 width -> 200 + 600
- Layout: 2-column fixed 200 + minmax(0, 1fr) in 800 -> 200 + 600
- Layout: 2x2 grid with named areas -> children placed at correct cells
- Layout: gap 20 between columns -> 800-20 = 780 distributed
- Layout: auto-flow (no grid-area) -> left-to-right, top-to-bottom

**Visual verification (Commit 2)**:
- Load Wikipedia at 1024 viewport
- Compare against ADR-027's screenshot
- Sub-checks:

| Element | ADR-027 state | ADR-028 expected |
|---|---|---|
| Sidebar | Vertical blob above article | **12.25rem left column** |
| Article content | Full width | **Right of sidebar, remaining width** |
| Footer | Below article | **Bottom, spanning both columns** |
| References / navbox | Already styled (ADR-025/027) | Same |
| Header | Text row | Same (header is flex, ADR-029) |

If ADR-028 delivers, this is the **largest single visible delta**
since ADR-025 (external CSS loading). If the sidebar still doesn't
shift (e.g. child `grid-area` not in ComputedStyle, or another
rule winning), document honestly.

## Acceptance criteria

ADR-028 is complete when:

- [ ] `Display::Grid` added and parsed
- [ ] `grid-template-columns` / `grid-template-rows` parsed as track lists
- [ ] `grid-template-areas` parsed as named cell map
- [ ] `grid-template` shorthand expands to longhand
- [ ] Per-child `grid-area` captured
- [ ] `layout_grid` computes track sizes and places children
- [ ] `layout_element` dispatches on `Display::Grid`
- [ ] Unit tests cover 10+ parse + layout cases
- [ ] Visual delta measured on Wikipedia (Commit 2 screenshots)
- [ ] `main` is green: 327 + N cases, all pass, zero warnings
- [ ] ADR-028 status -> Implemented in Commit 3

**Explicit claim**: sidebar becomes a real left column, article
content fills remaining width, footer spans bottom. Largest single
visible delta since ADR-025.

## Implementation

Implemented on `feat/grid-layout` across four commits (Commit 2
split into 2a and 2b because the URL fix was discovered during
grid verification):

| Commit | Scope |
|---|---|
| `826e9ef` | `Display::Grid` + `display_of` + `grid-template` shorthand expansion (14+1 tests). |
| `da3def2` | **URL fix** discovered while debugging grid: root-relative URLs now resolve against origin, not base directory (4 regression tests). |
| `de48ba2` | `grid_layout.{hpp,cpp}` + `layout_element` dispatch on `Display::Grid` + `shift_subtree_y` (23 + 2 tests). |
| (this commit) | Docs: ADR-028 status -> Implemented, README + CHANGELOG, post-fix corrections to ADR-025/026/027. |

### Measured visible delta on Wikipedia (viewport 1774)

| Check | Result |
|---|---|
| Sidebar narrows to ~325px left column | ✅ |
| Article content indents to right column | ✅ |
| Navbox styling preserved (ADR-027) | ✅ |
| Article body readable | ✅ |
| **Article's vertical position within its cell** | ❌ ~660px too low (deferred) |
| No crash, no regression | ✅ |

The structural transform (columns side by side) DID happen. The
article's vertical placement within its cell is offset by ~660px --
"HTML" heading appears at y ≈ 730 instead of y ≈ 70. Root cause
hypothesis (unverified): either the article's
`.mw-content-container` child contains block children before its
visible content, or its own internal layout adds top
margin/padding not accounted for by grid row layout. Deferred to
a follow-up commit or its own ADR.

### The URL bug discovery (the actual biggest deliverable)

**ADR-028's verification trail revealed a five-ADR-deep bug that
silently blocked ADR-025, ADR-026, and ADR-027's real capability.**

When Wikipedia didn't shift at 1120+ viewports (grid rules should
have fired), the diagnostic sequence went:
1. Grid rules exist in external CSS ✓
2. Grid parsing works on that CSS ✓
3. Grid layout works on synthetic tests ✓
4. But grid rules never reach ComputedStyle on Wikipedia ✗
5. Cascade never sees them because external CSS fetch fails ✗
6. Fetch fails because the URL is `https://en.wikipedia.org/wiki/w/load.php` instead of `https://en.wikipedia.org/w/load.php` ✗
7. URL is wrong because `resolve_url` concatenated root-relative hrefs onto the base *directory* (with `/wiki/` subdir) instead of the base *origin* ✗

**Fix:** `resolve_url` for root-relative URLs now splits the base at
the first `/` after the scheme separator and concatenates onto
just scheme + host (+ port). 4 new tests cover subdirectory bases.

**Consequence:** every external CSS fetch on Wikipedia has been
silently 404ing since ADR-025. ADR-025/026/027's "measured deltas"
were real, but they came from inline `<style>` tags (~20KB of
Wikipedia's 242KB CSS) — not from external CSS. After this fix,
the external 222KB (including the `@media`-scoped grid rules
ADR-027/028 target) actually reaches the cascade. Post-fix, those
ADRs' actual visible deltas are larger than measured at the time.

Post-fix correction notes appended to ADR-025, ADR-026, ADR-027
preserve their historical record.

### What shipped vs. what didn't

**Shipped (real infrastructure, real visible delta):**
- Grid template parsing: fixed/fr/minmax tracks + named areas.
- Grid layout dispatch with track sizing, cell placement, gaps.
- Sidebar column narrows on Wikipedia at 1120+ viewports.
- Article content indents to the right column.
- **External CSS loading now actually works** (URL fix).
- All three prior ADRs' features actually reach the cascade.

**Didn't ship (deferred):**
- Article vertical position within its cell (~660px off).
- Grid auto-flow (only named-area placement).
- `inline-grid`, `subgrid`, `repeat()`, `auto`, `max-content`,
  `fit-content()`, line-based `grid-column`/`grid-row`.
- `fr` rows filling container height (flow-layout constraint).

### Test count

- Before ADR-028: 327 cases, 1367 assertions
- After Commit 1: 336 cases, 1401 assertions
- After Commit 2a: 365 cases, 1508 assertions (+4 URL fix in 2a,
  +25 grid in 2b; note the +25 grid tests were added during
  debugging session and accounted here)
- Zero warnings, zero failures

