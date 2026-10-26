# ADR-029: Flex Layout (Basic)

**Status:** Implemented
**Date:** 2026-10-26
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout engine), ADR-013 (length units), ADR-016 (inline formatting), ADR-025 (external CSS), ADR-028 (grid layout)

## Context

ADR-028's layout-tree dump revealed that Wikipedia's 585px header
above the grid container -- the largest remaining visual gap --
is entirely a missing-flex-layout problem. Wikipedia's Vector 2022
skin positions the header, search, and dropdown menu as a
horizontal bar via CSS flex; without flex layout support, our
engine treats each header child as a block, stacking them
vertically.

### Measured on Wikipedia's CSS (2026-10-26)

**Flex rule count**: 36 total (26 `display: flex`, 10
`display: inline-flex`).

**Property distribution across flex containers**:

| Property | Uses |
|---|---|
| `display: flex` (container marker) | 36 |
| `align-items` | 18 |
| `justify-content` | 7 |
| `flex-flow` (shorthand) | 4 |
| `flex-wrap` | 3 |
| `gap` | 2 |
| `flex-direction` | 2 |
| `flex` (shorthand) | 1 |
| `flex-shrink` | 1 |
| `column-gap` | 1 |

**Header-specific rules** (the visual target):

```css
.mw-header {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 16px;
}
.mw-header .vector-header-start,
.mw-header .vector-header-end {
    display: flex;
    align-items: center;
}
.vector-header-container {
    display: flex;
    justify-content: center;
}
```

**Value forms present** (from full CSS scan):

- `flex-direction`: `column` (2 uses); row is default elsewhere
- `justify-content`: `center`, `end`, `flex-end`, `space-between`
- `align-items`: `center`, `flex-start`
- `flex-wrap`: `nowrap`, `wrap`
- `gap` / `column-gap`: `16px`, `6px`, `8px` (fixed px only)
- `flex`: `1 1 100%` (1 use, shorthand form)
- `flex-shrink`: `1`
- `flex-flow`: `row`, `row nowrap`, `column nowrap` (shorthand form)

**Current visible state** (from ADR-028 layout-tree dump at
viewport 1774):

- `.mw-page-container-inner` starts at y=585
- 585px of vertically-stacked header blocks above it
- Article content at y=633 (585 + site notice)
- **All article content is below the fold until scroll**

**Target visible state**: header collapses to ~50-100px tall
(flex row of logo + search + hamburger). Article content moves
to y≈50-150. First-fold finally shows actual content instead of
stacked header blocks.

### The three-path prediction (per ADR-027/028 Meta-lesson)

| Path | Status |
|---|---|
| 1. Parse `display: flex`? | ❌ Falls to `Display::Block` today |
| 2. Win cascade? | ✅ Flex rules are in the cascade (external CSS now loads post-URL-fix) |
| 3. Consumed by layout? | ❌ `layout_element` has no flex dispatch |

ADR-029 closes paths 1 and 3. Expected visible delta: **high**
(~485-535px of reclaimed vertical space).

### Scope boundaries

This ADR ships **single-line flex** only:

- Direction: `row` (default) and `column`
- Justify-content: `flex-start` (default), `flex-end`, `center`, `space-between`, `space-around`, `end` (modern alias)
- Align-items: `stretch` (default), `flex-start`, `flex-end`, `center`
- Flex-grow, flex-shrink, flex-basis per-item
- `gap`, `row-gap`, `column-gap`
- `flex` and `flex-flow` shorthands expanded at parse time (ADR-012 pattern)

**Not in scope (deferred)**:

- `flex-wrap: wrap` and `wrap-reverse` (multi-line flex)
- `row-reverse`, `column-reverse`
- `align-self`, `align-content`
- `order`
- Percentage `flex-basis` beyond what `parse_length` already handles
- `inline-flex` participating in inline runs (treated as block-level flex for v1; visually identical for header case)

**Deferring flex-wrap is acceptable**: the header has
`flex-wrap: wrap` but at 1774 viewport (and reasonable narrower
widths down to ~800px) all header children fit in one line. Wrap
becomes visible only at very narrow widths; capturing `nowrap`
behavior for v1 preserves the visual result at typical viewports.

## Decision

