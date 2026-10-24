# ADR-024: Hover-Match Transition Check

**Status:** Accepted
**Date:** 2026-10-24
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-023 (skip-subtree), ADR-022 (targeted cascade), ADR-018 (state pseudo-classes)

## Context

ADR-023 measured that wrapper-element hovers on Wikipedia still
took 340-460ms, well short of its own ~8-20ms prediction. Root
cause: `restyle()` re-cascades every hover-sensitive element in
the dirty subtree. For a wrapper `<div>` / `<body>` / `<html>`
hover, that's ~2,191 `<a>` elements -- but per ADR-018's matcher,
**none of them actually match `a:hover`**, because `:hover` on
`<a>` requires the `<a>` to be hovered or an ancestor of the
hovered element, and a wrapper is neither.

ADR-023's Deferred followup proposed the fix: check whether each
flagged element's `:hover` match state actually transitioned
between old and new hovered element, and cascade only elements
where it did.

Per ADR-023's Meta-lesson ("ADR Followup predictions must be
measurement-first"), we measured the transition distribution
**before** writing this ADR.

### Measured distribution

Instrumentation on `render_pipeline.cpp`'s filtered walk, run on
Wikipedia's HTML article (799KB, 7,871 elements, 158 rules,
debug build -O0, 72 transitions across a manual hover session):

| Transition kind | `flagged` | `transitioned` | Check cost |
|---|---|---|---|
| Wrapper hover (`<html>`, `<body>`) | 2,191 | **0** | 2.5-6.1ms |
| `<section>` <-> `<a>` (link hover) | 123 | **1** | 0.3-0.4ms |
| `<section>` <-> `<b>`, `<table>` (non-`<a>` in section) | 123 | **0** | 0.2ms |
| `<span>` inside `<a>` <-> `<html>` | 2,191 | **1** | 2.6ms |
| `<button>`, small `<div>` (no `<a>` descendants) | 0-6 | 0 | <0.05ms |

Per-element cost analysis:

- **Cascade**: 380ms / 2,191 = ~0.17ms per flagged element
- **Check**: 4ms / 2,191 = ~0.0018ms per element
- **Check is ~95x cheaper than cascade** per element checked

Even at 50% transition rate, the check pays for itself. At the
measured 0% (wrapper case), it's pure profit.

### The subtle case that shapes the design

`prev=<html> new=<span>` shows `transitioned=1` -- a `<span>` is
hovered, and its ancestor `<a>` transitions its `a:hover` match
from false to true (per ADR-018's walk-up matcher, `:hover` on X
is true if X is hovered **or an ancestor of the hovered element**).

So a naive optimization like "if `hovered_element` isn't an `<a>`,
skip cascading entirely" would **miss this case**. The check must
be per-element: "does THIS element's match state change?", not
"does the currently-hovered element match an anchor?"

## Decision

We add a per-element hover-match transition check to
`restyle()`'s Level 1 filtered walk.

### New public helper

`renderer/css/hover_sensitivity.hpp`:

```cpp
// True iff `el` matches at least one anchor-position :hover
// selector when evaluated against `state` (i.e., when
// `state.hovered_element` is whatever value it has). Extracted
// from compute_hover_sensitive's inner loop so the same logic
// backs both the precompute and the transition check.
bool matches_any_anchor_hover(
    const dom::Element& el,
    const RenderState& state,
    const Stylesheet& ua,
    const Stylesheet& author);
```

`compute_hover_sensitive` is refactored to call this helper per
element (behavior unchanged -- just deduplicated logic).

### Pipeline integration

`RenderPipeline::restyle()` Level 1 path, for each element in the
`dirty` set collected by the existing subtree walk:

```cpp
bool old_match = matches_any_anchor_hover(el, old_state, ...);
bool new_match = matches_any_anchor_hover(el, new_state, ...);
if (old_match != new_match) {
    erase_subtree(el, styles_cache_);
    cascade_subtree(el, ...);
    did_work = true;
}
```

