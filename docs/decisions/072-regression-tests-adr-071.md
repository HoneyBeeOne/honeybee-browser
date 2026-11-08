# ADR-072: Regression tests for ADR-071 fixes

**Status:** Implemented
**Date:** 2026-11-09
**Related:** ADR-071 (the bug fixes), ADR-069 (culling rule)

## Context

ADR-071 fixed two long-standing bugs that were only caught through manual Wikipedia-scale verification:

1. **Inline elements rendered as blocks** — `layout_block_children` treated every element child as a block, giving inline elements (like `<h2 style="display:inline">`) a local `y=0`. Their text was then culled as "off-screen" during scroll.
2. **Container culling** — `paint_children` culled `Box` nodes by their own `y + height`. Because `<body>` was clamped to the viewport height (641px) while its descendants extended to 30,000px, scrolling past 641px culled the entire body and dropped the article.

Both bugs were invisible at fixture scale — they required Wikipedia's specific DOM structure (inline headings inside divs, containers with height ≠ children height) to surface. The fixes are verified manually (Wikipedia renders correctly, screenshots taken), but nothing prevents a future refactor from reintroducing them.

This ADR adds **unit tests that would fail if the ADR-071 fixes are reverted**, without requiring Wikipedia-scale data.

## Decision

Three targeted unit tests, one per fix + one guard against over-culling:

### Test 1 (`test_layout.cpp`): inline `<h2>` becomes `InlineContext`

Constructs `<div><h2>HEADING_TEXT</h2></div>` with CSS `h2 { display: inline }`. Runs `layout_element`. Asserts:

- `<h2>` is **not** a `LayoutBox` (ADR-071 routes inline children to `inline_run`)
- The text `"HEADING_TEXT"` **is** in some `InlineContext` under the div

**Before ADR-071:** `find_all_boxes(root, "h2").size()` = 1, and no `InlineContext` contains `"HEADING_TEXT"`. Test would fail on both CHECKs.

### Test 2 (`test_paint.cpp`): `Box` is never culled by its own height

Constructs: `Root Box(h=641) → Inner Box(y=0, h=30000) → Text(y=1500, h=20)`. Paints with `visible_top=1500, visible_bottom=2141`. Asserts the drawer fires once.

**Before ADR-071:** `paint_children(Root.children=[Inner])` culls `Inner` because `Inner.y + Inner.height` > margin? No wait — the cull comparison is against **Root's child**, so it checks `Inner.y + Inner.height = 30000 < visible_top - 128 = 1372` → FALSE, `Inner.y = 0 > visible_bottom + 128 = 2269` → FALSE. So Inner is not culled even with old code.

Hmm. Let me reconsider. The bug manifested for `<body>` itself, not its children. Let me restructure:

- **Root Box (the `<body>` itself)**: y=0, h=641
- **Child Box (mw-content-text)**: y=574, h=30440
- **Grandchild Text**: y=1500, h=20

Paint with `visible_top=1500`. The recursion:
- paint(root, vt=1500) → paint_box(root) → paint_children([child], vt=1500)
- Child check: `child.y + child.height = 574+30440 = 31014 < 1500-128 = 1372`? No. `child.y = 574 > 2141+128 = 2269`? No. Not culled. → paint_box(child) → paint_children([text], vt=1500)
- Text check: `text.y + text.height = 1520 < 1372`? No. `text.y = 1500 > 2269`? No. Draw ✓

Hmm — so this tree doesn't reproduce the bug either. Let me re-read the actual bug trace.

The actual issue was: `<body>` had y=0, h=641. Its children (mw-content-text) had y=574, h=30440. When scrolling, the paint tree was something like:
```
html
  → paint_children sees body (y=0, h=641)
```

Wait — was body directly checked in `paint_children`? No, body is the ROOT (or html is root, body is first child).

Actually from the trace:
- Root = `LayoutBox` for `<html>` (or document root)
- paint(root) → paint_box(root) → paint_children([body], vt, vb)
- body: y=0, h=641
  - OLD code: cull check → `0 + 641 = 641 < vt - 128 = visible_top - 128`. If `visible_top = 1045`, then `641 < 917` → TRUE → cull body → **skip entire subtree**
  - NEW code (ADR-071): body is `Box` → no cull check → recurse into paint_box(body) → paint_children([mw-content-text], vt, vb)
    - mw-content-text: y=574, h=30440. Cull check (it's also Box → no cull check) → recurse
      - ... eventually reach text at y=1500, which IS in visible range

So the reproduction is:
- **Root Box (with `body` as child)**: `Root → Box(y=0, h=641) → Box(y=574, h=30440) → Text(y=1500, h=20)`
- Paint with `visible_top=1500, visible_bottom=2141`

**Before ADR-071:** the `Box(y=0, h=641)` (= body) would be culled → descendants dropped → drawer=0
**After ADR-071:** body is Box → not culled → descendants reached → drawer=1

That works. Three levels deep.

Let me refine the spec text.

### Test 3 (`test_paint.cpp`): Text leaf IS still culled

Constructs: `Root Box → Text(y=50000, h=20)`. Paints with `visible_top=5000, visible_bottom=5641`. Asserts drawer fires **zero** times.

This guards the perf property. If a future refactor disables culling entirely (e.g., `disable_cull = true` unconditionally), this test catches the perf regression.

## What this ADR does NOT test

- **Wikipedia-scale integration.** The actual Wikipedia render requires network I/O, real CSS from MediaWiki, and a real GTK window. Manual visual verification remains the primary gate for "does Wikipedia look correct?"
- **Async logger timing.** The sync/async switch is verified by `test_log.cpp` (which now passes after the dual-mode fix). Adding a race-sensitive timing test would be flaky.
- **CSS cascade interactions.** The tests use minimal CSS. Wikipedia's cascade involves `:is()`, `:where()`, CSS variables — none of which can be exercised headlessly.

## The honest gap

These tests catch **structural regressions**. They do not catch **visual regressions**. If the cascade starts returning wrong color values for Wikipedia's specific selectors, these tests won't notice — only screenshots would.

The right pattern going forward: every Wikipedia-specific bug fixed gets **both** a structural unit test (if possible) **and** a screenshot-based manual verification documented in the ADR. ADR-071 and ADR-072 are the first instance of this pattern.

## References

- ADR-071 — the bug fixes these tests protect
- ADR-069 — culling rule (leaf-only)
- `tests/test_layout.cpp` — layout tests
- `tests/test_paint.cpp` — paint tests
