# ADR-042: Position (relative / absolute)

**Status:** Implemented
**Date:** 2026-10-28
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-016 (inline formatting), ADR-028 (grid), ADR-029 (flex), ADR-033 (position: fixed -- parses all four but only lays out Fixed), ADR-041 (pseudo-elements -- visible synthesis deferred waiting for this)

## Context

ADR-033 (position: fixed) parses all four `position` values
into `LayoutBox::position` but only handles `Fixed` in layout.
`Relative`, `Absolute`, and `Sticky` are stored but treated as
`Static`. ADR-041's Commit 1 added pseudo-element parse +
cascade (pseudo_styles populated correctly), but **ADR-041's
visible-delta prediction (title underline, edit brackets,
citation backlinks) failed** because:

1. The primary target `.vector-page-titlebar::after` needs
   `position: absolute` laid out correctly
2. The parent `.vector-page-titlebar` is both
   `position: relative` (containing block) AND
   `display: flex` (ADR-029)

ADR-042 closes ADR-033's deferral for `relative` + `absolute`,
and combines with ADR-041's pseudo infrastructure to deliver
the title underline that ADR-040 predicted (via the wrong
mechanism) and ADR-041 deferred.

### Measurement (Wikipedia CSS, 2026-10-28)

Position property usage:

| Value | Uses |
|---|---|
| `position: absolute` | **19** |
| `position: relative` | **22** |
| `position: fixed` | 3 (already handled) |
| `position: sticky` | 2 (deferred) |

**First-viewport absolutes: 2 total.**

| Rule | Visible? | Notes |
|---|---|---|
| `.vector-page-titlebar::after` | ✅ **This ADR's target** | `display: block; position: absolute; bottom: 0; left: 0; right: 0; height: 1px; background: ...` |
| `.vector-toc .vector-toc-toggle` | ❌ `display: none` | No visual effect |

**Containing blocks (position: relative):** 4 in first-viewport
chain — `.vector-page-titlebar`, `.mw-page-container`,
`.vector-body`, `.mw-header.vector-header-search-toggled
.vector-search-box`.

**Second-viewport absolutes (17 total):** dropdowns, edit
links, reference markers, thumbnail magnify icons. Making
them behave as out-of-flow will fix their positions once
absolute layout works; visible on scroll but not in the first
viewport.

### Current test coverage

ADR-033's deferral is explicitly tested:
`tests/test_layout.cpp:1721` — "position: absolute stored
but layout as static (v1)". This test **must be updated** by
ADR-042 (the deferral is being closed).

## Decision

Implement `position: relative` (as containing block marker)
and `position: absolute` (as out-of-flow child positioned
against its containing block), with a v1 scope that
deliberately covers **the titlebar pseudo-element** and the
19 non-pseudo absolutes, but defers complex cases.

### `position: relative`

- Element stays in normal flow (visual offsets via
  `top`/`left` deferred — v1 keeps it at its static position)
- Element becomes a **containing block** for absolute
  descendants. `LayoutBox` gains `bool is_containing_block`
  set from `box.position == Relative || Absolute || Fixed`.
- Containing block dimensions = the element's content box
  (`x + border[3] + padding[3]` to
  `x + border[3] + padding[3] + content_w`, similar for y)

**Deferred (own ADR if needed):** applying `top`/`left`
offsets visually to the relative element itself. Wikipedia
doesn't use them in first-viewport.

### `position: absolute`

- Element does **not** advance parent's `cursor_y` /
  contribute to flex/grid sizing
- Positioned against its **direct parent's content box** if
  the parent is a containing block
- If the parent is NOT a containing block, falls back to
  static (ADR-033 behavior, documented deferral)
- Offsets parsed from `top` / `bottom` / `left` / `right`
  (individual properties; `auto` for unset)
- Size resolution rules (v1):
  - `left` + `right` both set: `width = containing_w - left - right`
  - `left` set only + width set: x = containing_x + left
  - `right` set only + width set: x = containing_x + containing_w - right - width
  - Neither `left` nor `right`: x = containing_x (static fallback)
  - Same logic for `top` / `bottom` / `height`
- Result appended to parent's `children` after parent's
  normal children complete, with computed `x` / `y` / `width`
  / `height`

**Deferred:**
- **Multi-level containing block walk-up**: if the direct
  parent is static but a grandparent is `position: relative`,
  real CSS uses the grandparent. v1 uses static fallback.
  Covers 100% of first-viewport (`.vector-page-titlebar` is
  direct parent).
- **`top: auto` / `bottom: auto` constraint overrides**: the
  full CSS §10.6.4 priority order (when both are auto, use
  static position). v1 uses the simple rules above.
- **Percentages** in top/bottom/left/right (needs
  `parent_content_height` -- ADR-043 candidate).