We add flex layout support for single-line containers, following
ADR-028's pattern (parse primitives + layout dispatch, split into
two implementation commits + docs).

### Display enum gains Flex and InlineFlex

```cpp
enum class Display : std::uint8_t {
    Block, Inline, None, Grid, Flex, InlineFlex
};
```

`display: flex` → `Display::Flex`.
`display: inline-flex` → `Display::InlineFlex`.
`display_of()` recognizes both.
`layout_element` dispatches both to `layout_flex_children` (v1
treats inline-flex as block-level flex; the difference is
deferred -- visually identical for the header case).

### Shorthand expansion at parse time

Following ADR-012's convention (shorthand → longhand at parse):

**`flex` shorthand**: `flex: A B C` → `flex-grow: A; flex-shrink:
B; flex-basis: C`. Also handles one- and two-value forms per CSS
spec (`flex: A` = `flex-grow: A; flex-shrink: 1; flex-basis: 0`;
`flex: A B` = `flex-grow: A; flex-shrink: B; flex-basis: 0`).

**`flex-flow` shorthand**: `flex-flow: <direction> <wrap>` →
`flex-direction: <direction>; flex-wrap: <wrap>`. Handles
single-value form too.

Both expand via `expand_shorthand()` in `stylesheet.cpp` (the
same function `grid-template` uses).

### Flex container properties parsed

From the container element's `ComputedStyle`:

- `display: flex` → container marker (already routed via `Display::Flex`)
- `flex-direction`: `row` (default), `column` (also `row-reverse`, `column-reverse` accepted by parser but layout falls back to their forward versions -- deferred)
- `justify-content`: `flex-start` (default), `flex-end`, `center`, `space-between`, `space-around`, `end`
- `align-items`: `stretch` (default), `flex-start`, `flex-end`, `center`
- `gap`, `row-gap`, `column-gap`: `<length>` (already parsed by ADR-013)
- `flex-wrap`: parsed but multi-line behavior deferred; `wrap` treated as `nowrap` for v1

### Flex item properties parsed

From each child element's `ComputedStyle`:

- `flex-grow`: `<number>` (default 0)
- `flex-shrink`: `<number>` (default 1)
- `flex-basis`: `<length>` or `auto` (default `auto`)

Per-item `align-self` deferred.

### Layout algorithm: `layout_flex_children`

```
1. Determine direction (row vs column) from container.
2. Read container's `gap` (or row-gap/column-gap) and
   justify-content / align-items.
3. For each flex item:
   a. Layout the child at (0, 0) to get natural size.
   b. Compute flex-basis:
      - explicit length → use it
      - auto → use natural main-axis size
   c. Sum flex-basis + gaps to get total main-axis used.
4. Distribute remaining main-axis space:
   - If total > container size: shrink mode.
     shrinkage_i = flex_basis_i * shrink_i / sum(basis * shrink).
     new_basis_i = basis_i - shrinkage_i * overflow.
   - If total < container size: grow mode.
     growth_i = remaining * grow_i / sum(grow).
     new_basis_i = basis_i + growth_i.
   - If total == container size: no adjustment.
5. Compute item positions along main axis per justify-content:
   - flex-start: cursor = 0
   - flex-end: cursor = remaining
   - center: cursor = remaining / 2
   - space-between: distributed between items
   - space-around: distributed around items
6. Compute cross-axis positions per align-items:
   - stretch: item height = container height (only when item has no height)
   - flex-start: cross = 0
   - flex-end: cross = container_cross - item_cross
   - center: cross = (container_cross - item_cross) / 2
7. For row: position each child at (main_cursor, cross_offset).
   For column: position at (cross_offset, main_cursor).
8. Container main-axis size = sum of adjusted basis + gaps.
   Container cross-axis size = max child cross size
   (or container_cross if any item has explicit cross size for
   stretch mode).
```

### Cross-axis stretch behavior (v1 simplification)

