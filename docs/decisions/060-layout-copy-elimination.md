# ADR-060: Eliminate string-copy overhead in layout hot path

**Status:** Accepted
**Date:** 2026-11-02
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (block layout), ADR-013 (length units), ADR-058 (perf split-timer), ADR-059 (cascade index)

## Context

ADR-059's split-timer measured Wikipedia in Release:
cascade 190-263ms, layout 2461-3249ms (median 2678ms),
total 2655-3442ms. **Layout is ~90% of remaining work.**

### Profile (perf record, Wikipedia HTML article)

1099 samples captured via `perf record -g --call-graph dwarf`
on the Release binary. Top self-time functions:

| Symbol | Self % | Attribution |
|---|---|---|
| `contains_word` | 5.19% | **Cascade** -- `.class` substring matching during selector match |
| `pthread_rwlock_unlock` | 3.15% | Library (GTK/glib/fontconfig internal locks) |
| `_int_free` | 3.12% | **Allocation churn** |
| `malloc` | 2.23% | Same |
| `do_lookup_x` | 1.75% | Dynamic linker (first-load only, **ignore**) |
| `pthread_rwlock_rdlock` | 1.67% | Library internal |
| `FcCharSetIsSubset` | 1.49% | Fontconfig (font subset checks) |
| `matches_simple` | 1.46% | **Cascade** -- simple selector match |
| `__memmove_evex_unaligned_erms` | 1.29% | String copies |
| `matches_from` | 1.18% | **Cascade** -- recursive selector match |
| `SelectorList::matches` | 1.16% | **Cascade** -- selector list iteration |
| `_int_malloc` | 1.84% | Allocation churn |
| `cfree` | 0.98% | Allocation churn |

**Allocation churn total: ~9.5%** (~250ms).
**Cascade-related selectors: ~10%** (but cascade itself is only 7% of total after ADR-059).

### The dominant frame: `fill_sides` lambda at 25.13%

Cumulative report (`perf report --children`) shows:

```
34.87%  layout_element (whole layout phase)
  |
  |-25.13%  layout_element::{lambda()#2}::operator()
  |           (the fill_sides inner lambda)
  |
  |-12.79%  recursive layout_element calls (block children,
             flex/grid cell layout -- legitimate structural work)
  |
  |-~10%    box.width/height, transform, background handling
```

**`fill_sides` at 25.13% = ~660ms.** That's 12 property lookups
× 5000 elements = 60K lookups, each doing:

1. **Key allocation**: caller passes `const char*`, `get()` takes
   `const std::string&` -- implicit conversion allocates a
   `std::string` for the key
2. **`unordered_map` lookup**: hash + compare
3. **Value copy**: `get()` returns `std::optional<std::string>`,
   copying the stored `CascadeValue::value` (a `std::string`)
4. **Parse overhead**: caller then runs `parse_length(*v, ctx)`
   on the copied string

Three allocations per call × 12 calls per element = **36
allocations per element × 5000 = 180K allocations just for
`fill_sides`.** This is the source of the ~9.5% allocation
churn plus a significant chunk of `fill_sides`'s own 25%.

### What my initial hypothesis got wrong

I initially proposed inlining `is_ws` in `selectors.cpp` as a
~5% win, on the assumption it called `std::isspace()`. **This
was wrong.** `is_ws` (selectors.cpp:576-578) is already a
simple char-comparison chain. No win available.

I also initially scoped `contains_word` (5.19%) as a layout
hotspot. **This was also wrong** -- it's cascade-side. Its
optimization belongs to a future ADR-061 (cascade), not
ADR-060 (layout).

The diagnostic's most valuable output was **disproving my
own hypotheses**. ADR-060's scope is now derived entirely
from the profile, not from speculation.

## Decision

Ship two focused copy-elimination fixes on the layout read
path. Both are read-only changes -- no cascade, no population
logic, no structural refactor.

### Fix 1: `ComputedStyle::get_view()`

Add a `string_view`-returning variant alongside existing
`get()`:

```cpp
// cascade.hpp
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
    std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string>{}(s);
    }
};
struct StringEq {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
    bool operator()(const std::string& a, const std::string& b) const noexcept {
        return a == b;
    }
    bool operator()(const std::string& a, std::string_view b) const noexcept {
        return a == b;
    }
    bool operator()(std::string_view a, const std::string& b) const noexcept {
        return a == b;
    }
};

struct ComputedStyle {
    std::unordered_map<std::string, CascadeValue, StringHash, StringEq> declarations;

    // ADR-060: read-only view of a property value. No copy.
    // Returns nullopt if the property is not present.
    std::optional<std::string_view> get_view(std::string_view property) const;

    // Existing API unchanged (returns copy for callers that need
    // owned string, e.g. .value_or(...) patterns).
    bool has(const std::string& property) const;
    std::optional<std::string> get(const std::string& property) const;
};
```

**Why this preserves compatibility**: `get()` is unchanged.
`get_view()` is additive. Both lookups share the same
`declarations` map (now with transparent hash/equal for
heterogeneous lookup). Existing callers that need owned
strings keep working; hot-path callers opt in via `get_view()`.

### Fix 2: `fill_sides` inline fast path

The 25% lambda is inside `fill_sides` (`layout.cpp:37-44`).
Replace with a direct `find` pattern using static string keys:

```cpp
// Before (layout.cpp:37-44):
auto px = [&](const char* p) {
    const auto v = style->get(p);  // 2 allocs (key + value copy)
    return v ? parse_length(*v, ctx) : 0.0f;
};

// After (ADR-060):
auto px = [&](std::string_view p) -> float {
    auto it = style->declarations.find(p);  // no key alloc (transparent hash)
    if (it == style->declarations.end()) return 0.0f;
    return parse_length(it->second.value, ctx);  // no value copy
};

// Callers use string literals which convert to string_view at zero cost:
out[0] = px("margin-top");
out[1] = px("margin-right");
// ...
```

**Same behavior, three costs eliminated**:
- Key allocation (transparent hash accepts `string_view`)
- Value copy (`it->second.value` accessed by reference)
- Optional wrapper (direct `find`, no `optional<string>` construction)

The `parse_length` call remains — that's a separate concern
(deferred to ADR-061 typed fields if it turns out to be
significant, but the profile doesn't show `parse_length` in
the top 10, so it's probably fine for common `Npx` values).

## Design decisions

### Why not typed `ComputedStyle` fields (the bigger fix)

The profile suggests typed fields (e.g. `float margin_top`)
populated during cascade would eliminate **all** of the map
lookup + parse cost, not just the copies. That's the bigger
win.

**But**: typed fields touch the cascade population path --
every `apply_rule` needs to know about the 12 (or more) typed
properties, and `ComputedStyle` becomes a hybrid map + struct.
That's a ~200-line change across 3+ files that changes the
data model, not just the read path.

**ADR-060's scope is limited to read-path copy elimination.**
Typed fields are ADR-061's scope (after ADR-060's measured
wins prove the incremental methodology works).

### Why not `contains_word` optimization

`contains_word` at 5.19% is **cascade-side** -- it runs during
`SelectorList::matches`, not during layout. After ADR-059,
cascade is only ~7% of total time. Even a 2× speedup of
`contains_word` saves ~2% of total.

**Different ADR-061 target** (cascade `contains_word`
pre-splitting element classes).

### Why heterogeneous lookup (transparent hash) rather than string_view-to-string at call site

Alternative: keep `unordered_map<string, ...>` as-is, but
have `fill_sides` pre-construct `static const std::string`
per property once:

```cpp
static const std::string kMarginTop = "margin-top";
auto px = [&](const std::string& p) {
    auto it = style->declarations.find(p);
    ...
};
out[0] = px(kMarginTop);  // no re-alloc per call
```

**This works for `fill_sides`** (12 static keys), but it
**doesn't fix other callers** that pass dynamic property names
or `const char*`. The transparent hash fix is universal —
every `get_view(string_view)` call site benefits without
per-callsite static keys.

**Effort difference**: transparent hash is ~20 extra lines
(in cascade.hpp) but eliminates the need for static-key hacks
at every call site. Worth the one-time cost.

### Why `get_view()` alongside `get()` (not replacing `get()`)

`get()` returns `optional<string>` — some callers genuinely
need owned strings (e.g. `value_or("default")` patterns,
string manipulation). Replacing `get()` with `string_view`
would ripple through many call sites.

**Additive `get_view()` keeps the blast radius small** —
only hot-path callers switch. Future ADRs can migrate more
call sites to `get_view()` incrementally.

## Deferred