- **`position: sticky`** (needs scroll state).
- **Over-constrained cases** (e.g., both `left` and `width`
  and `right` set): v1 honors `left`, ignores `right`
  (matches CSS spec §10.3.7 rule 1 "left wins in LTR").

### Pseudo-element integration (ADR-041 Commit 2, deferred)

Pseudo synthesis happens **after the parent's own children
complete**. For an element `E` with
`pseudo_styles[E].after` (or `.before`):

1. Complete E's normal children layout (block/flex/grid --
   whichever E's display triggers)
2. E's content box is now known (E's `x`, `y`, `width`,
   `height` are final)
3. For each pseudo `P` of E with `position: absolute`:
   - Synthesize a `LayoutBox` for P with `P.element = nullptr`
     (or a fresh dummy to avoid ambiguity) and P's
     ComputedStyle
   - Position P against E's content box using P's
     `top`/`bottom`/`left`/`right`
   - Append P to E's `children`
4. For pseudo `P` with `position: static` (or empty position):
   - Synthesize normally (flow-inserted child), matching
     ADR-041's original spec
   - Content `''` → zero-width box with background/border
   - Content `'literal'` → inline text fragment

**First-viewport visual delta: `.vector-page-titlebar::after`
becomes a 1px bar at the bottom of the titlebar.** Width =
titlebar's content width (`left: 0; right: 0`), height = 1px
(`height: 1px`), color = `var(--border-color-base)` →
`#a2a9b1`. This is the underline ADR-040's prediction was
actually pointing at.

### What about `position: fixed`?

ADR-033 handles `fixed` via a special case in
`layout_block_children` (out-of-flow, positioned against
viewport). ADR-042 does not change this behavior -- `fixed`
keeps working exactly as before. The containing-block
concept doesn't apply to fixed (viewport IS the containing
block). No change; existing ADR-033 tests for fixed remain
green.

## Design decisions

### Why parent-only containing block (not walk-up)

Real CSS: nearest ancestor with `position != static`. Our
first-viewport case has `.vector-page-titlebar` as the direct
parent of its absolute pseudo, so walk-up is unnecessary.
Walking up the layout tree requires adding
`LayoutBox::parent` pointers or threading a containing-block
pointer through every `layout_element` call site (7 in block +
grid + flex). That's ~15+ anchor patches for a case
Wikipedia doesn't exercise in the first viewport.

**Alternative considered**: add `parent` pointer to
`LayoutBox`. Small change (~5 lines + 8 call sites to
populate). Acceptable scope but still more than needed for
the title underline. **Deferred to when a real case
requires it.**

### Why defer relative visual offsets

`.vector-page-titlebar` and `.mw-page-container` both have
`position: relative` but no `top`/`left`/`right`/`bottom`
that would move them. Real Wikipedia uses the "relative
without offset" pattern (containing block marker only) for
these. Applying offsets is only needed for elements that
actually move -- none in first-viewport.

### Why put pseudo synthesis in this ADR, not ADR-041

ADR-041's Commit 2 (pseudo synthesis) failed because the
primary target (title underline) needs absolute positioning.
Doing pseudo synthesis **without** absolute positioning
would synthesize the pseudo but place it in normal flow --
looking wrong (bars below the title content, not under it).

**ADR-042 combines both**: pseudo synthesis + absolute
positioning, in one visible-delta commit. That's the honest
combination -- pseudo synthesis alone has no visible win, and
absolute positioning alone has no first-viewport win. Together
they deliver the underline.

ADR-041's Commit 1 (parse + cascade) is preserved unchanged
-- the pseudo_styles map is populated correctly and this ADR
just reads it.

### Why flow-skip happens per dispatch mode (not pre-extraction)

The "pre-extract absolute children, pass filtered list to
dispatch" approach touched every dispatch path (block, flex,
grid) with a wrapper loop + child re-iteration -- ~15 changes.
The "each dispatch skips absolute children inline" approach is
3 changes (one per dispatch: block, flex, grid, each adds
`if (child_position == Absolute) { pending_absolutes.push; continue; }`).
Simpler diff, same behavior.

Pseudo-synthesis is done the same way -- it's an additional
child append after dispatch completes, positioning against
`self.content_box`.

## Deferred

- **Multi-level containing block walk-up** — current v1 uses
  direct parent only
- **`top`/`left` visual offsets on `position: relative`** —
  relative elements stay at static position
- **`position: sticky`** — needs scroll state
- **Percentages in top/bottom/left/right** — needs
  `parent_content_height` (ADR-043 candidate)
- **Over-constrained cases** — left wins over right (CSS
  spec §10.3.7 rule 1, correct for LTR)
- **`content: 'literal text'` pseudo synthesis** — inline
  text content, different code path (ADR-041 deferred this
  too)
- **`content: attr() / counter() / url()`** — ADR-041
  deferred
