# ADR-051: Viewport Units (vh / vw / vmin / vmax)

**Status:** Accepted
**Date:** 2026-10-29
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-013 (length units — px/em/rem/%), ADR-037 (calc() — operand resolution), ADR-050 (@supports — surfaced the `calc(100vh - 48px)` case)

## Context

`parse_length` recognizes `px`, `em`, `rem`, `%`. Viewport units
`vh`, `vw`, `vmin`, `vmax` were explicitly deferred at ADR-037
(line 67 of `style_values.cpp`: *"Deferred (per ADR-037):
vh/vw/vmin/vmax (need viewport_height ...)"*).

ADR-050 surfaced the concrete consequence: after unblocking
`@supports (display: grid)`, `.vector-pinned-container` gained
`display: block` — but `.vector-sticky-pinned-container`'s
`max-height: calc(100vh - (24px * 2))` still evaluates to
**`(unset)`**, because `calc()` resolves the `100vh` operand via
`parse_length` and `vh` returns 0.

This ADR adds viewport units.

### Measurement (Wikipedia CSS, 2026-10-29)

| Unit | Occurrences |
|---|---|
| `vh` | **4** (`20vh`, `100vh`, `75vh`, `65vh`) |
| `vw` | **4** (all `75vw`) |
| `vmin` | 0 |
| `vmax` | 0 |

**Renderable rules** (7 rules, 8 declarations):

| Rule | Declaration | Reachable? |
|---|---|---|
| `.vector-sticky-pinned-container` | `max-height: calc(100vh - 48px)` | **Yes** — the ADR-050 successor |
| `.vector-dropdown-content` | `max-height: 75vh` | Yes (dropdown, hidden until opened) |
| `.vector-page-titlebar .vector-dropdown-content` | `max-height: 65vh` | Yes (dropdown) |
| TOC `max-width: min(0.85 * 59.25rem, 75vw)` (4 variants) | Yes (TOC sizing) |
| `.mw-highlight-lines pre span[id]` | `scroll-margin-top: 20vh` | **No** — `scroll-margin-top` is not parsed by the engine (independent deferral) |

**Net: 7 renderable rules.** All in secondary containers.

### Current LengthContext structure

```cpp
struct LengthContext {
    float font_size = 16.0f;
    float parent_font_size = 16.0f;
    float root_font_size = 16.0f;
    float parent_content_width = 0.0f;
    bool on_font_size_property = false;
};
```

**No viewport dimension.** 7 `LengthContext ctx;` construction
sites across `layout.cpp` (lines 57, 173, 366, 800, 1560, 1583,
1877).

### The `parent_content_width` vs. `viewport_w` distinction

`layout_element` receives `parent_content_width` — the **parent's**
content width, not the viewport width. `%` uses this correctly.
But `100vw` means "the viewport's width," not the parent's.

**These are independent parameters.** Threading viewport
dimensions requires new function parameters, not just populating
existing ones.

## Decision

Add `vh`, `vw`, `vmin`, `vmax` unit support to `parse_length`.
Thread viewport dimensions through `layout()` → `layout_element()`
→ `LengthContext`.

### LengthContext gains two fields

```cpp
struct LengthContext {
    // ... existing fields ...
    float viewport_w = 0.0f;   // viewport width (for vw/vmin/vmax)
    float viewport_h = 0.0f;   // viewport height (for vh/vmin/vmax)
};
```

**Defaults 0.0f** — when unpopulated (tests that don't set them),
`vh`/`vw` resolve to 0. That's semantically correct for "unknown
viewport" and preserves all existing test behavior (none of them
use `vh`/`vw` today, so 0 is fine).

### parse_length gains four unit branches

```cpp
enum Unit { PX, EM, REM, PCT, VH, VW, VMIN, VMAX };

if (ends_with(s, "vmin")) { unit = VMIN; suffix_len = 4; }
else if (ends_with(s, "vmax")) { unit = VMAX; suffix_len = 4; }
else if (ends_with(s, "vh"))   { unit = VH;   suffix_len = 2; }
else if (ends_with(s, "vw"))   { unit = VW;   suffix_len = 2; }

// (in existing order: vmin/vmax must be checked BEFORE vh/vw
// to avoid "vmin" matching "vh" suffix.)

case VH:   return (n / 100.0f) * ctx.viewport_h;
case VW:   return (n / 100.0f) * ctx.viewport_w;
case VMIN: return (n / 100.0f) * std::min(ctx.viewport_w, ctx.viewport_h);
case VMAX: return (n / 100.0f) * std::max(ctx.viewport_w, ctx.viewport_h);
```

### eval_math_expression

`calc(100vh - 48px)` already works: `eval_math_expression`
recurses into `parse_length` for each operand (line 315, verified
during measurement). Adding VH to `parse_length` automatically
makes `calc(100vh - ...)` resolve.

**No changes needed to `eval_math_expression`.**

### Threading viewport dimensions

**`layout()` signature change**:

```cpp
LayoutBox layout(const dom::Document& doc,
                 const css::ComputedStyleMap& styles,
                 TextMeasurer measurer,
                 float viewport_width,
                 float viewport_height,          // NEW
                 ImageDimLookup image_dims = nullptr);
```

**`layout_with_pseudo()` similarly** (`float viewport_height` new
param, passed through to `layout()`).

**`layout_element()` signature change**:

```cpp
LayoutBox layout_element(const dom::Element& el,
                         const css::ComputedStyleMap& styles,
                         const TextMeasurer& measurer,
                         float x_before_margin,
                         float y_before_margin,
                         float parent_content_width,
                         float parent_font_size,
                         float root_font_size,
                         Direction parent_dir,
                         float viewport_w,              // NEW
                         float viewport_h,              // NEW
                         ImageDimLookup image_dims);
```

**`LengthContext` construction sites**: all 7 populated with
`ctx.viewport_w = viewport_w; ctx.viewport_h = viewport_h;` — using
the values in scope.

**7 internal `layout_element` call sites** updated to pass
`viewport_w, viewport_h` through (block, flex, grid, fixed,
absolute, inline, img).

### `render_pipeline.cpp` caller

`render_pipeline.cpp` currently passes `viewport_width`. It gains
`viewport_height` from `window.cpp`'s GTK allocation (the widget's
allocated height, which the async fetcher / relayout trigger
already has access to via `m_scroll_window.get_allocated_height()`
or equivalent).

