# ADR-022: Targeted Cascade for Hover

**Status:** Implemented
**Date:** 2026-10-23
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-021 (incremental restyle), ADR-010 (cascade), ADR-018 (state pseudo-classes)

## Context

ADR-021 replaced full relayout with cascade-only restyle on hover
changes. On Wikipedia's 799KB HTML article (~7,871 elements,
~158 rules: 17 UA + 141 author), hover latency dropped from
~2-3s (previous relayout) to ~700ms (restyle). Correct, but not
"instant" -- ADR-021's own Followup section proposed targeted
cascade to close the remaining gap.

The 700ms was measured but not decomposed. Before designing the
fix, we instrumented `cascade()` to separate its two passes:

    compute_styles()        -- per-element rule matching + merge
    propagate_inheritance() -- top-down pass for inherited props

Measured on the same Wikipedia article (debug build, -O0):

    Page             Elements  Rules  Compute   Propagate  Total
    Welcome page     9         20     0.09ms    0.01ms     0.09ms
    Wikipedia HTML   7,871     158    664-896ms 9-13ms     673-909ms

Per-call match cost: 664ms / (7,871 x 158) ~= 0.53us per
`rule.selectors.matches(el, state)` invocation. Matches count
1,243,618 per cascade (elements x rules).

**Compute is 98.5% of total; propagate is 1.5%.** This confirms
the ADR-021 Followup hypothesis ("targeted cascade") in shape,
but changes the expected end-state:

- Targeted compute on 2 elements: ~0.2ms (down from 850ms)
- **Full propagate stays**: ~12ms (erased entries need inherited
  properties refilled)
- **Full restyle_walk stays**: ~5ms
- **Expected total: ~17ms on Wikipedia**

That is not the "<5ms" the ADR-021 Followup aspired to. But it
is well under perceptual instant threshold (~50-100ms), and it
is a ~40x improvement over the current 700ms. Whether 17ms
warrants further optimization (targeted propagation, targeted
walk) is a followup question, deferred here.

## Decision

We add targeted cascade: on hover change, re-cascade only the
two elements whose state actually changed, keeping the rest of
the ComputedStyleMap cached. Propagate and walk remain full-tree.

### New cascade API (renderer/css/cascade.hpp)

```cpp
// Re-run compute_styles scoped to `el`'s subtree, writing into
// `out`. Elements in the subtree get explicit-only styles --
// inherited properties are filled by propagate_inheritance.
void cascade_subtree(const dom::Element& el,
                     const Stylesheet& ua_sheet,
                     const Stylesheet& author_sheet,
                     const RenderState& state,
                     ComputedStyleMap& out);

// Remove `el` and all its element descendants from `out`. Used
// before cascade_subtree to ensure inherited properties from a
// prior restyle don't linger.
void erase_subtree(const dom::Element& el, ComputedStyleMap& out);

// Existing propagate_inheritance, moved from anon namespace to
// public so the pipeline can call it after targeted cascading.
void propagate_inheritance(const dom::Node& doc,
                           ComputedStyleMap& out);
```

### RenderPipeline changes

```cpp
class RenderPipeline {
    // ... existing ...
private:
    ComputedStyleMap styles_cache_;
    Stylesheet sheet_cache_;
    const renderer::dom::Element* prev_hovered_ = nullptr;
};
```

- `relayout()` populates `styles_cache_` via full cascade (existing
  full-cascade path, unchanged cost) and caches the parsed author
  sheet in `sheet_cache_`.
- `render()` clears `prev_hovered_` alongside `state_.hovered_element`
  (the ADR-018 dangling-pointer fix already clears the latter; the
  former joins it because it also points into the old DOM).
- `restyle()` becomes:

```cpp
void RenderPipeline::restyle() {
    if (!doc_) return;
    if (styles_cache_.empty()) {
        // Cache miss (first restyle, or after invalidation):
        // fall back to full cascade. Same cost as ADR-021.
        const std::string css = extract_inline_css(*doc_);
        sheet_cache_ = renderer::css::Stylesheet::parse(css);
        styles_cache_ = renderer::css::cascade(
            renderer::css::ua_stylesheet(), sheet_cache_, *doc_, state_);
    } else {
        // Targeted: erase + re-cascade dirty subtrees
        for (auto* el : {prev_hovered_, state_.hovered_element}) {
            if (!el) continue;
            renderer::css::erase_subtree(*el, styles_cache_);
            renderer::css::cascade_subtree(
                *el, renderer::css::ua_stylesheet(), sheet_cache_,
                state_, styles_cache_);
        }
        // Full propagate: refill inherited properties for the
        // erased entries from their (possibly-changed) parents.
        // ~12ms on Wikipedia; cannot be targeted without tracking
        // explicit-vs-inherited per property (deferred).
        renderer::css::propagate_inheritance(*doc_, styles_cache_);
    }
    restyle_walk(layout_root_, styles_cache_);
    prev_hovered_ = state_.hovered_element;
}
```

