# ADR-058: `top: 50%` + `transform: translateY(-50%)` (magnifier chain close)

**Status:** Implemented
**Date:** 2026-11-02
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-033 (position), ADR-042 (absolute positioning), ADR-051 (viewport units), ADR-054 (transform:rotate + magnifier diagnostic), ADR-056 (background URLs), ADR-057 (parent_content_height)

## Context

The search magnifier has been the through-line of five ADRs:

| ADR | Contribution |
|---|---|
| 054 | Diagnosed that the magnifier needs `top: 50%` + `translateY(-50%)`, not rotate; shipped rotate |
| 055 | `background:` color forms |
| 056 | `background:` URL forms -- **magnifier renders** (at y=7, too high) |
| 057 | `parent_content_height` threading -- **prerequisite for `top: 50%`** |
| 058 | **This ADR** -- `top: 50%` + `translateY(-50%)` |

### Measurement (2026-11-02, consolidated from ADR-054)

| Quantity | Value |
|---|---|
| `.cdx-text-input` (search box) height | 42px |
| `.cdx-text-input__start-icon` own height | 20px |
| Current absolute y | 7 (container top) |
| CSS on icon | `position: absolute; top: 50%; transform: translateY(-50%)` |

Correct final y: `7 + (42 * 0.5) - (20 * 0.5) = 18` (perfectly centered).

### What exists today

- **`top: Npx`** works in the absolute positioning block (`parse_off` lambda at layout.cpp:1720, using `parse_length`)
- **`top: N%`** doesn't resolve because `parse_length` resolves `%` against **width** (via `ctx.parent_content_width`). A `top: 50%` on a 42px-tall parent would resolve against width -- catastrophic.
- **`transform: rotate()`** works (ADR-054)
- **`transform: translateY()`** ignored -- rotate parser finds no `rotate(`, leaves field 0, paint applies nothing
- **`parent_content_height`** threaded (ADR-057) but **not consumed by `parse_off`** -- `abs_ctx.parent_content_height` is never set

### The two-different-percentages distinction (critical)

| Property | `%` reference | When resolved |
|---|---|---|
| `top: 50%` | **Parent** content height (`abs_ch`) | Layout time, `parse_off` lambda |
| `translateY(-50%)` | **Own** box height | After `box.height` is finalized |

**The two use different references.** `top` is parent-relative; `translateY` is self-relative. This means the implementation has two independent parts, each using a different "50%".

For the magnifier:
- `top: 50%` = `0.5 * 42 = 21` (half parent height)
- `translateY(-50%)` = `-0.5 * 20 = -10` (negative half own height)
- Sum: 7 + 21 - 10 = 18 ✓

## Decision

Ship `top: %` resolution + `transform: translateY()` support. Completes the magnifier chain.

### LayoutBox

+1 field next to `transform_rotate_deg`:

```cpp
// ADR-058: translateY transform in px. Resolved during
// layout_element after box.height is known (% relative to
// own height). Painted as cairo_translate(cr, 0, ty) inside
// the same save/restore wrapper as rotate.
float transform_translate_y = 0.0f;
```

### `top: %` resolution (absolute path)

The `parse_off` lambda at layout.cpp:1720 currently calls `parse_length(*v, abs_ctx)` -- `%` resolves against `abs_ctx.parent_content_width`, wrong for `top`/`bottom`.

**Fix (3 changes):**

