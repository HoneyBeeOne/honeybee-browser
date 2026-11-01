# ADR-053: Honor viewport units in max-width / max-height clamps

**Status:** Accepted
**Date:** 2026-11-01
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-037 (calc() + clamp origin), ADR-039 (box model), ADR-051 (viewport units in parse_length), ADR-052 (viewport height in production)

## Context

`layout.cpp:1734-1786` contains width/height clamps from
ADR-039's box model. Four clamps exist; three already work
correctly. Two paths skip vw/vh defensively -- that guard is
now stale.

### What the code does today

| Property | vw/vh guard? | % guard? | Status |
|---|---|---|---|
| `min-width` | **No guard** | No guard | Already works |
| `max-width` | **Skips vw/vh** | No guard | Needs removal |
| `min-height` | **No guard** | Only `%` | Already works |
| `max-height` | **Skips vw/vh** | Also `%` | Needs removal (vw/vh only) |

### Why the guards were added (ADR-037 era)

At ADR-037 time, `parse_length("100vw", ctx)` returned 0
because vw/vh weren't in `parse_length`. A `max-width: 100vw`
that resolved to 0 would trigger `if (0 < content_w)
content_w = 0` and collapse the element. ADR-037 skipped
vw/vh entirely -- the correct defensive choice at the time.

### What changed since

- **ADR-051** added vw/vh/vmin/vmax to `parse_length` (and
  `parse_operand`). `parse_length("100vw", ctx)` returns
  `ctx.viewport_width`.
- **ADR-052** made `ctx.viewport_height` nonzero in production
  (from `m_scroll_window.get_height()`).

The skip guard was defensive against a bug that is now fixed.
It is stale.

### Why removal is safe (the `> 0.0f` safety net)

The current clamp code contains:

```cpp
if (max_w > 0.0f && max_w < content_w) content_w = max_w;
if (max_h > 0.0f && max_h < (cursor_y - content_y)) {
    cursor_y = content_y + max_h;
}
```

Both gate on `> 0.0f`. If `parse_length` returns 0 for any
reason (initial render with viewport_w = 0 before GTK
allocation), the clamp is skipped and no collapse happens.
The safety net ADR-037 needed is already in the code -- it
just wasn't trusted at the time.

## Decision

Remove the `find("vw") == npos` and `find("vh") == npos`
string checks from the `max-width` and `max-height` clamps.
Keep the `%` guard on `max-height` (separate concern --
`parent_content_height` threading). Keep the `> 0.0f` checks
(safety net for initial render).

## Design decisions

### Why keep the `%` guard on max-height

Percentage heights resolve to viewport **width** today (no
`parent_content_height` in `LengthContext`). A `max-height:
100%` would clamp to 1774px instead of the parent's height --
catastrophic. Deferred until ADR-040's parent_content_height
threading.

### Why keep the `> 0.0f` checks

Initial render (before GTK allocates) has viewport_w = 0 and
viewport_h = 0. `parse_length("100vw")` returns 0. `max_w >
0.0f` fails. No clamp. Content stays at natural size. On
`on_draw`, viewport dims are real, relayout, clamp fires
with real values. Same two-pass pattern as ADR-052.

Without the `> 0.0f` checks, initial render would collapse
matching elements. With them, first render is a no-op, second
render is correct.

### Why 3 new tests, not just log verification

The 644-test baseline passes today because **zero tests
exercise layout-level max/min clamps**. The skip guard has
never been tested. After removal, three tests prove the
clamp fires:

1. `width: 5000px; max-width: 100vw` at ctx.viewport_w=1024
   -> box.width = 1024
2. `height: 5000px; max-height: 100vh` at ctx.viewport_h=678
   -> box.height = 678
3. `height: 5000px; max-height: calc(100vh - 48px)` at
   ctx.viewport_h=678 -> box.height = 630

These are ADR-053's verified delta. Without them, the fix
could silently not work.