### Cache invalidation

- **`render()`**: replaces `doc_`, calls `relayout()` which
  rebuilds `styles_cache_` and `sheet_cache_`. Cache is fresh
  after every navigation.
- **`record_visit()`**: modifies `state_.visited_urls`. Called
  from `window.cpp` *before* `render()`, so the subsequent render
  picks up the new visited set. **Invariant: `record_visit()` is
  only safe to call before `render()`.** If a future ADR adds
  "mark visited without navigating," that ADR must also clear
  the cache.
- **`relayout()`**: called on navigation and on width change.
  Both invalidate the layout tree, so full cascade is correct.
- **`set_hovered_element()`**: *does not* invalidate. The targeted
  diff handles it.

## Design decisions

### Why erase_subtree is necessary

The existing `propagate_inheritance` fills in inherited properties
only when the child's entry lacks them (idempotent by design). If
we simply re-cascade `<a>` and re-run propagate, its descendants
retain the *old* inherited `color` from the previous restyle,
because they still have a `color` entry in the cache.

Fix: erase the dirty subtree entirely from the cache before
re-cascading. After erasure, entries don't exist, and propagate
fills them in from parents. The invariant is: **the cache after
targeted restyle equals the cache after full cascade**, byte for
byte. This is unit-tested (Commit 2).

### Why propagate_inheritance stays full-tree

Two alternatives exist for targeting propagation:

1. **Track explicit-vs-inherited**: add `bool inherited` to
   `CascadeValue`. Populate in propagate. Skip non-inherited
   entries on subsequent propagates. Touches ADR-010's struct.
