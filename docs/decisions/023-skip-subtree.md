# ADR-023: Skip-Subtree Optimization for Hover

**Status:** Accepted
**Date:** 2026-10-24
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-022 (targeted cascade), ADR-018 (state pseudo-classes), ADR-010 (cascade)

## Context

ADR-022 delivered targeted cascade for hover: on a hover change,
only the elements whose state changed are re-cascaded, and the
rest of ComputedStyleMap is cached. Measured result: link hovers
dropped from ~700ms to 14-25ms -- a 30-50x improvement.

ADR-022's measurement also surfaced a remaining gap: 4 of 71
observed hover transitions (~6%) took ~900ms. Root cause (per the
ADR-022 Implementation section): `cascade_subtree(el)` re-cascades
**every element in `el`'s subtree**. When the hovered element is
a structural `<div>` containing most of the article, that means
re-cascading thousands of elements -- same cost as full cascade.

But there's a subtlety worth articulating precisely, because it
determines the design:

**:hover's semantics mean most subtree elements don't actually
change style.** Per ADR-018's matcher, `:hover` is true for an
element X iff X is the hovered element OR X is an ancestor of the
hovered element. Hovering a wrapper `<div>` makes the div (and
its ancestors) "hovered"; **its descendants are not hovered**.

So when hovering a wrapper div, does any `<a>` inside it match
`a:hover`? No: `a:hover` requires the `<a>` to be the hovered
element OR have the hovered element as an ancestor. The hovered
element is the `<div>`, which is a descendant-descendant of the
`<a>`, not an ancestor. So `a:hover` doesn't match the `<a>`.

The correct re-cascade set for a wrapper-div hover is: **elements
in the subtree that match some `:hover`-containing selector as
the anchor, when the anchor itself is hovered**. For UA stylesheet
`a:hover`, that's just the `<a>` elements in the subtree. ~50-100
out of ~5,000 descendants. Cascading those is ~8-20ms instead of
~900ms.

`cascade_subtree(el)` currently cascades all 5,000. ADR-023 fixes
this by precomputing which elements are hover-sensitive and
skipping the rest.

## Decision

We add hover-sensitivity detection + filtered subtree cascade,
with fallback for stylesheets the detection can't safely handle.

### Three levels, detected per stylesheet

For each selector in each stylesheet, classify by the position of
`:hover` within the selector:

| Level | Detection | Hover behavior |
|---|---|---|
| **0** | No selector contains `:hover` | Skip all hover work entirely |
| **1** | All `:hover`-containing selectors have `:hover` in the anchor compound (rightmost part), and no other part contains `:hover` | Precompute `is_hover_sensitive`; on hover, cascade only flagged elements |
| **2** | Any `:hover`-containing selector has `:hover` in a non-anchor part (`div:hover > a`, `div:hover a`), or has `:hover` in multiple parts | Fall back to unconditional `cascade_subtree` (current ADR-022 behavior) |

Overall level = **max** over all selectors in all sheets. If any
selector is Level 2, we run Level 2. If no selector has `:hover`,
we run Level 0.

**Current stylesheets**: UA has only `a:hover` (Level 1). Wikipedia's
inline CSS has no `:hover` rules. Overall: **Level 1**.

### Precompute `is_hover_sensitive`

For Level 1, at every `relayout()` (which already does full
cascade), compute:

```
is_hover_sensitive(X) = true iff
  exists a selector S in (ua_sheet, author_sheet) such that:
    - S contains PseudoHover in its anchor compound
    - X matches S's anchor compound when PseudoHover is treated
      as true and other state pseudos (:link, :visited) evaluated
      against current RenderState
```

