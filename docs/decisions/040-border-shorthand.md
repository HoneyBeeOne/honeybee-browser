# ADR-040: Border Shorthand Expansion

**Status:** Accepted
**Date:** 2026-10-28
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-012 (shorthand expansion: margin/padding/border-width), ADR-039 (box model completion — border shorthand limitation surfaced)

## Context

ADR-012 shipped shorthand expansion for `margin`, `padding`,
and `border-width` longhand. It did **not** expand the 3-value
`border` shorthand (`border: 1px solid #ccc`) or per-side
forms (`border-top`, `border-right`, `border-bottom`,
`border-left`).

ADR-039 Commit 2's test fix surfaced this limitation:
initial tests used `border: 5px solid black` and got zero
border width because ADR-012 doesn't expand that form. Tests
were corrected to use `border-width: 5px`, but the real-world
consequence is that **every Wikipedia element styled with
`border: 1px solid X` currently has no visible border**.

### Measurement (Wikipedia CSS, 2026-10-28)

**Border shorthand usage**:

| Form | Uses | Example |
|---|---|---|
| `border: <width> <style> <color>` (3-token) | 23 | `1px solid #a2a9b1` |
| `border: 0` (1-token zero) | 26 | `border: 0` |
| Per-side `border-top/right/bottom/left` | 8 | `1px solid transparent` |
| Standalone `border-width` (already expanded) | 7 | — |
| Standalone `border-color` (does nothing without width) | 63 | — |
| Standalone `border-style` (does nothing without width) | 4 | — |

**First-viewport impact** (viewport 1774):

| Element | Border declaration | Visible effect |
|---|---|---|
| `form.cdx-search-input` | `border: 1px solid #a2a9b1` | Search field gains 1px gray border |
| `h1#firstHeading` | `border-bottom: 1px solid #a2a9b1` | Article title gains bottom underline |
| `div.vector-column-start` | `border-top: 1px solid transparent` | Transparent — no visual |
| `div.vector-column-end` | `border-top: 1px solid transparent` | Transparent — no visual |

**2 genuinely visible first-viewport changes**. Both expected on real Wikipedia (search field has a border; article title has the heading underline). First-viewport PNG will differ — that's the delta, measurable and verifiable.

### What's NOT in scope (deferred to later ADRs)