**Alternatively** (if GTK height isn't cleanly available at
relayout time): pass `0` for `viewport_h`, which makes `vh`
resolve to 0. Documented as pragmatic v1 if the plumbing is
awkward — this ADR's layout-tree verification uses a hardcoded
height in the diagnostic harness, not the GTK window.

**Decision**: prefer threading GTK height. Fall back to 0 with
a documented deferral ("GTK height plumbing to render_pipeline is
its own small ADR") if the GTK API call doesn't fit cleanly.

## Design decisions

### Why thread through signatures (not thread-local)

**Alternative**: thread-local `g_viewport_w` / `g_viewport_h` set
by `layout()` at entry, read by `LengthContext`'s default
constructor.

**Rejected because**: thread-locals are untestable (each test
would need to set/reset), break determinism if tests run in
parallel (Catch2 supports parallel), and contradict the codebase's
explicit-parameter discipline.

**Cost of signature threading**: 7 `layout_element` sites + 2
`layout()` variants + 7 `LengthContext` sites = ~16 touchpoints.
Manageable in one commit.

### Why VMIN/VMAX included (despite 0 uses)

Cost: 4 lines (one enum value, two cases). Benefit: the `min()` /
`max()` math functions in CSS commonly pair with `vmin`/`vmax` on
pages that use `vh`/`vw`. Zero cost to include, avoids a future
ADR-051b.

**Alternative**: defer VMIN/VMAX. Rejected — the marginal cost
is genuinely negligible.

### Why `scroll-margin-top: 20vh` is not in scope

`scroll-margin-top` isn't a property the engine parses.
Adding `vh` doesn't make it renderable — it's deferred with the
property, not the unit.

Documented in the ADR so a future cycle doesn't mistake "vh now
works" for "scroll-margin-top now works."

### Why default viewport dims to 0

If `vh`/`vw` resolve to 0, downstream effects are:
- `max-height: 75vh` → `max-height: 0` → element clipped to 0
  height. But `max-height` isn't read by our layout today (no
  element uses `max-height` in a way that clips). Zero visible
  delta.
- `max-width: min(...,75vw)` → `max-width: 0` → if `max-width`
  were honored, element would be 0-width. **`max-width` also
  isn't read by layout.** Zero visible delta.

**But** `calc(100vh - 48px)` in `max-height` is now nonzero when
`viewport_h > 0`, and `max-height` IS readable by the cascade
even if layout ignores it. So the layout-tree verification works
(evaluating `max-height` to a real number via the style).

The zero-default is honest: **unknown viewport → 0**, which
produces the same visual as the current unparsed state (no rule
applies). Adding the parameter *enables* the correct resolution
when the caller supplies real dims.

### Why not add viewport dims to `parent_content_width` (reuse existing field)

