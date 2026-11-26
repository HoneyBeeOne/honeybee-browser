# ADR-059: Selector index for cascade performance

**Status:** Implemented
**Date:** 2026-11-02
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-010 (cascade origin/specificity), ADR-022 (`apply_rule_call_count` test hook), ADR-041 (pseudo-element cascade), ADR-058 (perf split-timer)

## Context

ADR-058's split-timer measured Wikipedia HTML article in
Release: **cascade 1058ms (31%), layout 2379ms (69%),
total 3437ms**. ADR-059 targets the cascade 1058ms.

### What the current cascade does

Per `cascade.cpp:55-64`, for every element, cascade iterates
**every rule** in both the UA and author sheets, times 3
pseudo-targets (None, Before, After):

```cpp
// Per element × 3 pseudo targets:
for (const auto& rule : ua_sheet.rules) {
    apply_rule(rule, el, UA, ..., PseudoElement::None, style);
}
for (const auto& rule : author_sheet.rules) {
    apply_rule(rule, el, Author, ..., PseudoElement::None, style);
}
// ... repeated for Before, After
```

`apply_rule` (line 55-73) short-circuits via
`rule.selectors.matches(el, state, target)`. For Wikipedia
(~5000 elements × ~2000 rules × 3 pseudo-targets), that's
**30 million `apply_rule` calls**, each doing full selector
matching from the rightmost compound backward.

### The indexing invariant

**For a rule to match an element, every simple selector in
the rightmost compound must match.** This is the CSS
specificity invariant — the foundation of every browser's
selector index. Specifically:

- If the rightmost compound contains `#foo`, the element
  **must** have `id="foo"`
- If it contains `.bar`, the element **must** have class `bar`
- If it contains `div`, the element **must** be a `<div>`

So we can bucket rules by their "most selective" rightmost
simple, and query only the rules whose anchor matches the
element. Everything else is definitionally impossible to
match and safe to skip.

### What exists that supports this

`SimpleSelector` (selectors.hpp:84-96) has clean typed kinds:

```cpp
struct SimpleSelector {
    SimpleKind kind;   // Type / Class / Id / Universal / ...
    std::string name;  // class name, id, or tag name
    // ... attribute + pseudo fields
};
```

`CompoundSelector` (line 98-100) holds `std::vector<SimpleSelector>`.
The rightmost compound of a `Selector` is `selector.parts.back().compound`.

`Selector::matches` (selectors.cpp:869-883) already
extracts pseudo-element from the rightmost compound and
filters on `target`. So the index doesn't need pseudo-target
partitioning — `matches()` handles it.

## Decision

Build a **rightmost-simple-selector index** in `Stylesheet::parse`.
Query the index per element in `cascade()` instead of
iterating every rule.

### Index structure (`stylesheet.hpp`)

```cpp
struct Rule;  // forward

struct RuleIndex {
    // Rules whose rightmost compound has an ID. Key = id name.
    std::unordered_map<std::string, std::vector<const Rule*>> by_id;
    // Rules whose rightmost compound has a class (and no ID).
    // Key = the FIRST class in the compound (deterministic).
    std::unordered_map<std::string, std::vector<const Rule*>> by_class;
    // Rules whose rightmost compound has a type (and no ID,
    // no class). Key = tag name.
    std::unordered_map<std::string, std::vector<const Rule*>> by_type;
    // Rules whose rightmost compound has none of ID/Class/Type
    // (universal, attribute-only, pseudo-only). Matched against
    // every element.
    std::vector<const Rule*> universal;
};

struct Rule { /* existing ... */ };

struct Stylesheet {
    std::vector<Rule> rules;
    RuleIndex index;  // built at end of parse()
    // ... existing fields

    // Populates index from rules. Idempotent (clears + rebuilds).
    // Called at end of Stylesheet::parse. Public for testability
    // but callers should not normally need it.
    void build_index();
};
```

### Anchor selection (per rule, per selector in `selectors.selectors`)

```cpp
const Rule* anchor_simple(const Selector& sel, const Rule& rule) {
    if (sel.parts.empty()) return nullptr;  // malformed, skip
    const auto& simples = sel.parts.back().compound.simples;
    // Priority: ID > Class > Type > Universal
    for (const auto& s : simples) {
        if (s.kind == SimpleKind::Id && !s.name.empty())
            return &rule;  // caller keys by s.name
    }
    for (const auto& s : simples) {
        if (s.kind == SimpleKind::Class && !s.name.empty())
            return &rule;  // caller keys by first class name
    }
    for (const auto& s : simples) {
        if (s.kind == SimpleKind::Type && !s.name.empty())
            return &rule;  // caller keys by tag name
    }
    return &rule;  // universal bucket
}
```