1. **Set `abs_ctx.parent_content_height = abs_ch`** when populating the context (line 1710-1716 area). `abs_ch` is already computed at line 1680 (`box.height - padding - border` -- the containing block's height).

2. **Split `parse_off` into `parse_off_h` and `parse_off_v`:**
   - `parse_off_h("left"/"right")`: uses `parse_length` (% against parent_content_width -- correct)
   - `parse_off_v("top"/"bottom")`: uses `parse_length_for_height` (% against parent_content_height -- the fix)

3. **Update call sites:**
   - `left_o` / `right_o` -> `parse_off_h`
   - `top_o` / `bottom_o` -> `parse_off_v`

### `translateY()` resolution (own height)

The transform block at layout.cpp:1911-1923 extracts `rotate()`. Extend it to **also** extract `translateY()`. But `translateY(-50%)` needs `box.height`, which isn't set until line 1928. So:

1. **In the transform block** (line 1911-1923): extract the raw `translateY(...)` token (as string) into a local `std::string transform_translate_y_raw`.

2. **After `box.height` is set** (line 1928): resolve `transform_translate_y_raw` to px:
   - If `%`: multiply parsed number by `box.height / 100`
   - Else: `parse_length(transform_translate_y_raw, ctx)` (px/em/rem)

3. **Store in `box.transform_translate_y`.**

### Paint wrapper

ADR-054's wrapper at paint.cpp:258-268 gates on `did_rotate`:

```cpp
bool did_rotate = false;
if (box.transform_rotate_deg != 0.0f) {
    cairo_save(cr);
    // ... rotate math ...
    did_rotate = true;
}
// ... body ...
if (did_rotate) cairo_restore(cr);
```

**Generalize to `did_transform`** -- activates if EITHER rotate OR translate is present:

```cpp
bool did_transform = false;
if (box.transform_rotate_deg != 0.0f || box.transform_translate_y != 0.0f) {
    cairo_save(cr);
    if (box.transform_translate_y != 0.0f) {
        // Apply translate first (simpler, matches magnifier case)
        cairo_translate(cr, 0, box.transform_translate_y);
    }
    if (box.transform_rotate_deg != 0.0f) {
        const float cx = box.x + box.width * 0.5f;
        const float cy = box.y + box.height * 0.5f;
        const float rad = box.transform_rotate_deg * 0.017453292519943295f;
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, rad);
        cairo_translate(cr, -cx, -cy);
    }
    did_transform = true;
}
// ... body ...
if (did_transform) cairo_restore(cr);
```

## Design decisions

### Why translate first, then rotate

CSS `transform` functions apply in **source order**. `translateY(-50%) rotate(45deg)` is translate-then-rotate. `rotate(45deg) translateY(-50%)` is rotate-then-translate. Different results.

**v1 applies translate first, then rotate, regardless of source order.** Why:

- The magnifier uses `translateY` only (no rotate), so this ordering is correct for the whole target case
- Rotate-only (ADR-054) is unaffected (translate is 0)
- Combined rotate+translate in one `transform:` declaration is a corner case (no current fixture uses it)
- **Simplest correct-by-construction for every case that exists**

If a future ADR needs combined transforms with correct source-order semantics, it'll parse the function order and either store a list or compose the Cairo transform sequence based on source order. Deferred.

### Why split parse_off into h/v lambdas (not parameterize)

The alternative (one `parse_off(prop, vertical: bool)`) works but reads worse. Two lambdas make the intent at each call site explicit:

```cpp
const auto top_o    = parse_off_v("top");     // % against parent height
const auto bottom_o = parse_off_v("bottom");
const auto left_o   = parse_off_h("left");    // % against parent width
const auto right_o  = parse_off_h("right");
```

Four lines, self-documenting.

### Why `abs_ctx.parent_content_height = abs_ch` is safe

`abs_ch` is the containing block's content height (already computed at line 1680). It's the correct reference for `top: %` on absolute children per CSS §10.1 (absolute positioning).

Setting `parent_content_height` on `abs_ctx` doesn't affect `abs_ctx.parent_content_width` (which stays `abs_cw`). Both are correct references for their respective axes.

### Why `%` in `translateY` uses own height, not parent

CSS spec: `transform: translateY(<percentage>)` -- the percentage is relative to **the element's own height**. Not the parent's. This is a well-known gotcha (different from `top: %` which is parent-relative).

For the magnifier: `translateY(-50%)` on a 20px icon = -10px. Not -21px (half the parent).

**Consequence**: the resolution **must** happen after `box.height` is finalized, not in the transform extraction block (which runs before `box.height`). This is why the two-block structure is required.

### Why box.height at line 1928 is safe to use

By the time line 1928 executes:
- All children are laid out (`cursor_y` finalized)
- Explicit `height:` (if any) has been applied
- `min-height`/`max-height` clamps have applied

So `box.height = (cursor_y - content_y) + padding + border` is the **final** height. `translateY(-50%)` resolves correctly against it.

For a box with `height: 20px` set explicitly, `box.height` reflects that (20 + any padding/border). For auto-height content (magnifier without explicit height), `box.height` reflects the content-derived height. Both work.

### Why the paint wrapper generalizes to did_transform

`did_rotate` is a local bool that gated cairo_save/restore balance. Generalizing to `did_transform` (true if rotate OR translate) keeps the invariant: "cairo_save was pushed iff we'll pop it". No risk of imbalanced Cairo state.

The translate is applied **inside the save** -- so it doesn't leak to siblings. Same discipline as rotate.

## Deferred

- **`translateX()`** -- not needed by any Wikipedia rule
- **`translate(x, y)` 2-arg form** -- not needed
- **Combined `rotate + translate` in one declaration with source-order semantics** -- no current fixture
- **`top: calc(50% + 5px)`** -- works via `parse_length_for_height`'s `%` handling + `calc` machinery (ADR-037). Not tested in this ADR.
- **`bottom: %`, `left: %`, `right: %`** -- left/right use width correctly (parse_off_h). `bottom: %` now works via parse_off_v (same as top). Not tested but shipped for free.
- **`translateY()` on non-absolute elements** -- works via the same transform extraction; no current consumer

## Consequences

**Better:**
- **`top: 50%`** now resolves for absolute children of explicit-height parents
- **`transform: translateY()`** works -- both `%` and px forms
- **The magnifier centers**: `y = 7 + (42*0.5) - (20*0.5) = 18`
- **The 5-ADR magnifier chain closes**: 054 → 055 → 056 → 057 → 058
- **First centered production element since ADR-045's inline-background work**
- **`bottom: %` works for free** (same `parse_off_v` path)
- **Test count 669 -> ~676**

**Accepted limitations:**
- **Combined rotate + translate** applies translate-first regardless of source order
- **`translateX()` not shipped** (not needed)
- **Absolute positioning still doesn't reflow** if the box is stretched -- the ADR-042 "shift from (0,0)-layout" pattern still applies; magnifier isn't stretched so unaffected

**Unchanged:**
- ADR-005 through ADR-057 behavior
- `top: Npx` / `left: Npx` / `right: Npx` -- unaffected (non-% uses parse_length)
- Rotate-only transform -- unaffected
- `background-image` paint -- unaffected

**Possible regression risks (verify in Commit 1):**
- **`abs_ctx.parent_content_height = abs_ch`** -- this is new; existing absolute tests with `top: 10px` unaffected (non-% still routes through parse_length_for_height which delegates to parse_length for px)
- **The `parse_off` split** -- existing `left:`/`right:` calls use `parse_off_h` (unchanged behavior). If I accidentally route `left:` through `parse_off_v`, `left: 50%` would resolve against height (wrong). Verify all 4 call sites.
- **The paint wrapper change** -- existing rotate-only tests should still pass (translate=0, no-op)
- **`transform_translate_y_raw` scope** -- it's declared inside the `if (style)` block at line 1911, but needs to survive to line 1928. Must be declared **before** the transform block, at function scope, or moved outside.

## Implementation plan

Two commits:

1. **LayoutBox field + layout_element translateY + absolute top:% + paint wrapper + tests**:
   - `layout_box.hpp`: `transform_translate_y = 0.0f`
   - `layout.cpp`:
     * `abs_ctx.parent_content_height = abs_ch`
     * `parse_off_h` / `parse_off_v` split
     * Extend transform block to extract `translateY()` raw value
     * After `box.height`, resolve to px, store in `transform_translate_y`
   - `paint.cpp`: `did_rotate` -> `did_transform`, add translate
   - `test_layout.cpp`: 6 new tests + 1 flip
   - `docs/adr058-top-translate-test.html`: fixture
   - `docs/README.md`: fixture list

2. **Docs**: ADR-058 -> Implemented, Post-ADR-058 notes in 054/056/057, README + CHANGELOG

## Verification

**Unit tests (6 new + 1 flip):**
1. `transform:translateY(-50%)` on 20px box -> `transform_translate_y = -10`
2. `transform:translateY(10px)` -> `transform_translate_y = 10`
3. `transform:translateY(50%)` on 40px box -> `transform_translate_y = 20`
4. `position:absolute; top: 50%` on 42px parent -> resolved target y = 21 (relative to parent content top)
5. `position:absolute; top: 50%; transform:translateY(-50%)` on 42px parent -> y = 21 - 10 = 11 (inside parent)
6. `position:absolute; top: 10px` -- unaffected (non-%)
7. **Flip**: `"layout: translateY(-50%) leaves rotate field at 0"` -> `"...leaves rotate at 0, sets translate_y"` -- `transform_rotate_deg == 0 AND transform_translate_y == -10`

**Synthetic fixture** (`docs/adr058-top-translate-test.html`):
- Row 1: control -- 20px icon at container top (`y=0`)
- Row 2: `top: 50%` only -- icon at `y=21` (half container, still not centered)
- Row 3: `transform: translateY(-50%)` only -- icon at `y=-10` (above container)
- Row 4: **both** -- icon at `y=11` (centered)

**Wikipedia production verification**:
Screenshot before/after on HTML article. Expected:
- **Before ADR-058**: magnifier at `y=7` (container top, ~10px too high)
- **After ADR-058**: magnifier at `y=18` (perfectly centered in the search box)

This is the **definitive visual proof** of the 5-ADR chain: 054 diagnosed the problem, 055-057 built the prerequisites, 058 centers the icon.

**Test count target**: 669 -> ~676 (+7 tests, 1 flip).

## Acceptance criteria

ADR-058 is complete when:

- [ ] `LayoutBox::transform_translate_y` field exists
- [ ] `layout_element` extracts `translateY()` (px + %) and stores in field
- [ ] `abs_ctx.parent_content_height = abs_ch` set for absolute children
- [ ] `parse_off_h` / `parse_off_v` split -- `top`/`bottom` use height, `left`/`right` use width
- [ ] `paint_box` translate wrapper (generalized `did_transform`)
- [ ] ~7 new tests pass (669 -> ~676)
- [ ] 1 test flipped (`translateY(-50%) leaves rotate field at 0`)
- [ ] Fixture shows row 4 icon centered
- [ ] **Wikipedia HTML article**: search magnifier at `y=18` (was `y=7`)
- [ ] Post-ADR-058 notes in ADR-054, 056, 057
- [ ] README row + CHANGELOG
- [ ] ADR-058 status -> Implemented

## Implementation

Implemented on `feat/top-translate-y` across two commits
(Commit 1 = two-layer top/translateY implementation,
Commit 2 = perf split-timer infrastructure). Single PR #255
merged at `222d58a`.

### What shipped

**`layout_box.hpp`:**
- `float transform_translate_y = 0.0f` (px, resolved
  post-height)

**`layout.cpp`:**
- `abs_ctx.parent_content_height = abs_ch` (1 line) --
  the containing block's height feeds `%` resolution in
  top/bottom offsets
- `parse_off` split into `parse_off_h` (left/right, uses
  `parse_length` against `parent_content_width`) and
  `parse_off_v` (top/bottom, uses `parse_length_for_height`
  against `parent_content_height`). All 4 call sites routed.
  `bottom: %` works for free.
- Transform extraction: `translateY(...)` raw value captured
  alongside existing `rotate()` extraction. Raw survives at
  function scope (declared BEFORE the `if (style)` block)
  so post-height resolution can use it.
- After `box.height` finalized: `transform_ty_raw` resolves
  against own box height via `parse_length_for_height`
  (temporarily overridden ctx). Store in
  `box.transform_translate_y`.

**`paint.cpp`:**
- `did_rotate` -> `did_transform`. Activates on `rotate != 0
  || translate_y != 0`.
- Wrapper: `cairo_save`, translateY first (if set), rotate
  second (if set). Preserves the save-iff-pop invariant.

**`render_pipeline.cpp`:**
- `PERF-CASCADE` / `PERF-LAYOUT` / `PERF-TOTAL` timing in
  `relayout()`. Diagnostic-only; zero behavior change.
  Foundation for ADR-059.

**`test_layout.cpp`:**
- 1 test flipped: `"translateY(-50%) leaves rotate field at
  0"` -> `"translateY(-50%) sets translate_y, leaves rotate
  at 0"` (100px box -> -50). Tagged `[adr-054][adr-058]`.
- 6 new ADR-058 tests:
  - `translateY(10px)` -> 10
  - `translateY(50%)` on 100px box -> 50
  - `rotate + translateY` sets both
  - `absolute top:50%` resolves against containing block
    height (42px -> +21)
  - `absolute top:50% + translateY(-50%)` centers icon
    (y=21, translate=-10, painted=11)
  - `bottom:50%` resolves for free

**`docs/adr058-top-translate-test.html`:**
- 4-row fixture: control / top-only / translate-only / both.
  Demonstrates the two-%-reference distinction visually.

### Verified delta (production, screenshot 2026-11-02)

**Wikipedia HTML article:**

| Element | Before ADR-058 | After ADR-058 |
|---|---|---|
| Search magnifier | `y=7` (container top) | **`y=18` (centered)** |
| Wikipedia logo | correct | correct |
| Body / layout | correct | correct |

**The 5-ADR magnifier chain closes with visible production
proof:**

| ADR | Contribution | Icon y |
|---|---|---|
| 054 | Diagnose `top: 50%` + `translateY(-50%)` | invisible |
| 055 | `background:` color forms | invisible |
| 056 | `background:` URL forms -- **renders** | 7 |
| 057 | `parent_content_height` threading | 7 |
| **058** | **`top: 50%` + `translateY(-50%)`** | **18 (centered)** |

### The perf split-timer measurement

**Release build, Wikipedia HTML article:**

```
PERF-CASCADE: 1057.9ms  PERF-LAYOUT: 2379.3ms  PERF-TOTAL: 3437.2ms
```

| Phase | Time | % |
|---|---|---|
| Cascade | 1058ms | 31% |
| Layout | 2379ms | 69% |
| **Total** | **3437ms** | 100% |

**Debug build (for comparison):** 10,544ms total. Release
speedup is ~3.1x -- expected for `-O0` -> `-O2`. **Not a
build-flags-only problem; the underlying work is genuinely
expensive.**

Both halves are O(n²)-ish (selector matching has no
indexing; layout does 48 `style->get()` calls per element
with string keys). ADR-059 (perf cycle) uses this data as
its Context.

### Honest notes

**Split-timer is part of this ADR's scope**, not a separate
diagnostic branch. It lives in the same `render_pipeline.cpp`
that ADR-058 already touched (Commit 1), and it's permanent
measurement infrastructure that ADR-059 will rely on. Shipping
it as ADR-058 Commit 2 keeps the ledger clean: the ADR that
measured the performance problem is the same one that
discovered it needed measuring.

**Why the two-layer structure was required** (documented in
spec's "critical distinction"):

- `top: 50%` = `%` against **parent** content height
  (`abs_ch`), resolved at layout time
- `translateY(-50%)` = `%` against **own** box height,
  resolved after `box.height` finalized

Attempting one unified resolution would use the wrong
reference for one of them. This is a genuine CSS-spec
distinction, not an implementation choice.

**Definition-body split-declaration discipline applied
twice**: ADR-057 hit this (missing .cpp definitions after
.hpp declaration updates). ADR-058 avoided it by explicitly
patching both `paint.cpp` wrapper + restore lines in the
same patch and verifying via `did_rotate` grep. Same
discipline: when a symbol rename crosses declaration +
definition, patch both together and grep-verify.

### Test count

- 669 -> 675 cases (+6 new, 1 flipped)
- 2562 -> 2586 assertions (+24)
- Zero existing test regressions

### Deferred (documented)

- `translateX()` / 2-arg `translate(x, y)` -- not needed
  by any Wikipedia rule
- Combined `rotate + translate` **source-order** semantics --
  v1 applies translate-first regardless of source order.
  Correct for the magnifier case; corner case deferred.
- `bottom: %`, `left: %`, `right: %` -- bottom works for
  free (same `parse_off_v` path as top); left/right use
  width correctly via `parse_off_h`.
- `translateY()` on non-absolute elements -- works via the
  same transform extraction; no current consumer.



## Post-ADR-059 note

**The split-timer that ADR-058 shipped enabled ADR-059 to
isolate and fix the cascade phase.**

ADR-058 introduced `PERF-CASCADE` / `PERF-LAYOUT` /
`PERF-TOTAL` in `relayout()`. The first measurement:

```
PERF-CASCADE: 1057.9ms  PERF-LAYOUT: 2379.3ms  PERF-TOTAL: 3437.2ms
```

31% cascade, 69% layout. **Two distinct problems, no single
fix.** Without the split, we'd have wasted time optimizing
the wrong half.

ADR-059 targeted cascade with selector indexing:

```
PERF-CASCADE: ~190ms   (5.6x speedup, 3-run stable)
```

Cascade's share of render time: **31% -> 6-7%.** Layout is
now 93% of remaining work.

### The measurement methodology lesson

Layout showed 3x variance across 3 runs (2415ms - 7156ms)
for identical content in identical binary. **Split-timers
alone aren't enough for multi-run-per-page measurements --
you also need median-of-N sampling.** ADR-060 (layout perf)
inherits this lesson.

The split-timer also revealed the load-time warm-up
behavior: the first `PERF-CASCADE` (blank page start) is
~0.1ms, the second (partial render) ~0.1ms, the third
(full Wikipedia) is the actual cost. **The largest of the
three lines is the real measurement.**