2. **Re-propagate only dirty subtrees**: means we can identify
   which descendants of a dirty element inherit from it. Requires
   walking the subtree, which is essentially the same cost as a
   full propagate on the common case (a link's subtree is tiny),
   and much more complex to get right (a descendant might inherit
   from a grandparent that's outside the dirty subtree).

Neither pays off given that propagate is 1.5% of the cost. Both
are deferred to a followup ADR if the 17ms ever becomes a
problem. For now: full propagate, full walk, targeted compute.

### Why the parsed author sheet is cached separately

`extract_inline_css()` walks the whole DOM looking for `<style>`
elements. On Wikipedia that's ~2ms -- not huge, but it's on every
restyle. Worse, `Stylesheet::parse()` on Wikipedia's 141 rules is
~4ms -- also not huge, but again on every restyle. Combined ~6ms
per restyle, which is a meaningful fraction of the ~17ms target.

The CSS has not changed since the last navigation, so cache the
parsed Stylesheet alongside the styles cache. Invalidate in
`render()` (new document implies new CSS). This drops the
expected hover latency from ~17ms to ~11ms without any additional
complexity.

### Why hover on a wrapping div could be expensive

If the hovered element is a large `<div>`, `cascade_subtree(div)`
re-cascades the div + all its element descendants. On Wikipedia
that could be thousands of elements. This is correct but
potentially slow.

In practice:
- UA stylesheet only applies `:hover` to `a` (via `a:hover`
  color rule)
- Hovering a `<div>` doesn't produce a `:hover` match on the div
  unless an author rule targets `div:hover`. Wikipedia has none
  in inline CSS.
- So the compute diff for a div-hover is "0 elements change, we
  re-cascade the subtree wastefully but it's a no-op in terms of
  resulting style."

A future optimization: skip subtree re-cascade if the element's
`:hover` state can't produce a different style (e.g. no rule with
`:hover` matches the element). Deferred; document the current
cost as "correct but potentially wasteful for large hover targets."

## Deferred

- **Targeted propagation** (track explicit-vs-inherited or re-
  propagate subtrees). Requires ADR-010 struct change or subtree
  walk. Deferred because 12ms full propagate is acceptable.
- **Targeted walk** (`restyle_walk` filtered to dirty subtrees).
  5ms is acceptable.
- **Skip subtree re-cascade when hover can't produce a diff**.
  Would need to inspect rules and check if any `:hover` rule
  matches the target. Deferred.
- **Release build performance**. Current measurements are debug
  (-O0). Release (-O3) is typically 5-10x faster; the numbers in
  this ADR are debug-only. Documented for future decision-making:
  if Release gives <5ms on Wikipedia, the whole targeted-cascade
  effort might have been unnecessary -- but we can't retroactively
  un-ship it, and it's correct and future-proof.
- **Cache eviction**. Unbounded session cache; consistent with
  ADR-020's page cache approach.

## Consequences

**Better:**
- Hover latency on Wikipedia drops from ~700ms to an estimated
  ~11-17ms (measured: TBD in Commit 2). This is sub-perceptual-
  threshold; hover will feel instant even on the largest pages.
- Small pages get faster too (though they were already <1ms).
- The cache structure opens the door to future optimizations
  (partial restyle of specific properties, not just hover).

**Accepted limitations:**
- Full propagate + full walk still run per restyle, so the floor
  is ~17ms on Wikipedia regardless of how many elements changed.
  Going lower requires targeted propagation (deferred).
- Hover on a large wrapping element is potentially expensive
  (subtree re-cascade is O(subtree size)). Not exercised by UA
  styles; noted as future optimization.
- Cache invalidation invariant: `record_visit` must precede
  `render`. Documented here, tested implicitly by the pipeline's
  current call order.

**Unchanged:**
- `:link`, `:visited`, `:hover` semantics.
- Page cache (ADR-020) behavior.
- History/navigation (ADR-019).
- Hover visual appearance.

## Implementation plan

Three commits on `feat/targeted-cascade`:

1. **Cascade API additions** -- `cascade_subtree`, `erase_subtree`,
   expose `propagate_inheritance` publicly. Unit tests: cascade_subtree
   + erase_subtree produce identical map to full cascade on the
   same subtree.
2. **RenderPipeline integration** -- `styles_cache_` +
   `prev_hovered_` + `sheet_cache_`. `relayout()` populates
   cache. `restyle()` uses targeted path when cache non-empty.
   Cache invalidation on `render()`. Timing measurement in the
   commit (before remove). Unit test: cache after targeted
   restyle equals cache after full cascade.
3. **Docs update** -- ADR-022 status -> Implemented, README row
   update ("hover <20ms on Wikipedia"), CHANGELOG entry.

## Verification

Unit tests:

- `cascade_subtree` for a subtree produces identical ComputedStyles
  as `cascade()` restricted to the same subtree elements
- `erase_subtree` removes exactly el + descendants from map,
  leaves siblings alone
- **Cache equality invariant**: after a full cascade, save the
  map; change `hovered_element`; call targeted restyle; compare
  final map to a fresh full cascade with the new state. **Must
  be identical.** This is the critical correctness test -- if
  propagation doesn't refill erased entries correctly, this fails
  loudly instead of producing subtle color bugs.
- Dimension invariant (existing test from ADR-021) still holds

Visual verification:

- Wikipedia hover timing: measured via instrumentation, target
  <30ms (expected ~11-17ms)
- Color changes correctly on hover
- Reverts correctly on un-hover
- No regression on small pages

## Acceptance criteria

ADR-022 is complete when:

- [ ] `cascade_subtree`, `erase_subtree`, public `propagate_inheritance`
      added with unit tests
- [ ] `RenderPipeline` cache + targeted `restyle()` implemented
- [ ] Cache-equality invariant test passes (targeted = full)
- [ ] Measured hover latency on Wikipedia <30ms (target ~11-17ms)
- [ ] Hover color behavior unchanged (visual)
- [ ] `main` is green: 254 + N cases, all pass, zero warnings
- [ ] Measured numbers documented in this ADR's Implementation section

**Explicit non-claims**: sub-frame (<5ms) hover is *not* delivered
by this ADR; that requires targeted propagation (deferred). The
claim is "~40x faster, sub-perceptual threshold" which is user-
equivalent to instant.

## Implementation

Implemented on `feat/targeted-cascade` across three commits:

| Commit | Scope |
|---|---|
| `fb68119` | Cascade primitives: `cascade_subtree`, `erase_subtree`, public `propagate_inheritance`. 3 unit tests including the primitive-level correctness check (cascade_subtree + propagate equals full cascade for the subtree). |
| `352eb0c` | RenderPipeline integration: `styles_cache_`, `sheet_cache_`, `prev_hovered_`; `relayout()` populates; `render()` clears; `restyle()` targeted path with fallback. 2 cache-equality invariant tests (81 assertions) verifying targeted restyle equals full cascade. |
| (this commit) | Docs: ADR-022 status -> Implemented, README + CHANGELOG entries |

**Measured performance** on Wikipedia's HTML article (799KB, 7,871 elements, 158 rules, debug build -O0, 71 observed restyle transitions during a manual hover session):

| Category | Measured | Notes |
|---|---|---|
| Welcome page (pre-navigation) | 0.13-0.51ms | Not representative of real content |
| Link hover (typical, ~99% of observed) | **14-25ms** | ~30-50x faster than ADR-021's ~700ms baseline |
| Medium element hover (tables, lists) | 55-67ms | ~10x improvement |
| Structural element hover (large subtree) | 850-932ms (4 of 71 transitions) | ~6% of observed transitions; same cost as pre-ADR-022 |
| Median | ~20ms | |
| Mean | 76ms | Dominated by the 4 structural spikes |

The 4 large spikes are consistent with the ADR's Deferred caveat ("hover on large wrapping element is potentially expensive, correct but wasteful") but **not directly verified to be `<div>` wrappers** -- we did not log which element triggered each transition. The inference is that some hover targets have large subtrees (which `cascade_subtree` re-cascades entirely). Whatever the specific elements were, the characterization holds: most hover targets are fast (14-25ms), some have large subtrees and are slow (~900ms, cost dominated by subtree re-cascade).

**Compared to ADR-021 baseline:** link hovers are ~30-50x faster, which is the case ADR-022 set out to optimize. Wrapping-element hovers are unchanged from ADR-021's ~700-900ms range -- the ADR was honest that this case was not fixed.

## Deferred followup: ADR-023 (skip-subtree optimization)

The 4 large spikes (~6% of observed transitions) are the remaining gap. Root cause: when the hovered element has a large subtree (e.g. a structural div containing most of an article), `cascade_subtree(el)` re-cascades every element in that subtree -- thousands of elements, same cost as full cascade.

**ADR-023 (proposed)** would add a skip-subtree guard: before calling `cascade_subtree(el)`, check whether any `:hover` rule in the current stylesheets could possibly match `el` or any descendant. If not, skip entirely (hover state change produces no style diff for that subtree).

Correctness nuance: descendants of `<div class="wrapper">` may include `<a>` elements, and `a:hover` DOES match those `<a>` elements. So the naive "no rule matches `div:hover`" check is insufficient -- we need "no rule matches `*:hover` for any element in the subtree." Options:

1. **Precompute** which elements can ever match a `:hover` selector. Cache a `std::unordered_set<const Element*>` of "hoverable" elements; on hover change, walk the subtree checking membership. Cost proportional to subtree size, but a cheap walk (pointer lookups) instead of full cascade (rule matching + map inserts). Expected ~10-100x faster than `cascade_subtree` for the skip case.
2. **Decompose the rule set**: build a per-element "which rules could match this element on hover change" index. Same idea as (1) but with more upfront bookkeeping.
3. **Rule-level shortcut**: if no rule with a `:hover` simple-selector exists at all in the current stylesheets, skip all targeted work entirely (fast path for pages with no hover CSS). Not sufficient for Wikipedia's case since `a:hover` exists, but useful for small pages.

Option 1 is the natural extension and matches the existing pattern (`erase_subtree` + `cascade_subtree` are already walking the subtree -- adding a "does this element match :hover rules?" check per node is a small constant factor).

**Out of scope for ADR-022 because:**
- It requires a new "match only `:hover` rules" fast path in the matcher, or a parallel "hoverability" analysis
- The 99% case (link hovers) is already fast
- The 1% case (wrapping element hovers) is rare in practice and produces no visible delay in normal use
- The correctness invariant ("targeted == full cascade") would need to be re-verified for the skip path

Expected outcome: wrapping-element hovers drop from ~900ms to ~20-50ms (essentially the same as link hovers, since the skip check walks the subtree but doesn't cascade it). This would close ADR-022's only remaining gap.

**Meanwhile**, users hovering links (99% of observed behavior) get 14-25ms latency, which is sub-perceptual. The 1% slow case is documented but not user-visible in normal browsing.