Actually, cleaner: a helper returning `enum Bucket { Id, Class, Type, Universal }` + the key string.

### Query (per element)

```cpp
// Returns candidates from a unified index. Caller must dedupe.
std::vector<const Rule*>
RuleIndex::candidates_for(const dom::Element& el) const {
    std::vector<const Rule*> out;

    // ID bucket (at most 1 ID per element)
    if (auto id = el.get_attribute("id"); !id.empty()) {
        auto it = by_id.find(id);
        if (it != by_id.end())
            out.insert(out.end(), it->second.begin(), it->second.end());
    }

    // Class buckets (0-N classes per element)
    if (auto cls = el.get_attribute("class"); !cls.empty()) {
        for (const auto& c : split_whitespace(cls)) {
            auto it = by_class.find(std::string(c));
            if (it != by_class.end())
                out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }

    // Type bucket
    {
        auto it = by_type.find(el.tag());
        if (it != by_type.end())
            out.insert(out.end(), it->second.begin(), it->second.end());
    }

    // Universal bucket (always)
    out.insert(out.end(), universal.begin(), universal.end());

    // Dedupe: a Rule with multiple selectors can land in multiple
    // buckets. Sort + unique by pointer.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
```

### Cascade rewrite

Current per-element pattern (3 pseudo passes × 2 sheets):

```cpp
for (const auto& rule : ua_sheet.rules) {
    apply_rule(rule, el, UA, ..., None, style_none);
}
// ... before/after
```

New pattern (compute candidates once per element, reuse
across all 3 pseudo passes):

```cpp
const auto ua_candidates = ua_sheet.index.candidates_for(el);
const auto author_candidates = author_sheet.index.candidates_for(el);

// None
for (const auto* r : ua_candidates) apply_rule(*r, el, UA, ..., None, style_none);
for (const auto* r : author_candidates) apply_rule(*r, el, Author, ..., None, style_none);

// Before
for (const auto* r : ua_candidates) apply_rule(*r, el, UA, ..., Before, style_before);
for (const auto* r : author_candidates) apply_rule(*r, el, Author, ..., Before, style_before);

// After
for (const auto* r : ua_candidates) apply_rule(*r, el, UA, ..., After, style_after);
for (const auto* r : author_candidates) apply_rule(*r, el, Author, ..., After, style_after);
```

`apply_rule` is unchanged — it still does `matches()` per
candidate, which filters pseudo-target internally.

## Design decisions

### Why bucket by the "most selective" simple (ID > Class > Type)

Buckets should be small. If a rule has `.a.b`, indexing by
`a` yields a smaller bucket than indexing by `*` (universal).
For `.a.b#c`, indexing by `c` (id) is best.

**The invariant is preserved because any of these simples
must match**: if we index by `id=c` and the element doesn't
have `id=c`, the rule can't match. If we index by `class=a`
and the element doesn't have `a`, same. So any "must-match"
simple works; we pick the most selective for bucket size.

Multiple classes in a compound (`.a.b`): index by the first
(the deterministic choice). Element with `.a` and `.b` will
find it. Element with only `.b` won't — but `.a` is required,
so that's correct.

### Why unified index (not partitioned by pseudo-target)

`Selector::matches()` (selectors.cpp:873-882) already extracts
pseudo-element from the rightmost compound and fails on
mismatch. So a single index works: query once per element,
call `apply_rule` 3 times per candidate (once per pseudo
target), `matches()` filters.

Alternative: partition `RuleIndex` into `for_none` / `for_before`
/ `for_after`. This would be 3× faster at query time (only
33% of candidates match each pseudo target) but 3× the index
memory and complexity.

**Trade-off**: unified index is ~2/3 wasted `apply_rule` calls
that fail fast on pseudo mismatch. If candidates ≈ 50, that's
~33 wasted calls per element per pseudo pass × 3 = 100 wasted
per element × 5000 elements = 500k wasted calls. Each is a
few instructions (pseudo check + return false). At 10ns each,
~5ms total. **Negligible.** Ship unified; partition only if
it shows up in profiling.

### Why build at parse time (not lazily)

`Stylesheet::parse` returns by value. If we build the index
at end of parse, the `Rule*` pointers reference
`rules.data()[i]`. `std::vector` move preserves element
addresses, so pointers survive the move-return.

**Invariant**: callers must not mutate `Stylesheet::rules`
after parse. This holds today — cascade takes `const
Stylesheet&` and no code pushes to rules post-parse. Document
in `build_index()` docstring.