### The four clamps at a glance (post-fix)

| Property | vw/vh | % | px/em/rem |
|---|---|---|---|
| `min-width` | works | works | works |
| `max-width` | **works (new)** | works | works |
| `min-height` | works | skipped (ADR-040) | works |
| `max-height` | **works (new)** | skipped (ADR-040) | works |

## Deferred

- **`%` max-height** -- needs `parent_content_height`
  threading (ADR-040 territory)
- **`%` min-height** -- same
- **Ancestor culling** (`.vector-column-start h=0`) --
  separate culling rules; still blocks the sticky sidebar
  from showing any real content
- **`position: sticky` scroll-Y** -- separate cycle

## Consequences

**Better:**
- **`max-width: 100vw` now clamps** -- first time
- **`max-height: 100vh` now clamps** -- first time
- **`max-height: calc(100vh - 48px)` now clamps** -- the
  exact rule on `.vector-sticky-pinned-container`
- **Three new tests** exercise clamp paths for the first
  time (644 -> 647)
- **Stale ADR-037 comments removed** -- code now reflects
  current reality
- **One more prerequisite closed** on the sticky sidebar
  chain

**Accepted limitations:**
- **Zero PNG delta still** -- `.vector-sticky-pinned-container`
  is `h=0` from ancestor culling. The clamp fires but the
  element has nothing to clamp. Same category as ADR-047/048
  (behaviorally-complete, visually-zero, consumer pending)
- **`%` max-height still skipped** -- separate concern
- **Comment chain grows** -- ADR-037 comment now references
  ADR-053 for the removed guard rationale

**Unchanged:**
- ADR-005 through ADR-052 behavior
- All four clamps for non-viewport units (px, em, rem)
- `%` max-height guard preserved

**Possible regression risks (verify in Commit 1):**
- Any element currently with `max-width: Nvw` silently
  unclamped will now clamp. This includes 7 known rules from
  ADR-051. All 7 should be behaviorally equivalent or better
  -- clamped values are mathematically correct
- 644 existing tests unchanged -- no test relies on
  unclamped behavior

## Implementation plan

Two commits:

1. **Remove vw/vh guards + 3 tests**:
   - 2 lines removed from `max-width` (`find("vw")`/`find("vh")`)
   - 2 lines removed from `max-height` (same)
   - ADR-037/ADR-039 comments updated to reference ADR-053
   - 3 unit tests (see "3 tests" section)
   - `HONEYBEE_LOG=layout` verification: no crash, no
     behavior regression

2. **Docs**: ADR-053 -> Implemented, ADR-051 Post-ADR-053
   note, README row + CHANGELOG

## Verification

**Unit tests (primary):** 3 new tests as above. If they
pass, clamps fire correctly.

**Log verification (secondary):** `HONEYBEE_LOG=layout` on
Wikipedia HTML article -- viewport line unchanged, no crash,
647 total tests pass.

**PNG verification (honest zero):** before/after screenshots
identical. `max-width` on `.mw-table-of-contents-container`
now clamps but is ancestor-culled to h=0/display:none.

**Layout-tree verification:** the 3 unit tests provide the
computed box dimensions directly, proving the clamp fires.

## Acceptance criteria

ADR-053 is complete when:

- [ ] `max-width` clamp no longer skips vw/vh
- [ ] `max-height` clamp no longer skips vw/vh (still skips %)
- [ ] ADR-037/ADR-039 comments updated with ADR-053 rationale
- [ ] 3 new tests: max-width vw clamp, max-height vh clamp,
      calc(100vh - 48px) clamp
- [ ] All 647 tests pass (644 + 3)
- [ ] `HONEYBEE_LOG=layout` shows no regression
- [ ] First-viewport PNG delta: **zero**, documented honestly
- [ ] Post-ADR-053 note in ADR-051 (viewport units now
      honored by clamps)
- [ ] ADR-053 status -> Implemented