CSS `align-items: stretch` (default) stretches items to fill the
container's cross-axis. But our container's cross-axis size isn't
known before children are laid out (it's the max child height).

For v1: after laying out all children, compute the container's
cross-axis size as max(child cross). Then for items where
`align-items: stretch` and the child has no explicit cross-axis
size, set the child's cross to the container's. This is
single-pass and produces correct results for the header case
(children of `.mw-header` have their natural heights; the tallest
determines container height; no child is stretched because they
all have intrinsic content).

For `flex-start`/`flex-end`/`center`: position the child within
the container's cross-axis without resizing.

## Design decisions

### Why align-items but not align-self

`align-items` (18 uses) is a container property that applies to
all children uniformly. `align-self` (0 uses on Wikipedia) is a
per-item override. The header uses `align-items: center`
uniformly. Container-level alignment covers 100% of the observed
need.

### Why justify-content but not order

`justify-content` distributes items along the main axis after
sizing. `order` (0 uses) reorders items. The header uses
`justify-content: center` on the container -- the items are
already in the right DOM order.

### Why defer flex-wrap (the biggest deferral)

Multi-line flex requires a two-pass algorithm:
1. Break items into lines based on flex-basis + gaps exceeding
   container main-axis size.
2. Distribute cross-axis space across lines and per-line items.

This is roughly 2× the complexity of single-line flex. For
Wikipedia's header at typical viewports (800-1900px wide), the
header children (logo + search bar + hamburger) all fit in one
line. Visual delta from missing wrap is zero at those viewports.

At very narrow viewports (<600px), missing wrap causes the header
to overflow horizontally instead of wrapping. That's a v1
limitation, documented, and deferrable.

**Alternative considered**: always treat `flex-wrap: wrap` as
triggering multi-line. Rejected because it would add 150+ lines
for a case that doesn't manifest in the target page. Deferring is
honest scope discipline.

### Why defer row-reverse and column-reverse

0 uses on Wikipedia. Small visual cases (icon alignment, timeline
layout) but the header doesn't need them. Deferred.

### Why treat inline-flex as block-level flex

`display: inline-flex` (10 uses) means the container is
inline-level (participates in an inline run) AND its children
are flex-laid-out. Treating it as block-level flex means:

- Container position: block-level (stacks below siblings) vs.
  inline-level (flows with text)
- Container's own children: correctly flex-laid-out (both cases)

For Wikipedia's header, the `.search-toggle` element uses
`inline-flex` but is a flex item in the parent `.mw-header` flex
container. Its block-vs-inline behavior is determined by the
parent flex container's layout, not its own display. So
`.search-toggle` renders correctly either way.

For `.vector-main-menu-landmark` inside `.vector-main-menu` (also
inline-flex): its block-vs-inline behavior matters, but Wikipedia
hides this entire subtree via `visibility: hidden` in JS-off
state, so no visible difference.

**Alternative**: proper inline-flex handling. ~30 extra lines.
Deferred.

### Why shorthand expansion at parse time (matching ADR-012/ADR-028)

`flex: 1 1 100%` needs to become `flex-grow: 1; flex-shrink: 1;
flex-basis: 100%` so layout can read three simple properties
instead of parsing shorthand. Same pattern as `margin` shorthand
(ADR-012) and `grid-template` (ADR-028).

`flex-flow: row nowrap` similarly expands to two longhands.

Both handled in `expand_shorthand()` alongside `margin` and
`grid-template`.

## Deferred

- **`flex-wrap: wrap`** (multi-line flex). 0 visible impact at
  typical viewports; 3 rules use it (header + 2 in menus).
- **`row-reverse`, `column-reverse`**: 0 uses.
- **`align-self`**: 0 uses.
- **`align-content`** (multi-line cross distribution): N/A without
  wrap.
- **`order`**: 0 uses.
- **Percentage `flex-basis`**: works via `parse_length` for
  parent-width-relative percentages. Cross-axis percentages
  (parent-height) not supported -- would need to reorder layout.
- **`inline-flex` as inline-level**: treated as block-level flex
  for v1.
- **Flex item margins**: `margin` on flex items affects
  positioning. Our current `margin` handling already applies
  before flex layout begins, so this works. Documented but not
  a bug.
- **Nested flex containers**: works (each container gets its own
  layout pass), not stress-tested against Wikipedia's deep
  structure.

## Consequences

**Better:**
- **Header collapses from 585px to ~50-100px.** Article content
  moves from y ≈ 633 to y ≈ 50-150. First-fold finally shows
  actual content.
- **Largest single visual improvement of the CSS-layout arc.**
  More than grid layout (which needed header height removed
  first to be visible).
- Header dropdowns rendered as horizontal bars instead of
  vertical lists.
- `vector-column-start` h=0 likely resolves as a side effect:
  once header is flex, `vector-sticky-pinned-container`'s sticky
  behavior computes non-zero height.
- Flex infrastructure ready for future smaller layout
  refinements (TOC alignment, toolbar layout).

**Accepted limitations:**
- `flex-wrap: wrap` deferred. Narrow viewports show horizontal
  overflow on the header.
- `inline-flex` treated as block-level. No visible impact at
  typical viewports.
- `row-reverse`, `column-reverse`, `align-self`, `order` deferred.
- Multi-line flex requires a future ADR.

**Unchanged:**
- ADR-025 through ADR-028's features (external CSS loading, var
  resolution, @media filtering, grid layout). All continue to
  work; the header flex layout is the next layer above them.
