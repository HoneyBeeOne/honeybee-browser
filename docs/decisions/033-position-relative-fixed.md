# ADR-033: Position Layout (relative + fixed)

**Status:** Implemented
**Date:** 2026-10-26
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout), ADR-031 (float), ADR-032 (visibility/opacity paint culling)

## Context

Six structural CSS features were identified as deferred after
ADR-030's measurement:

| Feature | Uses in Wikipedia CSS | First-viewport impact |
|---|---|---|
| `position` | 47 (22 relative / 16 absolute / 3 fixed / 2 sticky / 1 static) | 15 positioned elements (14 relative / 0 fixed / 1 absolute) |
| `:checked` | 14 | 0 (all dropdowns + radios, none visible without click) |
| `:not()` | 391 occurrences | ~1 (skip link, already worked around) |
| `clip` / `clip-path` | 3 (`rect(1px,1px,1px,1px)`) | 0 without proper `:not()` |
| `z-index` | 5 values used | 0 without proper `position` |
| `float` | 35 (16 right / 10 left / 7 none / 4 !important) | ADR-031 shipped v1 |

This ADR takes **relative + fixed** as the first slice of
position. `absolute` (16 uses) and `sticky` (2 uses) are deferred
— `absolute` needs containing-block tracking (which containing
element a given absolute child anchors to), which is its own ADR.

### Measurement (Wikipedia CSS, 2026-10-26)

**The 3 `position: fixed` rules** all target the same element
in different states:

```css
.mixin-toc-below-page-title {
    position: fixed; top: 12px; left: 12px;
    margin: 0; z-index: 50;
}
.client-js .vector-below-page-title .vector-page-titlebar-toc {
    position: fixed; top: 12px; left: 12px;
    margin: 0; z-index: 50;
}
.client-js.vector-feature-toc-pinned-clientpref-0
    body:not(.vector-sticky-header-visible).vector-below-page-title
    .vector-page-titlebar-toc {
    position: fixed; top: 12px; left: 12px;
    margin: 0; z-index: 50;
}
```

All three target `.vector-page-titlebar-toc` — the TOC toggle
button shown only when the TOC is "below page title" mode. None
appear in the first-viewport render at desktop viewport 1774.

**The 22 `position: relative` rules**: 0 have actual
`top`/`left`/`bottom`/`right` offsets. All 22 are
"establish positioning context" declarations only (e.g.
`.mw-page-container { position: relative; z-index: 0 }`,
`.vector-toc .vector-toc-list-item { position: relative }`,
`.cdx-radio__label { position: relative }`).

**First-viewport render scan** (viewport 1774, y<900):

| Element | position | Offsets |
|---|---|---|
| `header.vector-header.mw-header` | relative | (none) |
| `div.cdx-search-input__input-wrapper` | relative | (none) |
| `div.cdx-text-input` | relative | (none) |
| `span.cdx-text-input__icon` | **absolute** | `top=50% left=9px` |
| `nav.vector-user-links` | relative | (none) |
| `div.mw-page-container` | relative | (none) |
| `header.mw-body-header` | relative | (none) |
| `a` (article/talk tabs, 5x) | relative | (none) |
| `div.vector-body` | relative | (none) |
| `div.mw-indicators` | relative | (none) |

**14 relative, 0 fixed, 1 absolute.** The only element with real
offsets is `span.cdx-text-input__icon` (the search icon) — and
that's `absolute`, deferred.

### Honest scope call