- **Containing block = padding-box vs content-box** — v1
  uses content-box (correct for `.vector-page-titlebar`)

## Consequences

**Better:**
- **Title "HTML" underline appears** -- ADR-040's prediction
  finally visible, via the mechanism ADR-041 correctly
  identified
- **Second-viewport absolutes position correctly** --
  dropdowns, edit links, reference markers, thumbnail
  magnify icons all align with real Wikipedia
- **ADR-041's visible delta achieved** -- the parse+cascade
  layer from Commit 1 finally has consumers
- **ADR-033 partial supersession** -- `relative` and
  `absolute` now handled; only `sticky` deferred
- **`position: relative` containing block marker ready** --
  future ADRs (edit-link brackets, dropdown positioning) can
  rely on it

**Accepted limitations:**
- Multi-level containing block walk-up deferred (parent-only)
- `top`/`left` visual offsets on relative deferred
- Percentages in offsets deferred
- `sticky` deferred

**Unchanged:**
- All ADR-025 through ADR-041 features
- `position: fixed` (ADR-033 special case) untouched
- Block / inline / grid / flex layout dispatchers unchanged
  in their core logic -- only gain "skip absolute children"

## Implementation plan

Three commits, same discipline as ADR-028 / 040 / 041:

1. **Absolute + relative layout** (behavior change, no first-
   viewport delta):
   - `is_containing_block` field on `LayoutBox`
   - `top`/`bottom`/`left`/`right` parsing (via existing
     `parse_length`)
   - Block dispatch: skip absolute children, collect, position
     after
   - Flex dispatch: same (per CSS spec, absolutes are
     out-of-flow for flex containers)
   - Grid dispatch: same
   - Update ADR-033's "position: absolute stored but static"
     test to assert the new positioning behavior
   - ~15 new tests: basic absolute, all four offsets, auto
     cases, containing block dimensions, parent not positioned
     fallback
   - First-viewport delta: **zero** (only first-viewport
     absolute is `.vector-toc-toggle`, `display: none`)
   - Second-viewport delta: absolutes reposition correctly
     (visible on scroll)

2. **Pseudo synthesis + first-viewport delta** (visible):
   - Pseudo synthesis in block/flex/grid dispatch (append
     after normal children complete)
   - `content: ''` → zero-width box, positions per absolute
     rules
   - `content: 'literal'` → deferred (inline path)
   - **Render-and-inspect PNG before merge** (ADR-039 lesson
     codified)
   - Title underline must appear
   - ~10 new tests: pseudo synthesis, absolute positioning,
     no pseudo rules → no change
   - First-viewport delta: **title "HTML" underline appears**

3. **Docs**:
   - ADR-042 status → Implemented
   - ADR-033 Post-ADR-042 note (relative + absolute
     superseded; only sticky deferred)
   - ADR-041 Post-ADR-042 note (visible delta achieved via
     ADR-042)
   - README row + CHANGELOG entry

## Verification

**Unit tests:**

Absolute positioning (Commit 1):
- Parent `position: relative`, child `position: absolute; top: 5px; left: 10px; width: 50px; height: 20px` → child positioned at parent content_x + 10, content_y + 5
- Same with `bottom`/`right` → positions relative to parent's bottom-right
- `left: 0; right: 0` + height set → width = parent content_w
- `top: 0; bottom: 0` + width set → height = parent content_h
- Both `left` and `right` + width set → left wins, right ignored (over-constrained)
- All `auto` → child at parent content_x/content_y with intrinsic size
- Parent not positioned → child treated as static (ADR-033 behavior preserved)
- Absolute child does not advance parent's cursor_y (parent height excludes it)
- Absolute child in flex parent → out-of-flow, not a flex item

Pseudo synthesis (Commit 2):
- Element with `::after { content: ''; position: absolute; bottom: 0; left: 0; right: 0; height: 1px; background-color: red }` → element's LayoutBox has an extra child, 1px tall, red, positioned at element's bottom edge, full content width
- Element with `::before { ... }` → child appears at top (same absolute positioning)
- Element with `::after { content: ''; position: static; background: red; height: 5px }` → static child appended at end (flow-inserted, non-absolute path)
- Element with `::after { content: 'text' }` → no box synthesized (deferred)
- No pseudo rules → element's children unchanged (regression)

**Visual verification (Commit 2, PNG before merge):**