- Existing block/inline/grid layout code.
- All navigation, cache, hover, paint behavior.

## Implementation plan

Three commits on `feat/flex-layout`:

1. **`Display::Flex` + `Display::InlineFlex` + shorthand expansion**
   -- new enum values, `display_of` recognition, `flex` and
   `flex-flow` shorthand expansion in `expand_shorthand()`.
   Tests: parse both display values, expand shorthands.
2. **`layout_flex_children`** -- the algorithm itself:
   `flex-basis` resolution, grow/shrink distribution,
   `justify-content` positioning, `align-items` cross-axis,
   `gap` handling. Dispatch from `layout_element` on
   `Display::Flex` / `Display::InlineFlex`. Tests: single child,
   2-child row with fixed sizes, 2-child row with
   flex-grow, `justify-content: center` / `space-between` /
   `flex-end`, `align-items: center` / `flex-start`,
   `flex-direction: column`, `gap`.
3. **Docs** -- ADR-029 status -> Implemented, README + CHANGELOG
   with measured visible delta.

## Implementation

**Implemented 2026-10-26.** Three PRs (spec, Commit 1 parser-side,
Commits 2+3 algorithm + feature-class flip) plus this closing docs
commit.

### The measured delta

`.mw-page-container-inner` y-coordinate on Wikipedia at viewport
1774 (this is the grid container; the article sits directly below):

| Stage | y-coordinate | Delta vs baseline |
|---|---|---|
| Baseline (pre-ADR-029) | 585 | -- |
| Commit 2 alone (flex, no flip) | 750 | +165 (regression) |
| **Commit 2 + Commit 3** | **59** | **-526 (payoff)** |

Header height, same viewports:

| Element | Commit 2 alone | Commit 2+3 |
|---|---|---|
| `.vector-header-container` | 727 | 36 |
| `.vector-header-start` | 711 | 5 |
| `.vector-header-end` | 20 | 20 |

The article content is above the fold for the first time in the
CSS-layout arc (ADR-025 through ADR-029). 526px of vertical
space reclaimed from baseline; the header is a compact bar
instead of a 727px stack of full-width mobile-layout blocks.

### The unplanned Commit 3 (scope addition)

The original plan for ADR-029 was **three commits**: parser,
algorithm, docs. Actual scope was **three PRs** -- spec (already
merged), parser (Commit 1), algorithm + feature-class flip
(Commits 2+3), docs (this commit). **The feature-class flip was
not in the original spec.**

It was discovered during Commit 2's verification. The layout tree
dump showed `.mw-page-container-inner` at y=750 -- **worse than
baseline**, not better. The unit tests were green (10/10); the
algorithm was correct. The regression was entirely in
Wikipedia's `<html class>` state:

- The page ships with `<html class="... vector-feature-main-menu-pinned-disabled ...">` when fetched without JS
- A small inline `<script>` flips that to `-enabled` at desktop viewports
- Without the flip, `.vector-main-menu-landmark` stays visible at 711px (the mobile hamburger menu, rendered as vertical stacked blocks)
- The hide rule is `.vector-feature-main-menu-pinned-enabled .vector-header-start .vector-main-menu-landmark { display: none }`
- Neither branch matches the mobile-default class

Commit 3 flips the class at desktop viewports in
`RenderPipeline::relayout()` (~15 lines, before the cascade).
This mimics what real Wikipedia's JS does. Idempotent round-trip:
desktop -> mobile -> desktop stays correct.