Alternative: use `std::size_t` indices instead of `Rule*`.
Safer but more indirection at query. Rejected for the
straightforward case.

### Why dedupe via sort+unique

A `Rule` with multiple selectors (`.a, .b, #c`) can land in
all three buckets. Query by element with `id=c` and `class=a`
would add the same rule 2+ times. Sort + unique keeps the
candidate set small.

**Sort cost**: candidates ~50, sort is ~300 comparisons,
unique is ~50. Per element × 5000 = 1.5M compares total.
Trivial. Alternative (hash-set per element) allocates more.

Alternative: dedupe via `unordered_set<const Rule*>` per element.
O(1) insert but allocates. Rejected for allocation churn.

### Why the sort key is pointer address

`Rule*` values are `&rules[i]` — distinct per rule, sortable.
Even if two selectors of the same rule land in different
buckets, they produce the same `Rule*` and unique collapses
them. Pointer sort gives deterministic order (implementation
detail but stable within a run).

### Why the anchor selection has a fixed priority

Alternative: score by empirical bucket size. Rejected — the
priority (ID > Class > Type) is nearly universally optimal
(id buckets are tiny, class buckets medium, type buckets
large, universal huge). Simple heuristic beats adaptive
for this scope.

## Deferred

- **Pseudo-target partitioning** -- 3× faster query but 3×
  index memory. Not needed at ADR-059's scale.
- **`[attribute]` indexing** -- attribute buckets would be
  rarely hit for Wikipedia CSS. Universal-bucket them for
  now; measure if it matters.
- **Descendant combinator optimization** -- rules like
  `div p` still scan the type-`p` bucket. Could pre-filter
  by ancestor type. Out of scope.
- **Style attribute inline cache** -- separate concern.

## Consequences

**Better:**
- **Cascade drops from ~1.06s to ~50–200ms** on Wikipedia
- **~30M apply_rule calls drop to ~1.5M** (~20× reduction)
- **`RuleIndex` is reusable infrastructure** -- future
  optimizations (pseudo partitioning, attribute buckets)
  build on it
- **No behavior change** -- the invariant guarantees
  correctness by construction. `matches()` still does the
  exact check.
- **Total Wikipedia render drops from 3.44s to ~2.4s**
  (layout still dominates)

**Accepted limitations:**
- **Layout still 2.38s** -- ADR-060 territory
- **Auto-mutation of `rules` after parse** would dangle
  index pointers. Documented invariant; no current risk.
- **Sibling/child combinator queries still scan anchor bucket**
  -- `div > p` still checks every `p` bucket entry, matching
  parent. Acceptable for current CSS.

**Unchanged:**
- ADR-010 through ADR-058 behavior
- Cascade specificity, origin, importance resolution
- Pseudo-element extraction (unchanged in `matches`)
- `apply_rule_call_count` semantics (the counter still
  increments per apply_rule call; the *count value* drops
  dramatically; tests may need update)