- **Typed `ComputedStyle` fields** (margin/padding/border as
  `float`) -- ADR-061, bigger win but bigger change
- **`contains_word` optimization** -- cascade-side, ADR-061
  or ADR-062 territory
- **`parse_length` fast path verification** -- not shown in
  top-10 profile, defer until measured
- **`pthread_rwlock_*` at 4.82%** -- library-internal
  (GTK/glib/fontconfig). Not actionable from our code.
- **`FcCharSetIsSubset` at 1.49%** -- fontconfig font subset
  checks. Defer unless text-heavy pages need more.

## Consequences

**Better:**
- **Layout: target ~10-15% reduction** (~250-370ms off 2678ms)
- **Allocation churn: target ~50% reduction** (~125ms off 250ms)
- **`get_view()` reusable** for every future hot-path lookup
- **Zero behavior change** -- read-path only, `get()` unchanged,
  cascade unchanged, data model unchanged
- **Test count 680 -> ~683** (3 new tests for `get_view`)

**Accepted limitations:**
- **`get()` still copies** -- callers that need owned strings
  keep the copy (correct, they need it)
- **Map lookup still happens** -- `fill_sides` still does
  `find(property)` per property. The hash + compare cost
  remains. Eliminating it requires typed fields (ADR-061).
- **`parse_length` cost remains** -- not addressed by this
  ADR. Deferred to typed fields or a dedicated parse-cache ADR.

**Unchanged:**
- ADR-005 through ADR-059 behavior
- `get()` API for all existing callers
- Cascade behavior, data model, `CascadeValue` layout
- Paint, fetch, GTK integration

**Possible regression risks (verify in Commit 1):**
- **`transparent hash` requires C++20 library support** --
  the codebase is C++20 already, so this should work.
  If build fails on `is_transparent`, fall back to static-key
  approach for `fill_sides` only.
- **`get_view()` correctness** -- must return a view that
  remains valid as long as the `ComputedStyle` is alive
  (yes: references into `unordered_map::value` are stable
  across lookups, only invalidated by insertion/erasure).
- **`get()` vs `get_view()` divergence** -- both must return
  the same logical value. Since `get_view()` uses the same
  map lookup, divergence is impossible by construction.

## Implementation plan

Two commits:

1. **`StringHash` / `StringEq` / `get_view()` + `fill_sides` inline**:
   - `cascade.hpp`: hash/equal types, map type change, `get_view()` decl
   - `cascade.cpp`: `get_view()` implementation (~10 lines)
   - `layout.cpp`: `fill_sides` uses `string_view` lookup
   - `tests/test_stylesheet.cpp`: 3 new `get_view` tests
   - Verify 680 -> 683 tests, all pass

2. **Docs**: ADR-060 -> Implemented, Post-ADR-060 notes in
   ADR-011 / 058, README + CHANGELOG

## Verification

**Unit tests (3 new):**
- `get_view("margin-top")` returns `string_view` matching `get("margin-top")`'s value
- `get_view("nonexistent")` returns `nullopt`
- `get_view` with `const char*` literal (via `string_view` conversion) works without copy

**Median-of-3 measurement (Release, Wikipedia HTML article):**
- Run 1: `PERF-LAYOUT: ???ms`
- Run 2: `PERF-LAYOUT: ???ms`
- Run 3: `PERF-LAYOUT: ???ms`
- **Median compared against pre-ADR-060 baseline 2678ms**

**Success criterion**: layout median < 2420ms (10% win).
If > 2500ms, the copy overhead wasn't the actual bottleneck —
investigate before shipping.

**`PERF-CASCADE` should be unchanged** (~193ms) -- no cascade
changes.

## Acceptance criteria

ADR-060 is complete when:

- [ ] `StringHash` / `StringEq` types in `cascade.hpp`
- [ ] `unordered_map<string, CascadeValue, StringHash, StringEq>` type change
- [ ] `ComputedStyle::get_view()` declared + implemented
- [ ] `fill_sides` uses `string_view` lookup + `.value` reference
- [ ] 3 new `get_view` tests pass (680 -> 683)
- [ ] Median-of-3 `PERF-LAYOUT` < 2420ms (10% win)
- [ ] `PERF-CASCADE` unchanged (~193ms)
- [ ] Post-ADR-060 notes in ADR-011 / 058
- [ ] README row + CHANGELOG
- [ ] ADR-060 status -> Implemented