### Post-Implementation Note: language dropdown pushes article below the fold

The measured delta above records the **grid container** at y=59.
That measurement is correct. However, the visible first viewport
does **not** show the article body at y=59 — it shows the
"In other languages" panel (`#p-lang-btn`), which is **4766px tall**
in our layout, filling the visible first viewport. The article
title (`h1#firstHeading`) sits at y=2470; the article body
(`vector-body`) at y=5560. Both are below the fold.

Root cause: `#p-lang-btn` is a CSS dropdown driven by a hidden
`<input type="checkbox">`. Real Wikipedia shows its content only
on `:checked` / `:hover`. Without `:checked` support (ADR-030
territory) and without `position: absolute` (also ADR-030), the
140 `<li>` children render as visible blocks in the block flow.

**This is not an ADR-029 regression** — the grid container is at
y=59 in every measurement. It's a known limitation of the CSS
feature set the arc has implemented so far (no `position:
absolute`, no `:checked` pseudo-class, no `:has()`). It becomes
visible now that the header is compact enough to expose the top
of the article column.

**A small workaround ships alongside this note**: at desktop
viewport (>= 1120), `RenderPipeline::relayout()` injects
`#p-lang-btn { display: none }` into the cascade. This matches
what real Wikipedia with JS actually shows — the dropdown is
visible only on hover/click. The principled fix is ADR-030
(`position: absolute` and `:checked`).

Measured post-workaround state (viewport 1774):

| Element | Before workaround | After workaround | Note |
|---|---|---|---|
| `#p-lang-btn` | y=107 h=4766 | **display: none** | hidden |
| `h1#firstHeading` | y=2470 | **y=107** | top of article column |
| `vector-page-toolbar` | y=4873 h=687 | **y=147 h=687** | visible |
| `vector-body` (article) | y=5560 | **y=834** | just below fold (900) |
| Total layout height | 60612 | 55885 | -4727 |

Article title is now visible at the top of the first viewport, with
the page tabs (Article / Talk) and page actions (Read / View source
/ View history). A **second `:checked` dropdown remains inline**:
`vector-page-toolbar` (the "Tools" panel, 687px) — same root cause
as the language dropdown (no `:checked`, no `position: absolute`).
The article intro starts at y=834, just below the fold. **A
principled fix for `:checked` + `position: absolute` (ADR-030) will
collapse the toolbar too and move the intro above the fold.**

The claim "article content above the fold" holds for the **article
title and page chrome** (y=107 through y=834). The article *body*
intro is at y=834, one small scroll away. This is the closest
Wikipedia has ever rendered in HoneyBee.

### Post-ADR-030 update (2026-10-26)

The prediction in this note -- that "a principled fix for
`:checked` + `position: absolute` (ADR-030) will collapse the
toolbar too and move the intro above the fold" -- was wrong
about the **mechanism**.

ADR-030's measurement showed that the Tools panel was **not**
`:checked`-controlled. It was controlled by another
**feature-class flip** on `<html>`
(`vector-feature-page-tools-pinned-disabled` -> `-enabled` at
desktop). Same JS-set pattern as ADR-029's own main-menu flip.
The visible bug was solved by the flip, not by structural work.

`:checked` and `position: absolute` remain genuinely needed for
future features (click-to-open dropdowns, infoboxes, thumbnails),
and ADR-031 remains their home. But they weren't the mechanism
behind the Tools panel.

Measured actuals after ADR-030 (viewport 1774):

| Element | ADR-029 baseline | Actual after ADR-030 |
|---|---|---|
| `.mw-page-container-inner` y | 59 | **36** |
| `.vector-page-toolbar` height | 687 | **21** |
| `vector-body` (article) y | 834 | **145** |
| `h1#firstHeading` y | 107 | **84** |

The article intro at y=145 is deeply above the fold, not "one
small scroll away" as this note predicted. The improvement
exceeded the prediction. The prediction was honest but
pessimistic about the destination and wrong about the mechanism.

This is the third correction in ADR-029's record: (1) the
mid-cycle Commit 2 regression that motivated Commit 3, (2) the
post-merge language-dropdown overstatement, (3) this mechanism
prediction. Each correction is preserved in place rather than
rewritten -- same discipline as ADR-028's post-fix corrections
to ADR-025/026/027.