**This ADR ships with near-zero visible delta on Wikipedia at
desktop viewport 1774.** The 22 relative rules are no-ops for
our engine (their positioning is only meaningful as context for
absolute children, which we don't yet have). The 3 fixed rules
target an element not in the first viewport.

The value is:

1. **Structural prep for ADR-034 (absolute)** — relative
   establishes positioning context; fixed shares most of the
   same machinery (out-of-flow + offset positioning)
2. **Principled code path** — currently `position` is completely
   unhandled; any element with `position: relative` or `fixed`
   in Wikipedia's CSS participates in normal block flow. This
   ADR at least parses the value and can no-op sensibly for
   relative, and take fixed out of flow correctly.
3. **Prep for `z-index`** — a future ADR needs position to
   establish stacking contexts

**Honest acceptance:** this ADR will not produce a visibly
different PNG. It establishes the position infrastructure that
ADR-034 (`absolute`) will build on.

## Decision

Add a position enum + field to `LayoutBox`, honor `relative` as
a no-op and `fixed` as viewport-anchored out-of-flow.

### 1. `Position` enum

```cpp
enum class Position : std::uint8_t {
    Static,     // default; participates in block flow
    Relative,   // in-flow; positioning context for absolute children
    Absolute,   // ADR-034; treated as Static in v1
    Fixed,      // out-of-flow; viewport-anchored
    Sticky,     // ADR-034+; treated as Static in v1
};
```

Parse from `ComputedStyle::get("position")`. Unknown values
default to `Static`.

### 2. `LayoutBox` gains `Position position` field

Default `Position::Static`. Set during `layout_element` after
`ComputedStyle` is looked up (same location where ADR-032 sets
`box.visible`).

### 3. `position: relative` — no-op in v1

Relative positioning: the element is laid out as if it were
`static`, then *shifted* by `top`/`left`/`bottom`/`right` at
paint time. Since 0 relative rules in Wikipedia's CSS have
actual offsets, the shift is always zero.

**Implementation:** set `box.position = Position::Relative`,
no other behavior. Future absolute children can use this as
their containing block (ADR-034 will read it).

### 4. `position: fixed` — out-of-flow + viewport anchor

For `position: fixed` children:

1. **Skip from parent's block flow.** The child does not
   advance `cursor_y`. Parent's height excludes the fixed child.
2. **Layout with viewport width as available content width.**
   `content_w` for a fixed child = `viewport_width` (not the
   parent's content_w), because fixed elements anchor to the
   viewport, not the containing block.
3. **Position after layout.** Read `top` / `left` offsets from
   `ComputedStyle`:
   - `top` unset → `y = 0`
   - `top` a length → `y = parse_length(top, ctx)` (viewport-anchored)
   - `left` unset → `x = 0`
   - `left` a length → `x = parse_length(left, ctx)`
   - `top: auto` → current scroll position (v1: treat as 0)
   - Percentages: parse relative to viewport (v1)
4. **Paint at that position.** Standard paint flow.

**No `right` / `bottom` in v1.** The 3 Wikipedia fixed rules use
only `top: 12px; left: 12px`. `right`/`bottom` would require
computing `viewport_width - box_width - right` — doable but
unused. Documented as deferral.

**No scroll interaction in v1.** Fixed elements anchor to the
initial viewport top-left at layout time. Scrolling doesn't
reposition them (real browsers pin them to the current scroll
position). ADR-034+ when we implement scroll-aware paint.

### 5. `absolute`, `sticky`, `static` — parsed, treated as `static`

`box.position` stores the correct enum value for future ADRs,
but layout treats them all as `static` in v1 (in-flow block).

Specifically: `span.cdx-text-input__icon` (the search icon,
`absolute top=50% left=9px`) currently renders in-flow at
approximately the right place by accident. Taking it out of flow
without a containing block would shift it unpredictably. Leaving
it as `static` in v1 is the honest conservative choice.

### 6. `z-index` — deferred

Fixed elements typically need paint-order priority (`z-index`).
The 3 Wikipedia fixed rules use `z-index: 50`. Paint order in
v1: fixed elements paint at their tree position (as if z-index
were auto).

**No visible impact today** because no fixed element is in the
first viewport. If a fixed element renders under another box in
the future, `z-index` gets its own ADR.

## Design decisions

### Why `relative` is a no-op in v1

The 22 relative rules in Wikipedia's CSS establish positioning
context but have no offsets. Relative positioning with no offsets
renders identically to static. Adding the enum value + field
captures the "context" semantics for future absolute children
without any visible change.

### Why `fixed` skips parent flow

Per CSS spec: fixed elements are out of normal flow. Their parent
doesn't advance `cursor_y`. Wikipedia's fixed TOC button example:
the article body starts below where the TOC button would have
been, not below the button's rendered position.

**Alternative** (leave fixed in flow, then shift at paint):
would produce the same visual result for the specific TOC case
but would corrupt sibling positioning (following siblings would
be pushed down by the fixed element's height). The out-of-flow
approach is correct and simple.

### Why viewport anchoring (not parent anchoring) for fixed

That's what `position: fixed` means per CSS spec: fixed relative
to the viewport, not the containing block. The layout computes
`(top_offset, left_offset)` in viewport coordinates regardless
of parent context.

**Alternative** (parent anchoring): would be `position: absolute`
behavior, which is deferred.

### Why `absolute` is parsed but treated as `static` in v1

`absolute` needs containing-block tracking: which ancestor
(with `position: relative | absolute | fixed`) is the reference
frame. Our layout tree doesn't currently track this. Adding it
is ADR-034's work (and would touch every positioned element's
layout path).

Treating absolute as static in v1 is a documented fallback — the
element renders in block flow, positioned as if `position: static`.
`span.cdx-text-input__icon` renders "approximately correct" by
accident because its in-flow position (y=22) is close to its
absolute position (`top: 50%` of the 20px input = 10px inside).
The difference is small enough to defer.

### Why not ship `right` / `bottom` for fixed

All 3 Wikipedia fixed rules use `top: 12px; left: 12px` only.
`right` / `bottom` require
`viewport_width - box_width - right` — small addition, but
unused. Deferring keeps v1 scope honest.

### Why this ADR exists even though it produces near-zero visible delta

Three reasons:

1. **Establishes position infrastructure.** Every subsequent
   position-related ADR (absolute, sticky, z-index) builds on
   this. Without relative + fixed parsing, absolute would need
   to add them too.
2. **Principled code path for `fixed`.** Even though the 3
   fixed elements aren't visible today, they will be when TOC
   toggle is implemented (a future ADR). Fixed works correctly
   from the start.
3. **Honest documentation of the "position" arc.** A reader of
   the ADR series sees that position was split across multiple
   ADRs, each shipping what it could honestly claim, each
   documenting the deferral. Same discipline as ADR-028
   (grid misdiagnosis), ADR-029 (three corrections), ADR-030
   (actuals exceeded predictions), ADR-031 (float v1), ADR-032
   (visibility/opacity, negative result).

## Deferred (ADR-034+ candidates)

- **`position: absolute`** (16 uses) — needs containing-block
  tracking. This is the one feature with a real-offset element
  in the first viewport (`span.cdx-text-input__icon`).
- **`position: sticky`** (2 uses) — needs scroll-aware layout.
- **`z-index`** (5 values) — paint ordering for positioned
  elements.
- **`right` / `bottom` offsets** for fixed and absolute.
- **Viewport scroll interaction** for fixed — repositioning on
  scroll.
- **`:not()`** — 391 occurrences; only 1 with first-viewport
  impact (skip link, already worked around).
- **`:checked`** — 14 uses; all dropdowns/radios, no
  first-viewport impact without click.
- **`clip` / `clip-path`** — 3 uses; needs `:not()` for effect.
- **Paint-authoritative culling for `position`** — currently
  paint draws every visible box; future ADRs may need to skip
  out-of-viewport positioned boxes (v1: no skip).

## Consequences

**Better:**

- **Position infrastructure exists.** `relative` and `fixed`
  parse, store on `LayoutBox`, and `fixed` correctly exits
  parent flow with viewport anchoring.
- **Principled code path for 3 fixed elements** in Wikipedia's
  CSS (currently they leak into block flow).
- **Prep for ADR-034 (absolute).** The `Position` enum + field
  + paint path are in place.
- **Prep for z-index.** Stacking context needs position.
- **No visible regression.** All 22 relative rules remain
  visually no-op; 3 fixed elements weren't visible pre-ADR-033
  and aren't visible post-ADR-033 either.

**Accepted limitations (honest, documented):**

- **No visible delta today.** This ADR does not produce a
  visibly different PNG.
- **`absolute` treated as `static`.** The search icon renders
  in-flow (approximately correct by accident; correct
  positioning requires ADR-034).
- **`sticky` treated as `static`.**
- **No `right` / `bottom` support.**
- **No scroll interaction for fixed.** Fixed elements anchor to
  initial viewport top-left; scrolling doesn't pin them.
- **No z-index.** Fixed elements paint at tree position.

**Unchanged:**

- All prior ADRs (018-032).
- Layout algorithm, cascade, float handling.
- Visible PNG appearance (no regression, no improvement).

## Implementation plan

Two commits on `feat/position-relative-fixed`:

1. **`Position` enum + `LayoutBox::position` field + parse +
   layout skip for `fixed` + viewport anchoring** — add enum
   in `layout_box.hpp`; parse from `ComputedStyle::get("position")`
   in `layout_element`; in `layout_block_children`, detect
   `position == Fixed` children, layout them separately, position
   at viewport anchor, and append WITHOUT advancing `cursor_y`.
   Add 5-6 tests (position parsing for each enum value, fixed
   out-of-flow with top/left anchor, fixed without offsets at
   (0,0), relative no-op).
2. **Docs** — ADR-033 status → Implemented, Post-ADR-033 note in
   ADR-031 recording zero-visible-delta honestly, CHANGELOG,
   README row.

## Implementation

**Implemented 2026-10-26.** Two PRs (spec #174, impl #175).

### Measured outcome: zero visible delta, exactly as predicted

The spec's honest caveat said this ADR would produce zero visible
delta on Wikipedia at desktop 1774. The pre-merge check confirmed
it:

    cmp -s /tmp/wiki-adr032-final.png /tmp/wiki-post-adr033.png
    -> PNGs are BYTE-IDENTICAL

Byte-identical PNG before and after. The prediction was correct,
to the byte. This is the first ADR in the arc where the visual
prediction was fully validated by exhaustive measurement.

### What shipped

**Position enum** (5 values):

    Static (default)
    Relative  -- stored; no-op in v1; context for ADR-034's
                 absolute children
    Absolute  -- stored; treated as static in v1; proper
                 handling in ADR-034
    Fixed     -- honored by layout; out-of-flow + viewport anchor
    Sticky    -- stored; treated as static in v1

**LayoutBox::position** field (default Static).

**layout_element** parses position from ComputedStyle's
`position` property.

**layout_block_children** honors fixed:

- Lays out the fixed child at (0, 0)
- Reads `top` / `left` from ComputedStyle, parses against
  viewport coordinates
- Shifts subtree to (top, left) via shift_subtree_x and
  shift_subtree_y
- Appends WITHOUT advancing cursor_y -- parent height excludes
  the fixed child

**No further behavior for relative / absolute / sticky in v1.**
Documented fallbacks.

### Why the visible delta is zero (the honest reason)

The three `position: fixed` rules in Wikipedia's CSS all target
`.vector-page-titlebar-toc` -- a TOC toggle button that only
appears when the TOC is in "below page title" mode. Not present
in the first viewport at desktop 1774.

All 22 `position: relative` rules are context-only (zero actual
`top` / `left` / `bottom` / `right` offsets). Their layout
behavior is identical to `position: static` in our engine.

The one `position: absolute` element with real offsets in the
first viewport -- `span.cdx-text-input__icon` at
`top: 50% left: 9px` -- is treated as `static` in v1. Its in-
flow position (approximately y=22 in the search bar) is close
to its absolute position by accident. Shifting it now without
containing-block tracking would be unpredictable.

**Not a flaw. An honest precondition.** The infrastructure is
in place; ADR-034 will activate it.

### Structural value delivered

1. **Position infrastructure exists.** Before this ADR, the
   `position` property was completely unhandled -- elements
   with `position: relative | fixed | absolute | sticky` all
   participated in normal block flow. Now parsed, stored, and
   honored for `fixed`.

2. **Principled code path for the 3 fixed elements** in
   Wikipedia's CSS. Currently these leak into block flow (the
   `.vector-page-titlebar-toc` button would appear inline if it
   were ever in the first viewport). After this ADR, fixed is
   out-of-flow with viewport anchoring.

3. **Prep for ADR-034 (absolute).** The `Position` enum,
   `LayoutBox::position` field, and paint flow are all in
   place. ADR-034 adds containing-block tracking.

4. **Prep for z-index.** Stacking contexts require position.
   Future ADR.

### Test count

- Before ADR-033 Commit 1: 414 cases / 1710 assertions
- After Commit 1: **421 cases / 1737 assertions** (+7 / +27)
- Full suite all pass, zero warnings

### v1 limitations shipped

- `right` / `bottom` for fixed: unused in Wikipedia's CSS
  (all 3 rules use `top:12px; left:12px`). Deferred.
- No scroll interaction for fixed: anchored to initial viewport
  top-left. Scroll-aware positioning deferred.
- `absolute` / `sticky` treated as `static`: needs containing-
  block tracking (ADR-034) and scroll-aware layout (ADR-034+).
- No `z-index` for fixed: fixed elements paint at tree position.
  Deferred.

## Verification

**Unit tests** (new `[layout][position][adr-033]` tag):

- `position: relative` → `box.position == Position::Relative`
- `position: static` → `box.position == Position::Static`
- `position: absolute` → `box.position == Position::Absolute`
  (stored but layout treats as static)
- `position: fixed` → `box.position == Position::Fixed`
- Fixed without offsets → box at `(0, 0)`, out of parent flow
  (parent's height excludes the fixed box)
- Fixed with `top: 12px; left: 12px` → box at `(12, 12)`,
  out of parent flow
- Following sibling after a fixed child is at normal flow
  position (fixed didn't push it down)
- `position: relative` doesn't change layout (no-op)

**Visual verification** (viewport 1774, Wikipedia HTML):

| Check | Expected |
|---|---|
| Article containers unchanged | ✅ (y=36, 84, 145 same) |
| Infobox position unchanged | ✅ (x=914 y=497 w=358) |
| Intro paragraph unchanged | ✅ (y=497 w=590) |
| Fixed elements not in first viewport | ✅ (none in y<900) |
| PNG identical to pre-ADR-033 | ✅ (zero visible delta) |

**Honest claim**: this ADR does not change the rendered PNG.
It establishes the position infrastructure that future ADRs
build on.

## Acceptance criteria

ADR-033 is complete when:

- [ ] `Position` enum added to `layout_box.hpp`
- [ ] `LayoutBox::position` field (default `Static`)
- [ ] `layout_element` parses position from ComputedStyle
- [ ] `layout_block_children` skips `fixed` from flow + anchors at viewport
- [ ] `relative` is a no-op (in-flow)
- [ ] `absolute` / `sticky` parsed but treated as `static`
- [ ] 5-7 unit tests added and passing
- [ ] Visual verification: PNG unchanged (documented)
- [ ] ADR-031's Post-ADR-033 note records zero-visible-delta
- [ ] `main` is green: 414 + N cases, all pass, zero warnings
- [ ] ADR-033 status → Implemented in Commit 2

**Explicit claim**: `relative` and `fixed` parse and store on
`LayoutBox`. `fixed` elements exit parent flow and anchor at
viewport `(top, left)` offsets.

**Honest caveat (repeated for emphasis)**: **this ADR produces
zero visible delta on Wikipedia today.** The 3 fixed rules
target an element not in the first viewport. The 22 relative
rules have no offsets and behave identically to static. The one
absolute element with real offsets (`span.cdx-text-input__icon`)
is treated as static in v1. The value is structural — it
prepares ADR-034 (absolute) and establishes the position
pattern.