`%` uses `parent_content_width` correctly (parent's content box).
Overloading it for `vh`/`vw` would break `%` semantics — a
`width: 50%` on a flex item should be 50% of parent, not 50% of
viewport. **Independent fields are correct.**

## Deferred

- **`scroll-margin-top` property** — not parsed; `20vh` value
  won't do anything until the property itself is added
- **`svh` / `lvh` / `dvh`** (small/large/dynamic viewport
  heights) — modern variants; not in Wikipedia CSS
- **Percentage `max-height`** — `parent_content_height` threading
  is a separate ADR-042-era deferral
- **Viewport dimensions from GTK window height** — if the
  threading to `render_pipeline.cpp` is awkward, deferring
  `viewport_h` from GTK and using 0 in production is acceptable
  v1 (this ADR's verification uses a hardcoded harness height)
- **`vw` inside `%` context** — CSS spec resolves `100vw` to
  viewport width, not percentage of anything; correctness is
  the reason for independent fields

## Consequences

**Better:**
- **`calc(100vh - 48px)` resolves** — `.vector-sticky-pinned-container`'s `max-height` becomes a real number (1750px at 1774×800 viewport)
- **7 renderable rules** start using viewport-relative sizing
- **Unblocks `position: sticky`** — sticky needs `100vh` sizing before its scroll-Y threading becomes the sole remaining prerequisite
- **`vmin`/`vmax` in `min()`/`max()`** — future CSS using viewport math works

**Accepted limitations:**
- **First-viewport PNG delta for the sticky sidebar** — depends on ancestor culling (`.vector-column-start h=0`, `.vector-main-menu-landmark display:none`), which is separate ADR work
- **`scroll-margin-top: 20vh`** still doesn't fire (`scroll-margin-top` unparsed)
- **`max-width` / `max-height` still not honored by layout** — the cascade stores them, but layout doesn't clip to them yet. Adding viewport units makes the stored values correct; acting on them is a separate "min/max clamps" ADR
- **`svh` / `lvh` / `dvh` deferred** — 0 uses in target

**Unchanged:**
- All ADR-005 through ADR-050
- `px`, `em`, `rem`, `%` behavior
- `calc()` — already recurses into `parse_length`; gains `vh`/`vw` operand support for free
- All existing tests (default 0 viewport dims preserve `vh`/`vw` at 0; no test uses them)

**Possible regression risks (to verify in Commit 2):**
- `layout()` signature change — 5 call sites in tests + production
- `layout_element` signature change — 7 internal call sites
- If any test uses an element with `max-height: <vh>` today, it would
  now evaluate to 0 instead of 0 (no change). Zero risk.

## Implementation plan

Three commits:

1. **LengthContext + parse_length** (behavior-neutral):
   - `LengthContext` gains `viewport_w`, `viewport_h` (defaults 0)
   - `parse_length` gains `VH`, `VW`, `VMIN`, `VMAX` unit branches
   - `eval_math_expression` needs no changes (verified — recurses
     into `parse_length`)
   - All existing tests pass unchanged (default 0 preserves
     current behavior)

2. **Threading + tests**:
   - `layout()` gains `viewport_height` param
   - `layout_with_pseudo()` gains `viewport_height` param
   - `layout_element()` gains `viewport_w`, `viewport_h` params
   - 7 internal call sites updated
   - 7 `LengthContext` construction sites populate viewport dims
   - `render_pipeline.cpp` caller updated
   - ~18 unit tests in `test_layout.cpp` (`[vh][vw][adr-051]`)
   - Layout-tree verification: `.vector-sticky-pinned-container`
     `max-height` resolves to a real number
   - First-viewport PNG: **zero delta documented** (ancestor
     culling blocks TOC rendering, same as ADR-050)

3. **Docs**: ADR-051 → Implemented; Post-ADR-051 note in
   ADR-013 and ADR-050; README + CHANGELOG

## Verification

**Unit tests (Commit 2, `[vh][vw][adr-051]`):**

- `100vh` at `viewport_h=800` → 800
- `100vh` at `viewport_h=0` (default) → 0
- `50vh` at `viewport_h=800` → 400
- `100vw` at `viewport_w=1774` → 1774
- `50vw` at `viewport_w=1774` → 887
- `vmin` picks smaller: `viewport_w=800, viewport_h=1000` → 800
- `vmax` picks larger: `viewport_w=800, viewport_h=1000` → 1000
- `vmin` case: `viewport_w=1000, viewport_h=800` → 800
- `calc(100vh - 48px)` at `viewport_h=800` → 752
- `calc(50vh + 50px)` at `viewport_h=800` → 450
- `min(0.85 * 59.25rem, 75vw)` at `viewport_w=1774` → min(805.8, 1330.5) = 805.8
- `calc(100vw - 24px)` at `viewport_w=1774` → 1750
- Element with `height: 100vh` and seeded viewport_h=800 → LayoutBox.height = 800 (confirms threading)
- Element with `height: 50vw` and seeded viewport_w=1000 → LayoutBox.height/width = 500
- `min(100vh, 500px)` at `viewport_h=800` → 500
- `max(100vh, 500px)` at `viewport_h=800` → 800
- Existing `%` unchanged: `width: 50%` in a 400px parent → 200 (regression)
- Existing `em` / `rem` / `px` unchanged (regression)
- `vmin`/`vmax` inside `calc()`: `calc(100vmin - 50px)` at `800×1000` → 750

**Layout-tree verification (Commit 2)**:

```cpp
// Seed viewport_h = 800 in the harness
auto root = layout_with_pseudo(..., 1774.0f, 800.0f, nullptr);
// Find .vector-sticky-pinned-container
// Expected ComputedStyle: max-height = "calc(100vh - (24px * 2))"
// Expected LayoutBox: no visible height (max-height not honored
//   by layout yet), but ComputedStyle is populated correctly
```

Actually the ComputedStyle already has the raw string
`calc(100vh - ...)`. The thing that changes is whether `parse_length`
can resolve it. So the layout-tree verification is:

- Cascade still stores the string (unchanged)
- `layout_element` calling `parse_length` on it now succeeds when
  the value is used for `height` / `max-height` / `width` — if
  those are read by layout. Since layout doesn't read
  `max-height`, the LayoutBox height won't change.
- **The verification is that the value can now be parsed to a
  number**, which the test suite proves via `parse_length` tests.

**First-viewport PNG verification (Commit 2)**:

| Check | Before | After |
|---|---|---|
| Sidebar TOC visible | (blocked by ancestor) | (still blocked — same as ADR-050) |
| ADR-050's `.vector-pinned-container` display:block | Present | Unchanged |
| Everything else | — | Unchanged |

**Zero first-viewport PNG delta** — documented honestly. The
implementation is verified via `parse_length` tests + LayoutBox
tests that confirm viewport dims thread through correctly.

**Layout-tree verification**: a test that constructs
`LengthContext` with `viewport_h=800` and asserts `parse_length("100vh")`
returns 800 verifies the code path. The `.vector-sticky-pinned-container`
element reaching a real `max-height` value is a follow-up after
`max-height` support lands.

## Acceptance criteria

ADR-051 is complete when:

- [ ] `LengthContext` gains `viewport_w`, `viewport_h`
- [ ] `parse_length` handles `vh`/`vw`/`vmin`/`vmax`
- [ ] `eval_math_expression` resolves them inside `calc()`
      (should be automatic — verify with tests)
- [ ] `layout()` gains `viewport_height` param
- [ ] `layout_with_pseudo()` gains `viewport_height` param
- [ ] `layout_element()` gains `viewport_w`, `viewport_h` params
- [ ] All internal call sites updated (7 + 5 tests)
- [ ] All `LengthContext` construction sites populate viewport dims
- [ ] `render_pipeline.cpp` passes GTK height (or 0 with documented
      deferral)
- [ ] 18+ tests
- [ ] First-viewport PNG: zero delta documented
- [ ] Post-ADR-051 note in ADR-013
- [ ] `main` green: 626 + N cases
- [ ] ADR-051 status → Implemented

**Explicit claim**: `100vh` resolves to viewport height when
`LengthContext.viewport_h` is seeded. `calc(100vh - 48px)` at
800px height returns 752. All existing `%`/`px`/`em`/`rem` tests
unchanged.

**Honest caveats:**
1. **Zero first-viewport PNG delta** — ancestor culling rules
   (`display: none`, `h=0`) still block the sticky sidebar from
   rendering. ADR-050's limitation carries over.
2. **`max-height` / `max-width` not honored by layout** — adding
   viewport units makes the ComputedStyle values resolvable, but
   layout doesn't read them yet. Acting on them is a separate ADR
   (min/max clamps).
3. **`scroll-margin-top: 20vh`** unchanged — the property isn't
   parsed, so adding `vh` doesn't fire.
4. **`svh`/`lvh`/`dvh`** deferred — 0 uses in target.
5. **GTK height threading** may fall back to 0 in production if
   the API call doesn't fit cleanly; verification uses a
   hardcoded harness height.