**`parent_content_height` threading** — 5 uses of `height:
100%` in Wikipedia CSS (all on `html`/`body`), zero visible
delta from applying them (page already has outer scroll via
ADR-021's `ScrolledWindow`). Moderate signature churn
(2 `LengthContext` construction sites, 8 `layout_element`
call sites). Ships as its own **ADR-041**.

**`overflow: auto | scroll` subsystem** — 4 uses total
(`auto` × 2, `hidden auto` × 2). Nested-overflow infrastructure
doesn't exist: no overflow detection, no scrollbar painting,
no wheel-event routing. Outer page scroll works (ADR-021), but
inner scroll needs a subsystem. Ships as its own **ADR-042**.

**`aspect-ratio`** — 0 uses in Wikipedia CSS. Deferred
indefinitely.

**`border-radius`** — 10 uses in Wikipedia CSS, needs Cairo
arc rendering (rectangle-only boxes today). Own ADR when
appetite exists.

**`border-style: dashed | dotted`** — needs paint changes to
draw dashes/dots. Not required for the target page (Wikipedia
borders are `solid` or `0`). Deferred.

## Decision

Expand the `border` shorthand (and per-side forms) at
**parse time**, per ADR-012's existing pattern:

### `border: <width> <style> <color>` → 3 longhands

Parse 3 whitespace-separated tokens. Each is one of:

- **Width**: length (`Npx`, `Nem`, etc.) or one of `thin | medium | thick` (unused in Wikipedia) or `0`
- **Style**: `none | solid | dashed | dotted | double | ...` — **any style except `none` is treated as visible** (renders as solid in v1; paint changes for dashed/dotted deferred)
- **Color**: hex, named, or `rgb()`

Emit three longhands: `border-width`, `border-style`, `border-color` at the same specificity/importance.

### `border: 0` → zero out

Single-token `border: 0` → `border-width: 0`. Style and color
irrelevant when width is 0. Same for `border: none` (equivalent
per CSS spec but unused in Wikipedia).

### `border: <width> <color>` (2-token, no style)

Per CSS spec, missing style defaults to `none`, which means
invisible. Wikipedia does not use this form; we accept it and
emit `border-width` + `border-color` (which combine to invisible
because style defaults to `none` in the paint path). No
behavioral change.

### Per-side forms

`border-top: <width> <style> <color>` → `border-top-width`,
`border-top-style`, `border-top-color`. Same for right/bottom/left.
`border-top: 0` → `border-top-width: 0`.

### Layout behavior

Layout already reads `box.border[i]` from `border-width` longhand
or `border-width` shorthand. Once `border: 1px solid #ccc`
expands to `border-width: 1px`, layout picks up 1px on all four
sides automatically. **No layout code changes.** Paint already
draws borders from `LayoutBox::border` and `border_color`.
**No paint code changes** either — this is pure cascade/parse
work.

### `border-style` semantics in v1

Layout doesn't currently read `border-style`. Paint draws any
nonzero `LayoutBox::border[i]` as a filled rectangle in
`border_color`. So:

- `border: 1px solid red` → 1px border, red, drawn solid ✓
- `border: 1px dashed red` → 1px border, red, drawn **solid** (dashed deferred)
- `border: 1px none red` → parse emits `border-width: 0` (per spec: `border-style: none` makes width 0 for rendering)

To make `border-style: none` zero out width, the expansion
needs to read style and conditionally emit `border-width: 0`
when style is `none`. Documented below.

## Design decisions

### Why parse-time expansion, not layout-time reading

ADR-012's pattern: `margin: 10px 20px` becomes four
`margin-*-` longhands at parse time. `border: 1px solid red`
should become `border-width: 1px; border-style: solid;
border-color: red` the same way. Layout reads only `border-width`
longhand, so this is a pure cascade fix.

**Alternative** (read `border` shorthand at layout time):
would require `parse_border_shorthand()` in `layout.cpp` and
duplicate reading logic. Worse. Rejected.

### Why render `border-style: dashed/dotted` as solid in v1

Paint draws filled rectangles. Dashed rendering requires:
- Splitting each border segment into dashes (`cairo_set_dash`)
- Stroke width calculation
- Corner handling

~40 lines. **Zero Wikipedia uses of dashed/dotted in the target page** (all borders are `solid` or `0`). Ships when a real page needs it. The **parse-side** recognizes `dashed`/`dotted` as valid styles (doesn't drop the shorthand rule), just renders them visually solid. Documented limitation.

### Why `border-style: none` zeroes width

Per CSS spec §8.5.3: "If border-style is none or hidden, the
used border-width is 0." So:

```css
.x { border: 5px none red }  /* no visible border */
```

Our expansion needs to check style and emit `border-width: 0`
when style is `none`. Otherwise paint would draw a 5px red
rectangle where the browser draws nothing.

Wikipedia doesn't use `border: Npx none`, but the parse must be
correct because a future ruleset might.

### Why `parent_content_height` gets its own ADR

Threading `parent_content_height` is **signature churn with
zero visible delta** — the signature change touches 10 sites
(2 `LengthContext` construction sites + 8 `layout_element`
call sites), and only `height: 100%` on `html`/`body` uses
percentages, which already produce correct visible output
because outer page scroll handles the extra length.

Bundling it here would make the PR a **mixed risk profile**:
border shorthand has visible delta (needs PNG-verify), threading
has zero delta (test-only). ADR-039 taught us that mixed-risk
bundling requires the two-commit isolation. Rather than repeat
that ceremony for a zero-delta feature, ship it as its own ADR.

### Why `overflow: auto | scroll` gets its own ADR

4 uses in Wikipedia CSS. But nested-overflow requires:

- Per-box overflow detection during layout
- Scrollbar rendering (paint)
- Wheel event routing to nearest overflow-ancestor under cursor
- Scroll offset tracking in paint
- `LayoutBox::scroll_x/y` fields

~150-250 lines. Bundling with 100-line border shorthand would
be a **3-in-1 PR** spanning parse, layout, paint, and event
plumbing. Cleaner as its own ADR when the scroll interaction
becomes a priority.

## Consequences

**Better:**

- **2 visible first-viewport changes**: `form.cdx-search-input`
  gains its 1px gray border, `h1#firstHeading` gains its
  bottom underline. Both correct per Wikipedia's CSS.
- **~30 elements across the whole page gain borders** that
  were previously invisible (tables, banners, tab bars,
  message boxes — all currently drawn without borders).
- **Completes ADR-012's shorthand coverage** for the border
  family. Only `border-style` rendering (dashed/dotted) and
  `border-radius` remain.
- **Zero code changes in layout.cpp or paint.cpp** — pure
  cascade/parse fix. Lowest-risk ADR of the recent batch.
- **Unblocks `border-radius`** as a future ADR — once borders
  render, rounding them becomes a paint-layer addition.

**Accepted limitations (honest, documented):**

- **`border-style: dashed | dotted` renders as solid**. Zero
  uses in target page; visible-rendering deferred.
- **`border-radius` not expanded** (separate ADR, needs Cairo
  arc support).
- **`border-collapse`, `border-spacing`** (table-specific,
  separate category, not this ADR).
- **`border: <width> <color>` 2-token form** — CSS spec
  defaults to `border-style: none`, our expansion emits
  `border-width: <width>` and `border-color: <color>` but
  style default-to-none isn't explicitly written, so paint
  would draw solid. **Documented as deviation from CSS spec
  for the 2-token case only.** Wikipedia doesn't use it.

**Unchanged:**

- ADR-012's `margin`, `padding`, `border-width` shorthand
  expansions
- Layout reading of `border-width`
- Paint drawing of `LayoutBox::border` and `border_color`
- All prior ADR behavior

## Implementation plan

Two commits on `feat/border-shorthand`:

1. **Parse-side expansion** — extend `try_expand_grid_template`'s
   sibling `expand_shorthand` (from ADR-012/ADR-028) with
   `try_expand_border` and `try_expand_border_side`. Handle
   `border: <width> <style> <color>`, `border: 0`,
   per-side forms. ~80-100 lines in `stylesheet.cpp`. 8-10
   tests. **Zero layout/paint changes.**
2. **Docs** — ADR-040 status → Implemented, Post-ADR-040
   note in ADR-012 (shorthand family completed for border),
   CHANGELOG, README.

## Verification

**Unit tests** (`[css][border][adr-040]` tag):

- `border: 1px solid #ccc` → 3 longhands with right values
- `border: 0` → `border-width: 0` (single longhand)
- `border: none` → `border-width: 0`
- `border: 1px dashed red` → 3 longhands, style=dashed
- `border: 5px none red` → `border-width: 0` (per CSS spec)
- `border-top: 2px solid blue` → 3 per-side longhands
- `border-left: 0` → `border-left-width: 0`
- `!important` preserved through expansion
- Malformed `border: foo bar baz` → rule dropped
- Cascade: last expansion wins when both `border` and `border-width` present (source order)

**Visual verification** (viewport 1774, Wikipedia HTML):

| Check | Before | After |
|---|---|---|
| `form.cdx-search-input` | no border | **1px gray border** |
| `h1#firstHeading` | no bottom border | **1px gray underline** |
| First-viewport PNG | — | **DIFFERS** (2 visible borders) |
| Article body layout | — | unchanged (borders add ~2px per side, well within margins) |
| Total layout height | 62283 | **possibly +2 to +4** (borders add to content height) |
| Full suite green | ✓ | ✓ |

**Render and inspect PNG before merge** — ADR-039's lesson codified. Any visible border appearing where it shouldn't (e.g. on body, on the header itself) must be caught before merge.

## Acceptance criteria

ADR-040 is complete when:

- [ ] `border: <width> <style> <color>` expands to 3 longhands
- [ ] `border: 0` expands to `border-width: 0`
- [ ] `border: <width> none <color>` zeroes width (CSS spec §8.5.3)
- [ ] Per-side `border-top/right/bottom/left` expand correctly
- [ ] `!important` preserved through expansion
- [ ] Malformed forms drop the rule
- [ ] 8-10 unit tests added and passing
- [ ] Search input and heading underline appear in PNG
- [ ] No unintended borders (body, header background, etc.)
- [ ] Post-ADR-040 note in ADR-012 (shorthand family completed)
- [ ] `main` green: 494 + N cases, all pass, zero warnings
- [ ] CI green (PR + post-merge)
- [ ] ADR-040 status → Implemented in docs commit

**Explicit claim**: `border: 1px solid X` now produces 1px visible
borders everywhere Wikipedia uses it. First-viewport PNG gains
2 visible borders (search field, heading underline). No layout
or paint code changes — pure cascade fix.

**Honest caveats**:

1. **`border-style: dashed | dotted` renders solid** (deferred).
2. **`border-radius` deferred** (own ADR).
3. **`border-collapse` / `border-spacing`** deferred (table category).
4. **`border: <width> <color>` 2-token** renders solid instead of
   invisible (CSS spec deviation, 0 uses in target).
5. **`parent_content_height` → ADR-041** (zero visible delta,
   threading churn).
6. **`overflow: auto | scroll` → ADR-042** (needs scroll subsystem).
7. **`aspect-ratio` → deferred indefinitely** (0 uses).