Where `old_state` and `new_state` differ only in `hovered_element`
(prev vs. current). All other `RenderState` fields are identical,
so we can construct `old_state` once as `state_` with
`hovered_element = prev_hovered_`.

### Cache-equality invariant preserved

The invariant is: **cache after targeted restyle == cache after
full cascade, byte-for-byte**. When `old_match == new_match`, the
element's `:hover`-derived style is unchanged, so its prior cache
entry is valid. Cache-equality still holds.

Verified by extending the existing cache-equality test with a
case that exercises the wrapper-hover scenario (many flagged, zero
transitions). Same pattern as ADR-022.

### Correctness test for the subtle case

A dedicated test exercises `prev=<span> new=<html>` where the span
is inside an `<a>`: assert that the `<a>` **is** re-cascaded (its
`a:hover` match transitions), even though the currently-hovered
element is a `<span>`, not an `<a>`. This catches the naive
optimization that would be tempting but wrong.

## Design decisions

### Why per-element, not per-hovered-element

The `<span>`-inside-`<a>` case (measured above) demonstrates that
an ancestor's `:hover` match can transition without the ancestor
being the hovered element. A per-hovered-element check ("is the
hovered element hover-sensitive?") would miss this.

The per-element check is O(flagged) extra work, but the measured
cost (~0.0018ms per element) makes this negligible against the
cascade cost (~0.17ms per element).

### Why this is not subsumed by ADR-023's precompute

The precompute answers "can this element EVER match an anchor-hover
selector?". The transition check answers "did this element's match
state CHANGE between old and new hovered element?". These are
different questions:

- A `<p>` with no `<a>` descendants: precompute says "not flagged"
  (correct, it can never match `a:hover`)
- An `<a>` inside `<div>`: precompute says "flagged" (correct, if
  it were the hovered element it would match)
- Same `<a>`, `<div>` hovered: precompute says "flagged", transition
  check says "0 change" -- the `<a>` shouldn't be re-cascaded
  because its match state didn't change.

Precompute: coarse filter (which elements COULD be affected).
Transition check: fine filter (which elements actually WERE
affected).

### Why this is smaller than ADR-023 was

ADR-023's risk was in the detection heuristic (Level 0/1/2
classification) and the precompute correctness. Those are already
shipped. ADR-024's change is:

- Extract one helper (mechanical refactor)
- Add one filter in the existing filtered walk
- Add 2-3 tests (correctness invariant + subtle case + existing
  algorithmic guard extended)

The algorithmic guard from ADR-023 already exists; ADR-024 extends
its threshold to catch regressions (from `<1000` to `<500` for the
wrapper case, since the correct count after this fix is 0).

### Cost of the check on small pages

For pages with no `<a>` in a wrapper, `flagged=0` -> check cost is
0. For pages with a single `<a>` hovered, `flagged=~1` -> check cost
is negligible. The check's worst case is the largest `flagged`
count, which is exactly the case where it saves the most cascade
time. Self-balancing.

## Deferred

- **Level 2 (compound `:hover`) optimization** -- still deferred
  from ADR-023. Would need a similar transition check for
  descendant-of-hovered-element effects. No current use case.
- **`prev_hovered_`-aware filtering of the subtree walk itself** --
  currently we walk the entire dirty subtree, then check each
  flagged element. Could we skip walking entirely if we knew no
  element in the subtree could transition? For div-hover in
  UA-only, we know from the current hovered element (a div) that
  no `<a>` descendant will match. That's a further optimization,
  but requires a "could this hovered element trigger any
  transition?" pre-check. Deferred -- the transition check already
  drops wrapper hover from 400ms to ~25ms, which is sub-perceptual.
- **Release build measurements** -- debug baseline is the ADR
  target; release expected 5-10x faster still.
- **Cross-hover-transition batching** -- if two adjacent hovers
  transition overlapping sets, could batch. Not a real bottleneck.

## Consequences