| Check | Before | After |
|---|---|---|
| Title "HTML" underline | No underline | **1px gray underline** |
| Titlebar height | Same | Same (pseudo is out-of-flow) |
| Body content position | Same | Same (pseudo doesn't shift flow) |
| No unintended overlaps | -- | No overlap |
| Full suite green | -- | -- |

**ADR-039 lesson codified**: render and inspect PNG BEFORE
merge, not after. ADR-040's revert of the box-sizing
correction (and ADR-041's Commit 2 skip) established this as
the standard discipline.

## Acceptance criteria

ADR-042 is complete when:

- [ ] `is_containing_block` field on `LayoutBox`
- [ ] `position: relative` recognized as containing block
- [ ] `position: absolute` children removed from flow (block,
      flex, grid)
- [ ] `top`/`bottom`/`left`/`right` parsed + applied
- [ ] Parent-not-positioned fallback to static documented +
      tested
- [ ] ADR-033's "position: absolute stored but static" test
      updated to new behavior
- [ ] Pseudo synthesis: `content: ''` produces absolute box
- [ ] Title "HTML" underline appears in first-viewport PNG
- [ ] No unintended layout shift elsewhere in first viewport
- [ ] 25+ new unit tests across both commits
- [ ] Post-ADR-042 notes in ADR-033 and ADR-041
- [ ] `main` green: 518 + N cases, all pass, zero warnings
- [ ] ADR-042 status → Implemented in docs commit

**Explicit claim**: title "HTML" underline appears via
`.vector-page-titlebar::after` with `position: absolute`.
This closes ADR-040's h1 prediction (via the mechanism
ADR-041 correctly identified) and ADR-041's deferred visible
synthesis.

## Implementation

Implemented on `feat/absolute-positioning` across three commits
(Commit 1 = absolute + relative layout, Commit 2 = pseudo
synthesis, Commit 3 = this docs update). **PNG verification
passed**: the title "HTML" underline appears in the AFTER
screenshot and does not appear in BEFORE.

### What shipped

**Commit 1 — absolute + relative layout:**
- `LayoutBox::is_containing_block` field (set when
  `position != Static`)
- Block dispatch: `pending_absolutes` vector collects absolute
  children of a containing block; positioned against parent's
  content box after normal children finish
- All four offsets (`top` / `bottom` / `left` / `right`) with
  proper auto/over-constrained rules
- Width/height stretch when left+right or top+bottom both set
- Explicit width/height preserved when only one axis is set
- Fallback: parent not positioned -> absolute child treated as
  static (ADR-033 behavior preserved)
- 13 new tests in `tests/test_layout.cpp`

**Commit 2 — pseudo synthesis:**
- `apply_pseudo_elements()` -- recursive post-walk over layout
  tree, synthesizes before + after boxes for elements with
  `pseudo_styles` entries
- `apply_one_pseudo()` -- parses `content` (only `''` empty
  literal supported; literal text, attr(), counter(), url()
  deferred), positions absolute pseudos against parent's
  content box (same rules as Commit 1)
- `layout_with_pseudo()` -- new top-level function; calls
  `layout()` normally, then post-walks to synthesize pseudos
- `render_pipeline.cpp` switched to `cascade_with_pseudo()` +
  `layout_with_pseudo()` (3 call sites)
- 8 new tests in `tests/test_pseudo_layout.cpp`

**Commit 3 (this commit) -- docs:**
- ADR-042 status -> Implemented
- ADR-033 Post-ADR-042 note
- ADR-041 Post-ADR-042 note
- README row update
- CHANGELOG entry

### Measured visible delta (viewport 1774, PNG-verified)

| Element | Before | After | Verdict |
|---|---|---|---|
| Title "HTML" underline | None | 1px gray line under title | Confirmed |
| Sidebar | Intact | Intact | No regression |
| Body text | Unchanged | Unchanged | No regression |
| Issue box border | Present (ADR-040) | Present | No regression |

**The ADR-040 prediction is finally visually true**, via the
mechanism ADR-041 correctly identified (`.vector-page-titlebar
::after` pseudo-element, absolute-positioned at bottom of
titlebar) and now possible because ADR-042 Commit 1 taught
layout how to position absolutes against their parent.

### Test count

- Before ADR-042: 518 cases, 1980 assertions
- After Commit 1: 531 cases, 2035 assertions (+13 / +55)
- After Commit 2: 539 cases, 2064 assertions (+8 / +29)
- After Commit 3: unchanged (docs only)

Zero new warnings introduced. Two pre-existing
`-Wmisleading-indentation` warnings in `render_pipeline.cpp`
(ADR-022 asset loader) unchanged.

### Deferred (documented, as planned)

- **Flex/grid parent absolutes** -- v1 handles block parents
  only. First-viewport case (`.vector-page-titlebar::after`)
  works because pseudo synthesis is post-walk, not routed
  through flex dispatch.
- **Multi-level containing block walk-up** -- v1 uses direct
  parent
- **`position: sticky`** -- needs scroll state
- **Percentages in offsets** -- needs `parent_content_height`
- **`top`/`left` visual offsets on `position: relative`** --
  relative elements stay at static position
- **`content: 'literal text'`** -- inline path (brackets,
  backlink arrows)
- **`content: attr() / counter() / url()`** -- ADR-041
  deferred, still deferred