For UA's `a:hover`: `is_hover_sensitive(X)` iff X is an `<a>`
element (the anchor compound's only non-pseudo simple is `Type(a)`).

Store as `std::unordered_set<const dom::Element*> hover_sensitive_`,
populated in `relayout()`, cleared in `render()` (alongside the
existing cache invalidation from ADR-022).

### Filtered subtree cascade

`RenderPipeline::restyle()` Level 1 path, on hover change:

```
if prev_hovered_ != state_.hovered_element:
    for el in {prev_hovered_, state_.hovered_element}:
        if el is nullptr: continue
        walk el's subtree:
            if hover_sensitive_.count(descendant) > 0:
                erase_subtree(descendant, styles_cache_)
                cascade_subtree(descendant, ...)
                (mark dirty)
        erase_subtree(el, styles_cache_)  # the el itself, if sensitive
        cascade_subtree(el, ...)
    if any_dirty:
        propagate_inheritance(*doc_, styles_cache_)
        restyle_walk(layout_root_, styles_cache_)
        prev_hovered_ = state_.hovered_element
```

Note: the walk is over the dirty element's descendants, checking
`hover_sensitive_` membership per node. When the hovered element
is a leaf `<a>`, this walk is trivial (no descendants). When the
hovered element is a wrapper `<div>`, the walk visits ~5,000
descendants but only cascades the ~50-100 that are `<a>` -- that's
the whole win.

**Level 2 fallback**: unchanged from ADR-022 -- full
`cascade_subtree(el)` on the dirty element. Correct but not fast.
Documented limitation.

### Correctness invariant (extended)

ADR-022's invariant was: **cache after targeted restyle == cache
after full cascade, byte-for-byte**. ADR-023's precompute means
the set of re-cascaded elements differs (fewer), so we must
verify:

**New invariant**: after ADR-023's filtered restyle, the cache
equals what a full cascade would produce. Verified via the same
test pattern as ADR-022's cache-equality test.

This requires the precompute to be **conservative**: when in doubt,
mark an element as hover-sensitive (over-cascade is safe; under-cascade
produces stale styles). Level 2 fallback is one extreme of this
conservatism; for Level 1, the anchor-compound check is exact.

## Design decisions

### Why the "does this element match when hovered?" check is cheap at render, expensive at hover

Naively, "which elements in the subtree are hover-sensitive?" could
be answered on-demand at hover time by running `selector.matches(el, state)`
per element per rule. But `matches()` is what the cascade costs
itself -- running it just to *check* is ~50% of the cost of just
cascading. Saving 50% is not the 30-50x win we need.

**Precomputing at render time amortizes the check across all future
hovers.** The check runs once (7,871 elements x 1 hover rule =
0.08ms overhead per render), the result is stored, and every
subsequent hover pays only the O(subtree) pointer-membership walk.

### Why detection is per-selector, not per-element

An element doesn't know whether it's hover-sensitive -- that
depends on the rules. Detection must inspect rules. A stylesheet
where all `:hover` selectors are anchor-position (Level 1) is
safe to precompute exactly; one with `:hover` on ancestors (Level 2)
requires the matcher's full parent-chain walk per element per rule
to determine sensitivity, which costs more than it saves.

Splitting detection by level means we can be aggressive when it's
safe and conservative when it isn't.

### Why the detection is a heuristic, not a proof

Level 1's correctness depends on the assumption: **if `:hover` is
only in the anchor compound, then an element's hover-related style
change requires that element to be the hovered element (or an
ancestor)**. Per ADR-018's matcher, `:hover` on an element X is
true iff X is the hovered element or an ancestor of the hovered
element. So for X to newly match `S` (where `:hover` is in the
anchor compound of S), X must be:
- (a) the hovered element (X's own `:hover` becomes true), or
- (b) a descendant of the hovered element (an ancestor of X becomes
  hovered)

Case (a) -- X is the hovered element -- is what Level 1 precomputes
for. Case (b) -- X is a descendant of the hovered element -- is
what requires walking the subtree. Both are handled: precompute
flags X, and the hover-time walk cascades flagged elements in the
hovered element's subtree.

What Level 1 misses: an element Y that isn't hover-sensitive by
itself but becomes styled because an **ancestor matches a compound
`:hover` rule**. That's exactly what Level 2 catches and defers.

### Why re-cascade of `<a>` elements is still needed on wrapper-div hover

Wait -- doesn't the analysis above say `<a>` elements in a wrapper
`<div>` don't match `a:hover` when the `<div>` is hovered? Yes.
So why re-cascade them?

Because they might match under other rules. If an author stylesheet
had `div:hover a { color: red }` (which is **Level 2**, so we
fall back to full subtree cascade anyway), the `<a>` would change
color when the parent `<div>` is hovered. Level 1 by definition
has no such rules, so this case doesn't apply.

But wait -- what about `a:hover` when `<a>` itself is hovered?
Then `<a>` is the hovered element. When the mouse moves from
`<a>` to a **blank space** in `<div>` (un-hover), we need to
re-cascade the `<a>` (un-hover transition). The `<a>` is in the
dirty subtree of the *previously* hovered element... wait no, the
`<a>` is not a descendant of the current hovered element (blank
space). Let me re-check.

Actually `prev_hovered_` and `state_.hovered_element` are the two
dirty elements. If we hover from `<a>` (prev) to null (current),
we process `prev_hovered_ = <a>` and `state_.hovered_element = nullptr`.
The `<a>` is the dirty element; we walk its subtree (empty), cascade
the `<a>` itself (if hover-sensitive -- it is). Correct.

If we hover from `<div>` (prev) to null (current): we process `<div>`
and null. Walk `<div>`'s subtree, cascade any hover-sensitive
descendants. In UA-only case, no descendants are hover-sensitive
for `a:hover` when `<div>` (not `<a>`) is hovered -- wait, but the
precompute flagged `<a>` elements as hover-sensitive. So we'd
re-cascade them even though their style didn't change. Minor
waste (5,000 pointer lookups + ~50-100 cascade calls for `<a>`s).

Hmm, that's still ~8-20ms. Better than 900ms but not the theoretical
~1ms of "no changes needed." Acceptable -- the win is the 30-50×
reduction, not perfection.

Alternative: track which element *was* hovered (prev_hovered_) vs
which *is* hovered (state_.hovered_element), and only re-cascade
elements whose `:hover`-matching state actually transitioned. That's
a further optimization, deferred (would need per-element hovered
state tracking).

### Level 2 fallback: why not try harder

Level 2 has compound selectors like `div:hover > a`. To precompute
`is_hover_sensitive(a)` = "would `<a>` match `div:hover > a` for
some hovered_element?", we'd need to enumerate all hovered_element
values and check each -- equivalent to the matcher cost per element
per hovered_element, which is intractable.

Alternative: precompute the set of "elements that could be made
hover-relevant by an ancestor hover" -- that's a superset of the
anchor-based precompute (include descendants of anchors). But for
UA-only `a:hover`, all descendants of `<a>` are also relevant if
they inherit color. So the superset would be "descendants of `<a>`"
which is small (usually empty). It's tractable but complex to get
right.

**Deferred**: Level 2 optimization (proper descendant-of-anchor
precompute for compound selectors). Current author CSS (Wikipedia
has no `:hover`, so Level 1; test pages have only `a:hover` from UA)
never triggers Level 2. Deferring costs nothing today.

### Cache invalidation

`hover_sensitive_` is recomputed in `relayout()`, cleared in
`render()` (same lifecycle as `styles_cache_` from ADR-022). No new
invalidation paths. `record_visit()` also invalidates it implicitly
via the next `render()`, since `:link`/`:visited` state affects
which elements are "currently a hoverable anchor" -- actually no,
`:link`/`:visited` aren't part of the anchor compound for `a:hover`.
`:link`/`:visited` in the anchor compound would make it Level 2
(they're state pseudos too, but they don't prevent the precompute
-- we'd evaluate them against `state_` at precompute time). This
is documented but not currently exercised.

## Deferred

- **Level 2 (compound `:hover`)** optimization. Requires
  "descendants of anchors" precompute or similar. Current stylesheets
  don't use it; deferring costs nothing.
- **`prev_hovered_`-aware filtering**: only re-cascade elements
  whose hover-matching state actually transitioned (see Design
  Decisions). Would drop the wrapper-hover 8-20ms to ~1ms.
  Requires per-element hovered-state tracking. Deferred; current
  is already under perceptual threshold.
- **Cross-hover-transition optimization**: caching the last hover
  state's matching set so we only diff. Same complexity as above.
- **`:hover:visited` and `:hover:link` handling**: Level 1 precompute
  would need to evaluate `:link`/`:visited` at precompute time
  (against `state_.visited_urls`). The current detection treats
  "any state-pseudo in anchor compound" as Level 1 (safe because
  precompute uses `state_`), but this isn't exercised in tests
  yet. Add tests when a real use case appears.
- **Release build measurements**: current baseline 900ms is debug.
  Release may be <50ms already (30-50x speedup would mean we're
  optimizing 5ms vs 10ms). Not measured; documented as unknown.

## Consequences

**Better:**
- Wrapper-element hovers on Wikipedia drop from ~900ms to an
  expected ~8-20ms (measured: TBD in Commit 2). This closes the
  last gap ADR-022 flagged.
- Level 0 (no `:hover` rules at all) becomes free: hover work
  skipped entirely. Small pages with no hover CSS get sub-frame
  restyle.
- Link hovers unchanged (~14-25ms) -- no regression on the 99%
  case.
- Precompute overhead is negligible: ~0.08ms per render on Wikipedia.

**Accepted limitations:**
- Level 2 (compound `:hover`) is unchanged -- still ~900ms on
  pathological stylesheets. Documented; no current use case.
- Wrapper-div hover still does full O(subtree) walk even though
  most descendants don't need cascading. ~1ms walk overhead,
  acceptable.
- The precompute "over-cascades" when a hovered element's
  descendants are flagged but their style didn't actually change
  (UA-only case). Minor waste; further optimization deferred.

**Unchanged:**
- `:link`, `:visited`, `:hover` semantics -- no matcher changes.
- Cache equality invariant (targeted == full cascade) -- preserved,
  tested.
- ADR-022 algorithmic guard (apply_rule call count) -- still holds
  for the pure link-hover case.
- Page cache (ADR-020), history (ADR-019), navigation behavior.

## Implementation plan

Three commits on `feat/skip-subtree`:

1. **Hover-sensitivity precompute + detection** -- new
   `renderer/css/hover_sensitivity.{hpp,cpp}` with:
   - `HoverLevel detect_level(ua_sheet, author_sheet)` -- returns
     Level 0/1/2
   - `std::unordered_set<const Element*> compute_hover_sensitive(doc, ua_sheet, author_sheet, state)` -- for Level 1 only
   - Unit tests: level detection (0, 1, 2 cases), precompute
     (matches what manual matching would produce)
2. **RenderPipeline integration** -- `hover_sensitive_` member,
   computed in `relayout()`, cleared in `render()`. `restyle()`
   uses filtered walk for Level 1, falls back to unconditional
   `cascade_subtree` for Level 2. Extended cache-equality test.
   Algorithmic guard: apply_rule call count for wrapper-div hover
   drops from ~millions to ~thousands.
3. **Docs** -- ADR-023 status -> Implemented, measured distribution
   in Implementation section, README + CHANGELOG updates.

## Verification

Unit tests:

- `detect_level`: Level 0 (empty sheet, no `:hover`), Level 1
  (`a:hover` only), Level 2 (`div:hover > a`, `div:hover a`,
  `a:hover b:hover`).
- `compute_hover_sensitive`: for `a:hover` UA sheet and a document
  with `<a>` elements, returns exactly the `<a>` element set.
- **Cache equality** (extended): after filtered Level 1 restyle,
  `styles_cache_` equals full cascade byte-for-byte (same pattern
  as ADR-022).
- **Algorithmic guard**: for a wrapper-div hover on a synthetic
  document with 1 wrapper + 100 `<a>` descendants + 1000 `<span>`
  siblings, `apply_rule_call_count` after restyle is ~O(100) (only
  the `<a>` descendants), not O(1100) (all subtree elements).

Visual/measurement verification on Wikipedia:

- Link hover latency unchanged (~14-25ms).
- Wrapper-div hover latency: measured, target <50ms (expected
  ~8-20ms). Goes into Implementation section as measured number.
- Hover color correctness unchanged.

## Acceptance criteria

ADR-023 is complete when:

- [ ] Level detection + precompute implemented with unit tests
- [ ] `restyle()` uses filtered path for Level 1, falls back to
      unconditional for Level 2
- [ ] Cache-equality invariant test extended and passing
- [ ] Algorithmic guard for wrapper hover passes (<20,000 apply_rule
      calls, target ~200-1,000)
- [ ] Measured wrapper-div hover on Wikipedia <50ms (target ~8-20ms)
- [ ] Link hover unchanged (~14-25ms, no regression)
- [ ] `main` is green: 260 + N cases, all pass, zero warnings
- [ ] Measured numbers documented in this ADR's Implementation section

**Explicit claim**: wrapper-element hovers on Wikipedia drop from
~900ms to sub-perceptual. This closes ADR-022's only remaining
performance gap for the current stylesheet ecosystem (UA `a:hover`
only, no author `:hover` rules). Level 2 (compound `:hover` in
author CSS) is documented as deferred.