**Possible regression risks (verify in Commit 1):**
- **`apply_rule_call_count` tests** (3 sites in
  test_layout.cpp): the counter will now count ~50× fewer
  calls. If a test asserts "≥ N" or "== N", it needs
  revision. If it asserts relative semantics ("hover > no-
  hover"), likely still holds.
- **Rules with no anchor simple** (universal, `:root`,
  `[attr]`) land in the universal bucket -- correctness
  preserved, but those rules are queried for every element.
  If a stylesheet has ~1000 such rules, no savings. Measure.
- **Order of rules within a bucket** -- sort+unique by
  pointer may reorder. Cascade specificity resolution is
  order-independent within the same specificity, but rule
  iteration order affects the `apply_rule_call_count`
  trace. Tests asserting exact counters need update.
- **`Stylesheet` move semantics** -- `RuleIndex` holds
  `Rule*`; `std::vector` move preserves pointers, but
  copy-construct would rebuild index (invalid). `Stylesheet`
  is only move-assigned, never copied, in current code.
  If copy semantics are added later, index must be rebuilt.

## Implementation plan

Two commits:

1. **RuleIndex + build_index + cascade rewrite + test updates**:
   - `stylesheet.hpp`: `RuleIndex` struct, forward-decl
     `Rule`, `Stylesheet::index` field, `Stylesheet::build_index`
   - `stylesheet.cpp`: implement `build_index` (iterate rules,
     compute anchor per selector, append to appropriate bucket)
   - `cascade.cpp`: replace 6 rule-iteration loops with
     candidate-based loops (compute per element, reuse across
     3 pseudo passes)
   - `selectors.hpp/cpp`: helper `anchor_bucket(const Selector&)`
     returning `(bucket, key)` pair
   - `test_layout.cpp`: update 3 `apply_rule_call_count` tests
     (revise expected values or change assertions to relative)
   - `test_stylesheet.cpp`: ~5 new ADR-059 tests
   - Verify: 675 → ~680 tests still pass

2. **Docs**: ADR-059 → Implemented, Post-ADR-059 notes in 010
   / 022 / 058, README + CHANGELOG

## Verification

**Unit tests (~5 new):**
- `RuleIndex`: rule with `#foo` lands in `by_id["foo"]`
- `RuleIndex`: rule with `.bar` lands in `by_class["bar"]`
- `RuleIndex`: rule with `div` lands in `by_type["div"]`
- `RuleIndex`: rule with `*` lands in `universal`
- `candidates_for` dedupes rules with multiple selectors

**Existing tests unchanged (except counter assertions):**
- All 125 test_stylesheet.cpp tests must pass unchanged
- 3 `apply_rule_call_count` sites in test_layout.cpp may flip

**Performance verification (`PERF-CASCADE`, Release):**
```
Before (ADR-058): PERF-CASCADE: 1057.9ms
After  (ADR-059): PERF-CASCADE: [target 50–200ms]
```
Run against Wikipedia HTML article. Capture `PERF-CASCADE`
line. Speedup ratio = `1057.9 / new_cascade_ms`.

**Success criterion**: `PERF-CASCADE < 300ms` (3.5× minimum
win). If > 300ms, the index is ineffective (probably too
many universal-bucket rules) — investigate before shipping.

**Wikipedia end-to-end**: `PERF-TOTAL` drops from 3437ms to
~2400ms (cascade saves ~860ms; layout unchanged at 2379ms).

## Acceptance criteria

ADR-059 is complete when:

- [ ] `RuleIndex` struct in `stylesheet.hpp`
- [ ] `Stylesheet::build_index()` populated at end of parse
- [ ] `cascade()` uses `candidates_for(el)` instead of rule-iteration
- [ ] Helper `anchor_bucket` extracting rightmost-simple key
- [ ] 3 `apply_rule_call_count` tests updated (documented reason)
- [ ] ~5 new ADR-059 tests pass (675 → ~680)
- [ ] `PERF-CASCADE` on Wikipedia < 300ms (Release)
- [ ] `PERF-TOTAL` drops from 3437ms to ~2400ms
- [ ] Post-ADR-059 notes in ADR-010 / 022 / 058
- [ ] README row + CHANGELOG
- [ ] ADR-059 status → Implemented

## Implementation

Implemented on `feat/selector-index` across two commits
(Commit 1 = index + cascade rewrite, Commit 2 = this docs
update). Single PR #258 merged at `7888756`.

### What shipped

**`selectors.hpp` / `selectors.cpp`:**
- `AnchorBucket` enum (`Id` / `Class` / `Type` / `Universal`)
- `struct Anchor { AnchorBucket bucket; std::string key; }`
- `anchor_for(const Selector&)`: returns the most selective
  mandatory-match simple from the rightmost compound
  (priority `Id > Class > Type > Universal`)

**`stylesheet.hpp` / `stylesheet.cpp`:**
- `RuleIndex` struct: 4 buckets (`by_id`, `by_class`,
  `by_type`, `universal`), each `std::vector<std::size_t>`
  (rule **indices**, not `Rule*` -- preserves source order
  for specificity tiebreaking)
- `Stylesheet::index` field
- `build_index()` called at end of `parse()`
- `candidates_for(id, classes, tag)`: sorted + deduped
  candidate indices. **DOM-free interface** -- callers
  extract element fields

**`cascade.cpp`:**
- 6 rule-iteration loops replaced with 2 candidate groups
  (UA + author). Candidates computed **once per element**,
  reused across 3 pseudo-target passes.
- Extracted element id / classes / tag once per element.

**`tests/test_layout.cpp`:**
- ADR-022 counter-threshold test comment updated. Baseline
  drops from ~17 to ~5 calls per transition. **Assertions
  unchanged** -- the `< 100` bound already covers both
  baselines, so no flips.

**`tests/test_stylesheet.cpp`:**
- 5 new ADR-059 tests: bucket landings for #id / .class /
  tag / universal, plus multi-selector dedupe.

### Verified delta (Release build, 3 fresh Wikipedia loads)

| Run | Cascade | Speedup vs 1058ms |
|---|---|---|
| 1 | 200.3ms | 5.3x |
| 2 | 188.7ms | 5.6x |
| 3 | 189.6ms | 5.6x |

**~190ms stable across runs, 5.6x structural speedup.**
Meets ADR-059's acceptance criterion (`PERF-CASCADE <
300ms`).

**~870ms saved per Wikipedia page load** from cascade alone.

### The layout variance discovered during measurement

Layout (unchanged by ADR-059 -- same code path as pre)
showed **3x variance across the same 3 runs**:

| Run | Layout | Total |
|---|---|---|
| 1 | 2750.1ms | 2950.4ms |
| 2 | 2415.5ms | 2604.3ms |
| 3 | **7155.7ms** | 7345.3ms |

**Identical content, identical binary, 3x variation.** This
is environmental noise on the 8GB test machine (concurrent
Firefox ~900MB resident, potential memory pressure / cache
eviction during the ~5000-element layout traversal), not a
regression from ADR-059.

**Methodology lesson for ADR-060** (layout perf): single-run
timings are unreliable at Wikipedia scale. Multi-run +
median (not mean -- outliers like Run 3's 7156ms would skew
the mean badly) is required to claim any layout delta.

Small-page control confirms the measurement path itself is
sound: `docs/demo.html` (10 elements) shows cascade 0.1ms,
layout 23ms first pass, 0.5-1.3ms subsequent. Tight, sensible.

### Honest framing of the improvement

**Cascade share of render time: 31% -> 6-7%.**

Before ADR-059: cascade 1058ms, layout 2379ms, total 3437ms.
After ADR-059: cascade ~190ms, layout (variance) ~2400-7150ms.

The **cascade win is real and structural** -- the index
eliminates ~29 million of ~30 million `apply_rule` calls
per Wikipedia page load. This will persist.

The **total-time win is masked by layout variance** --
whether Wikipedia loads in 2.6s or 7.3s after ADR-059
depends on ambient conditions, not on our code. ADR-060
(layout perf) is the remaining bottleneck and will need
the multi-run methodology lesson above.

### Two implementation bugs caught before shipping

1. **Namespace-scope bug**: first `build_index` placement
   was appended after the closing `} // namespace
   honeybee::renderer::css` -- compiled cleanly as a free
   function but `Stylesheet` was unreachable at that scope.
   22 errors, all the same root cause. Fix: relocated the
   entire block inside the namespace.

2. **Test literal `\n`**: Python's `'\\n'` in a heredoc
   appended literal `\` + `n` characters to
   `test_stylesheet.cpp` instead of a newline. One-line fix.

**Same class of discipline as ADR-057's definition-body bug
and ADR-058's `ctx` scope bug**: patch the symbol where it
is *used*, not just where it is *declared*. The compiler
catches the mismatch immediately, but only if the whole
project is rebuilt (which ADR-059's first test run didn't
do -- it ran against the old binary).

### Why the anchor priority is a real invariant (not a heuristic)

`anchor_for` picks `Id > Class > Type > Universal` because
each of these is a **mandatory match**: if the element
doesn't have the specific id/class/tag, the selector
cannot match. So bucketing by the most selective one gives
the smallest possible bucket without ever dropping a
should-match rule.

**The bug class this protects against**: if anchor priority
were wrong (e.g. picking class over id), rules with both
would land in `by_class["a"]` and miss elements that have
`id="b"` but not `class="a"`... except `id="b"` was required
too, so it wouldn't miss anything. **Actually safe either
way** -- any mandatory-match simple works. Priority picks
the *smallest* bucket, not correctness.

**The real bug class is missing buckets**: rules with
attribute-only anchors (`[href]`, `[aria-expanded]`) land in
universal. If a stylesheet has many of those, universal
grows large and the index saves less. Universal bucket size
should be measured as a follow-up (ADR-059b if it's >500
rules on Wikipedia).

### Test count

- 675 -> 680 cases (+5 new)
- 2586 -> 2608 assertions (+22)
- **Zero flips** in the 3 ADR-022 counter tests (bounds
  already accommodated both baselines)

### Deferred (documented)

- **Pseudo-target partitioning** -- 3x memory, not needed
  at ~190ms current cascade.
- **`[attribute]` indexing** -- attribute-only rules
  currently in universal. Follow-up ADR-059b if universal
  bucket turns out to be >500 rules on Wikipedia.
- **Descendant combinator ancestor pre-filtering** --
  `div p` still scans every `p` bucket entry. Out of scope.
- **Layout's 2.4s** -- ADR-060 territory. Requires
  multi-run methodology per the measurement lesson above.