### The discipline working preventively

This is the first time the measure-predict-verify-correct
pattern **caught a regression before merge** rather than
correcting the record after (cf. ADR-028's post-hoc misdiagnosis
correction):

- Commit 2 alone would have shipped a -165px regression
- The verification step revealed it (layout tree dump on real
  Wikipedia, not just unit tests)
- The diagnostic traced the cause to `<html>` server-shipped state
- Commit 3 fixed it in the correct architectural layer
  (browser-side JS-substitute)
- The two-stage measurement is now permanent in the historical
  record

### Four small bugs fixed during the implementation

1. **`ComputedStyle::get` takes `const std::string&` not `std::string_view`** -- the `get_prop` lambda signature was adjusted.
2. **Explicit `flex-basis: 0` was being treated as natural size** -- breaks `flex: 1 1 0` semantics (both children default to their content sizes, no free space to distribute, grow does nothing). Removed the "parsed > 0" guard; explicit basis including 0 is honored.
3. **Flex test harness didn't zero the UA default `body { margin: 8px }`** -- so `x=0` assertions were off by 8px. Prepended `body,html{margin:0;padding:0}` in `flex_prep`.
4. **(Test-only) Initial `align-items` test used inline `style="height:..."`** -- which the test cascade doesn't parse, so the children were natural-height and the cross-axis offset was 0. Rewritten to use natural content heights (nested divs; `fake_measure` yields 16px per line).

### Test count

- Before ADR-029 Commit 2: 379 cases, 1575 assertions
- After Commit 2: 389 cases, 1628 assertions (+10 / +53)
- After Commit 3: 392 cases, 1637 assertions (+3 / +9)
- Full suite all pass, zero warnings

## Verification

**Unit tests** (in new `tests/test_flex_layout.cpp`):

Parse tests:
- `display: flex` → `Display::Flex` via `display_of`
- `display: inline-flex` → `Display::InlineFlex`
- `flex: 1 1 100%` → three longhands
- `flex: 1` → grow=1, shrink=1, basis=0
- `flex-flow: row nowrap` → two longhands
- `flex-flow: column` → direction=column, wrap=nowrap

Layout tests:
- 2-child row, fixed sizes 100 + 200, container 800:
  children at x=0, x=100; container width = 300
- 2-child row with flex-grow 1 each, container 800:
  each child ~400 wide
- 2-child row with gap: 16: children offset by 16
- `justify-content: center`: children centered
- `justify-content: space-between`: children at edges
- `align-items: center`: short child vertically centered
  against tall child
- `flex-direction: column`: children stack vertically with
  justify-content along vertical axis
- Single child row: child at (0, 0), container size = child
- Empty flex container: zero size

**Visual verification** (Commit 2):

| Check | Before ADR-029 | After ADR-029 |
|---|---|---|
| Header height | 585px | ~50-100px |
| Article content starts at | y ≈ 633 | y ≈ 50-150 |
| Header layout | stacked vertical blocks | horizontal bar |
| Sidebar `vector-column-start` | h=0 | likely non-zero |
| Navbox styling | preserved (ADR-027/028) | preserved |
| No crash, no regression | ✅ | ✅ |

## Acceptance criteria

ADR-029 is complete when:

- [ ] `Display::Flex` and `Display::InlineFlex` added and parsed
- [ ] `flex` and `flex-flow` shorthands expand to longhands
- [ ] `layout_flex_children` computes basis/grow/shrink/positions
- [ ] `justify-content` supports 5 values + `end` alias
- [ ] `align-items` supports 3 values + stretch default
- [ ] `flex-direction: row | column` supported
- [ ] `gap` / `row-gap` / `column-gap` applied
- [ ] `layout_element` dispatches on Flex / InlineFlex
- [ ] Unit tests cover 15+ cases
- [ ] Measured visible delta on Wikipedia (Commit 2 message)
- [ ] `main` is green: 365 + N cases, all pass, zero warnings
- [ ] ADR-029 status -> Implemented in Commit 3

**Explicit claim**: header collapses from 585px to ~50-100px.
Article content moves above the fold for the first time. Largest
single visual delta of the CSS-layout arc.