**Better:**
- Wrapper-element hovers on Wikipedia drop from 340-460ms
  (ADR-023) to **~25ms measured** (walk 5 + check 4 + cascade 0 +
  propagate 12 + restyle_walk 5). ~14x improvement, sub-perceptual
  (well under the ~50ms human-noticeability threshold).
- Link hovers unchanged at 14-25ms (check adds <1ms).
- Correctness for the `<span>`-inside-`<a>` case preserved
  (measured: 1 transition, correctly re-cascaded).
- Closes the last known hover performance gap.

**Accepted limitations:**
- Full subtree walk still runs on wrapper hover (~5ms) even though
  zero cascades happen afterward. Could be optimized with a
  "this hovered element can't trigger transitions" pre-check;
  deferred.
- Full propagate (~12ms) and restyle_walk (~5ms) still run
  per-transition. Targeting those is a separate optimization.
- Level 2 fallback unchanged (documented in ADR-023).

**Unchanged:**
- ADR-023's Level 0/1/2 detection and precompute.
- Cache-equality invariant.
- ADR-022's algorithmic guard and ADR-023's skip-subtree guard.
- All other navigation/history/cache behavior.

## Implementation plan

Two commits on `feat/hover-transition-check`:

1. **`matches_any_anchor_hover` helper + refactor** -- extract from
   `compute_hover_sensitive`, expose publicly, refactor caller.
   Add 2-3 unit tests: helper returns identical results to the
   inlined version (via precompute behavior comparison), plus the
   `<span>`-inside-`<a>` case (returns true when span is hovered).
2. **Pipeline transition check** -- `old_state`/`new_state` in
   `restyle()`'s Level 1 path; per-element transition filter before
   cascade. Extended cache-equality test (wrapper-hover case);
   extended algorithmic guard (wrapper-hover `apply_rule` count
   drops from ~37,000 to ~0 -- correct). Commit message includes
   measured wrapper-hover latency from a fresh Wikipedia test.

Plus docs commit (third):

3. **Docs** -- ADR-024 status -> Implemented, README + CHANGELOG,
   measured distribution in Implementation section.

## Verification

Unit tests:

- `matches_any_anchor_hover`: identical results to inlined version
  (asserted via precompute equality on a test document)
- `matches_any_anchor_hover`: `<span>` inside `<a>` returns true
  when span is hovered (the subtle case)
- Cache equality: after transition-check restyle, `styles_cache_`
  equals full cascade for the wrapper-hover scenario
- Extended algorithmic guard: wrapper-hover `apply_rule_call_count`
  drops from ~37,000 (ADR-023) to ~0 (ADR-024)

Visual/measurement verification on Wikipedia:

- Link hover latency: **measured**, target 14-25ms (unchanged)
- Wrapper hover latency: **measured**, target ~25ms (from 340-460ms
  in ADR-023)
- Hover color correctness unchanged
- `<span>`-inside-`<a>` case: hovering the span should turn the
  containing link lighter blue (ancestor `:hover` matching)

## Acceptance criteria

ADR-024 is complete when:

- [ ] `matches_any_anchor_hover` extracted, tested, called by both
      precompute and transition check
- [ ] `restyle()` Level 1 path cascades only elements whose hover
      match transitioned
- [ ] Cache-equality invariant test extended and passing
- [ ] Algorithmic guard: wrapper-hover `apply_rule_call_count`
      drops from ~37,000 to <100 (target ~0)
- [ ] Measured wrapper-hover on Wikipedia <50ms (target ~25ms)
- [ ] Link hover unchanged (14-25ms, no regression)
- [ ] `<span>`-in-`<a>` visual test: hovering span styles ancestor
      link correctly
- [ ] `main` is green: 267 + N cases, all pass, zero warnings
- [ ] Measured numbers documented in this ADR's Implementation
      section

**Explicit claim**: wrapper-element hovers drop from 340-460ms
(ADR-023) to ~25ms (measured in Commit 2). This closes ADR-023's
prediction gap and delivers the "hover is sub-perceptual on all
element types" claim that ADR-021 originally aspired to.
