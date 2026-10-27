# ADR-039: Box Model Completion — height, min/max clamps, box-sizing, overflow

**Status:** Accepted
**Date:** 2026-10-28
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout), ADR-013 (length units), ADR-035 (`<img>` layout), ADR-037 (calc + math functions), ADR-038 (pseudo-classes)

## Context

The layout engine has applied `width` from ComputedStyle to
every element since ADR-011 (`layout.cpp:265` — `content_w =
parse_length(*w, ctx)`). But `height` has only ever been
applied inside the `<img>` sizing block from ADR-035. Every
other element's height is **derived entirely from content**:
`box.height = cursor_y - content_y + padding + border`.

**This is ADR-037's deferred height gap.** ADR-037's
Implementation section records that the Wikipedia search icon
went from `w=2 h=0` to `w=20 h=0` after calc() shipped — width
fixed, height not because `layout_element` never reads `height`
on non-`<img>` boxes. ADR-037 explicitly deferred this to
"ADR-040 (box model)" — that's this ADR (renumbered 039
because ADR-039's slot is here in the spec-driven roadmap).

`box-sizing`, `min-width`, `max-width`, `min-height`,
`max-height`, and `overflow` are all entirely absent. The
selector engine has no path for any of these.

### Measurement (Wikipedia CSS, 2026-10-28)

**Property usage counts**:

| Property | Total | Dominant values |
|---|---|---|
| `box-sizing` | 14 | `border-box` (12), `content-box` (2) |
| `min-width` | 40 | `10px` (15), `200px` (4), `18px` (3), `18.75em` (2), `44px` (2) |
| `max-width` | 27 | `none` (8), `min(0.85 * 59.25rem, 75vw)` (4), `100%` (3), `99.75rem` (2), `320px !important` (1), `calc(...)` (1) |
| `min-height` | 31 | `10px` (15), `32px` (3), `18px` (3), `44px` (2), `3.125rem` (1 — the header) |
| `max-height` | 6 | `3.3em`, `75vh`, `calc(100vh - 48px)`, `65vh`, `200px`, `2rem` |
| `overflow` | 18 | `hidden` (13), `auto` (2), `hidden auto` (2), `visible` (1) |
| `overflow-x` | 3 | `hidden` (2), `auto` (1) |
| `overflow-wrap` | 5 | `break-word` (4), `anywhere` (1) |

**First-viewport impact**:

| Property | Total elements | First-viewport |
|---|---|---|
| `height` (non-img, currently unapplied) | 7 | **4** |
| `min-height` | 11 | **9** |
| `max-height` | 3 | 3 |
| `min-width` | 7 | 4 |
| `max-width` | 10 | 5 |
| `box-sizing` | 9 | **7** |
| `overflow` | 8 | 5 |

**The search icon has multiple sizing rules** (from CSS):

```css
.cdx-search-input .cdx-text-input__icon.cdx-text-input__start-icon {
    min-width: 10px;
    min-height: 10px;
    width: calc(var(--font-size-medium,1rem) + 4px);
    height: calc(var(--font-size-medium,1rem) + 4px);
    display: inline-block;
}
```

So the icon has **four** sizing properties. Currently: width=20 (calc works, ADR-037), height=0 (never read), min-width=ignored, min-height=ignored. Applying `height` alone would fix it (h=0 → h=20). Applying `min-height` alone would also fix it (h=0 → h=10). Applying both: h=20 (height wins over min-height clamps).

### The box-sizing semantics shift

`box-sizing: border-box` changes what `width` means:

- **`content-box` (default)**: `width` = content area. Actual border-box width = `width + padding + border`.
- **`border-box`**: `width` = total border-box width. Actual content width = `width - padding - border`.

On Wikipedia, 12 rules use `border-box` including likely a global reset (many modern pages do `.mw-parser-output * { box-sizing: border-box }`). **This is the highest-risk change in this ADR** — it can shift every element that has both `box-sizing: border-box` and `width` set.

Elements affected: 7 first-viewport elements have `box-sizing: border-box`. Their current rendered widths include their padding+border; after this ADR, those widths will shrink by padding+border.

**Was current render wrong?** CSS-spec-wise, yes — those elements should already be `border-box`. Visually, our render has been showing them wider than spec. After ADR-039, they'll be spec-correct but visually different.

**Risk of visible regression**: an element that was "accidentally correct at the wrong width" could break at the right width. Not expected on Wikipedia (the CSS is well-tested), but the risk is real. This ADR's verification includes a "width parity" table showing before/after for key elements.

### Overflow: hidden semantics

`overflow: hidden` clips content to the box bounds. Paint-time concern, not layout:
- Layout still computes child positions normally (children can extend past box)
- Paint clips children to the box's content rect (via Cairo `cairo_rectangle` + `cairo_clip`)

**v1 scope**: `overflow: hidden` and `overflow: visible` (no-op). Defer `auto` and `scroll` (both require scrollbar rendering and scroll event handling — separate ADR).

**v1 handling of `overflow: hidden auto`** (2 uses): parse as `hidden` for both axes for now (v1 treats the two-value form by using the first value for both). Documented limitation; proper x/y separation ships with scroll support.

## Decision

Four sub-features, shipped as one ADR with two implementation commits (non-risk / risk).

### 1. `height` applied to all boxes

`layout_element` gains a height-application block parallel to the
existing width block (line 265):

```cpp
// ADR-039: apply height from ComputedStyle, override content-derived.
if (style) {
    if (auto h = style->get("height"); h && !is_auto(*h)) {
        const float eh = parse_length(*h, ctx);
        if (eh > 0.0f) {
            box.height = eh + box.padding[0] + box.padding[2]
                            + box.border[0] + box.border[2];
            cursor_y = content_y + eh;
        }
    }
}
```

**Impact on search icon**: h=0 → h=20 (calc resolves via ADR-037, then height block applies it).

**Impact on other elements**: 4 first-viewport elements get real heights. Most should be safe (their content-fit height was probably close).

**Not a layout-blocker for parent height**: if a child has explicit `height: 100px` but content overflows, the child's box.height is 100, cursor_y stays at content_y+100, parent doesn't grow. Content overflow is clipped if `overflow: hidden`, visible otherwise. Standard CSS.

### 2. `min-width`, `max-width`, `min-height`, `max-height` clamps

Applied after width/height resolution, before box dims are finalized:

```cpp
// ADR-039: min/max clamps.
if (style) {
    auto apply_min = [&](const char* prop, float& val) {
        if (auto v = style->get(prop); v && !is_auto(*v) && *v != "none") {
            const float p = parse_length(*v, ctx);
            if (p > val) val = p;  // min: floor
        }
    };
    auto apply_max = [&](const char* prop, float& val) {
        if (auto v = style->get(prop); v && !is_auto(*v) && *v != "none") {
            const float p = parse_length(*v, ctx);
            if (p > 0.0f && p < val) val = p;  // max: ceiling
        }
    };
    apply_min("min-width", content_w);
    apply_max("max-width", content_w);
    // height clamps applied after height resolution
}
```

**`max-width: min(0.85 * 59.25rem, 75vw)` — vw deferred**: since `eval_math_expression` returns 0 for `75vw` (ADR-037 defers `vw`), `min(...)` returns 0, `max-width` becomes 0, element collapses. **This is a real regression risk.**

**Mitigation**: if `parse_length` returns 0 for a max-width expression that contains `vw`/`vh`, treat as `max-width: none` (skip the clamp) rather than clamping to 0. This is not CSS-spec-pure (CSS would evaluate the vw) but preserves current rendering for those elements until vw support ships.

**Detection approach**: pre-scan the value string for `vw|vh|vmin|vmax` — if found and the calc/min/max eval returns 0, skip the clamp. Documented v1 workaround.

**Impact**: element widths clamp correctly in most cases. The `min(...)`-with-vw cases skip clamping rather than collapse to 0. Documented limitation.

### 3. `box-sizing: border-box`

The width resolution block at line 265 gains box-sizing awareness:

```cpp
// ADR-039: box-sizing changes width interpretation.
bool border_box = false;
if (style) {
    if (auto b = style->get("box-sizing"); b && *b == "border-box") {
        border_box = true;
    }
}

const auto w = style->get("width");
if (w && !is_auto(*w)) {
    const float ew = parse_length(*w, ctx);
    if (ew > 0.0f) {
        if (border_box) {
            content_w = ew - box.padding[3] - box.padding[1]
                            - box.border[3] - box.border[1];
            if (content_w < 0.0f) content_w = 0.0f;
        } else {
            content_w = ew;
        }
    }
}
```

Same for `height` block: subtract padding+border from height when border_box.

**Impact**: for the 7 first-viewport elements with `box-sizing: border-box`, width shrinks by `padding-left + padding-right + border-left + border-right`. For typical Wikipedia elements: `padding: 16px 16px; border: 1px solid` → width shrinks by ~34px. For elements with `width: 100%` and box-sizing: border-box, the width fits the parent exactly (was overflowing by padding before).

**Visual delta risk**: this is the biggest unknown. Verification table in this ADR includes a per-element width comparison.

### 4. `overflow: hidden` paint clipping

Paint-time concern. `LayoutBox` gains `bool clip_children = false` set during layout from `overflow == "hidden"`.

`paint_box` in paint.cpp, before painting children:

```cpp
if (box.clip_children) {
    cairo_save(cr);
    cairo_rectangle(cr, box.x + box.padding[3], box.y + box.padding[0],
                    box.width - box.padding[3] - box.padding[1],
                    box.height - box.padding[0] - box.padding[2]);
    cairo_clip(cr);
}
// ... paint children ...
if (box.clip_children) cairo_restore(cr);
```

**Impact**: 5 first-viewport elements with `overflow: hidden` clip children. Nav tooltips, dropdown overflow content, some decorative fixed-size containers.

**`overflow: visible`** (default): no-op, no clipping. **`overflow: auto | scroll`**: unchanged (treated as visible for v1, defer with scroll support).

## Design decisions

### Why bundle four sub-features

Each is small individually:
- `height` (~15 lines)
- `min/max` clamps (~25 lines)
- `box-sizing` (~15 lines)
- `overflow: hidden` clipping (~15 lines in paint.cpp)

But they interact:
- `height` + `max-height` clamp: max-height caps what height set
- `box-sizing` + `min/max`: clamps apply to the border-box dimension, so `width: 100px; max-width: 50px; box-sizing: border-box` means the border-box is 50px (content = 50 - pad - border)
- `overflow: hidden` + height: overflow clipping is only visible when height is bounding content

Bundling ensures the interactions are tested together, not in isolation. Same pattern as ADR-028 (grid + root-relative URL fix).

### Why two commits (non-risk / risk)

- **Commit 1 (non-risk)**: `height` application + `min/max` clamps + `overflow: hidden` clipping. All additive — elements only get "sizing" or "clipping" that wasn't there before; nothing about existing behavior changes for elements without these properties.
- **Commit 2 (risk)**: `box-sizing: border-box`. Changes existing widths. Isolated for easy revert if it causes visible regressions on first-viewport elements.

This lets us measure Commit 1's delta cleanly. If Commit 1 shows no regressions and a clean search-icon fix, merge it. Then Commit 2 with its own measurement.

If Commit 2 causes unexpected width regressions (e.g., "article intro now 34px narrower than it should be"), Commit 2 can be reverted while Commit 1 stays. Two-stage shipping minimizes blast radius.

### Why vw-in-max-width is skipped rather than allowed to collapse

`max-width: min(0.85 * 59.25rem, 75vw)` on Wikipedia's `.vector-toc` containers. If `vw` evaluates to 0 (ADR-037 defers vw), then `min(0.85 * 59.25rem, 0)` = 0, so `max-width: 0`, container collapses to 0 width.

That's a real visual bug — worse than the pre-ADR-039 state where max-width was ignored entirely (container rendered at natural width).

**v1 mitigation**: detect `vw|vh` in the value string; if present AND the eval returns 0 (indicating vw/vh was hit), skip the clamp (treat as `none`). Preserves current rendering for those 4 first-viewport elements.

**What this costs**: those 4 elements don't get their `max-width` clamp honored. But they render at natural width (current behavior), not collapsed. Documented limitation with a clear deferral to `vw`/`vh` support.

### Why `overflow: auto | scroll` deferred

Both require:
- Detecting overflow (layout must compute overflow state per box)
- Scrollbar rendering (paint)
- Scroll event handling (input plumbing, not built yet)
- Wheel event forwarding to the correct overflow-ancestor under the cursor

That's an entire subsystem. 2 uses in Wikipedia CSS (`overflow: auto`), zero visible impact in current render (no scroll interactions exist yet). Deferred to its own ADR when scroll behavior becomes a feature.

### Why `overflow-wrap: break-word` deferred

Different property, not `overflow`. `overflow-wrap: break-word` changes how text wraps within an element's available width — it's a text-layout concern handled in `layout_inline_run` / line-wrapping, not in box model.

5 uses on Wikipedia (`.mw-parser-output` — long URLs, long identifiers). Would ship as its own small ADR in the typography category, alongside `word-break` and `hyphens`. Not part of the box model category.

### Why `overflow-x` / `overflow-y` deferred

Only 3 uses combined. And they interact with `overflow: auto` (which is deferred). Ship with `overflow: auto` in the scroll ADR.

## Deferred (ADR-040+ candidates)

- **`overflow: auto | scroll`** — scrollbar rendering + scroll events, own ADR
- **`overflow-x` / `overflow-y`** — axis-specific, ship with scroll support
- **`overflow-wrap: break-word | anywhere`** — text layout, typography ADR
- **`min()` / `max()` / `clamp()` containing `vw` / `vh`** — requires viewport-height threading (ADR-037 deferred)
- **`aspect-ratio`** — 0 uses in Wikipedia's CSS but common in modern CSS; small standalone ADR
- **`max-height: vh`** — same vw/vh deferral (3 first-viewport elements affected but they're invisible containers)
- **`overflow: hidden auto`** two-value semantics — v1 treats as `hidden` for both axes; proper x/y separation ships with scroll

## Consequences

**Better:**

- **ADR-037's height gap closed**. Search icon w=20 h=0 → w=20 h=20. First-viewport visible delta, closes the ADR-037 deferral.
- **9 first-viewport elements** get `min-height` clamps — nav bars, buttons, badges that specify minimum heights now render at those heights instead of content-derived.
- **7 first-viewport elements** get `box-sizing: border-box` — spec-correct width interpretation. May fix or may regress; measured in Commit 2.
- **5 first-viewport elements** get `overflow: hidden` clipping — nav dropdowns, decorative containers clip cleanly.
- **`max-width: 100%`** on images caps them at their container. Fixes ADR-035's "footer SVGs render at intrinsic dims" gap for the `max-width: 100%` cases.
- **`max-width: min(...)`** on TOC containers — mostly works (4 uses); 1 has vw and skips, 3 don't and clamp correctly.
- **Precondition for real layout work**: box-sizing support is required for any future container queries, aspect-ratio, or flexbox max-sizing semantics.

**Accepted limitations (honest, documented):**

- **vw/vh in max-width**: 4 uses on Wikipedia; skipped (treated as none) rather than collapsed to 0.
- **`overflow: auto | scroll`**: unchanged (treated as visible, no scrollbar rendering).
- **`overflow: hidden auto`**: treated as `hidden` for both axes.
- **`overflow-wrap`**: unchanged (deferred to typography ADR).
- **`aspect-ratio`**: unchanged (0 uses in target CSS anyway).
- **Commit 2's box-sizing risk**: possible width regressions on first-viewport elements; isolated commit for revertability.

**Unchanged:**

- All non-height/width layout behavior
- Grid, flex, float, position — box-model properties apply uniformly to all layout modes
- Cascade, selector matching, paint for non-clipped content
- ADR-037's calc values — height that resolves via calc now flows to layout

## Implementation plan

Two commits on `feat/box-model`:

1. **Non-risk box-model features** — `height` application + `min/max`
   clamps (with vw/vh mitigation) + `overflow: hidden` paint
   clipping. ~75 lines across layout.cpp + paint.cpp + layout_box.hpp.
   10-12 tests.
2. **`box-sizing: border-box`** — width and height blocks gain
   box-sizing awareness. ~25 lines in layout.cpp. 4-6 tests.

Plus a docs commit.

## Verification

**Unit tests** (`[layout][box-model][adr-039]` + `[paint][box-model][adr-039]`):

Commit 1:
- `height: 100px` on div with content-less children → box.height=100
- `height: 100px` + content taller than 100 → box.height=100, cursor_y advances by 100
- `min-width: 200px` on div with `width: 100px` → content_w=200
- `max-width: 200px` on div with `width: 300px` → content_w=200
- `max-width: none` → no clamp
- `min-height: 50px` on empty div → box.height=50
- `max-height: 50px` on div with 100px content → box.height=50
- `max-width: min(...vw...)` — skip clamp when eval returns 0
- `overflow: hidden` on div → box.clip_children=true, paint sets clip rect

Commit 2:
- `box-sizing: border-box; width: 100px; padding: 10px` → content_w=80
- `box-sizing: content-box; width: 100px; padding: 10px` → content_w=100
- `box-sizing: border-box; height: 100px; padding: 10px` → content_h=80
- `box-sizing: border-box; width: 100%; padding: 10px` → content fits parent exactly

**Visual verification** (viewport 1774, Wikipedia HTML):

| Check | Before | After Commit 1 | After Commit 2 |
|---|---|---|---|
| Search icon | w=20 h=0 | **w=20 h=20** | w=20 h=20 |
| First-viewport PNG diff | — | search icon visible, min-height clamps | box-sizing width shifts |
| Regressions on article body | — | none expected | possible — measured |
| Footer SVGs | intrinsic | intrinsic + max-width clamp | same |

**Commit 1's PNG is the primary visible-delta measurement**. Commit 2's is a "did we break anything" check.

## Acceptance criteria

ADR-039 is complete when:

- [ ] `height` applied to all boxes (not just `<img>`)
- [ ] `min-width`, `max-width`, `min-height`, `max-height` clamps work
- [ ] `max-width: min(...)` with `vw` skipped (not collapsed)
- [ ] `max-width: none` / `max-height: none` no-op
- [ ] `box-sizing: border-box` subtracts padding+border from width/height
- [ ] `overflow: hidden` sets `clip_children` on `LayoutBox`
- [ ] `paint_box` clips children when `clip_children`
- [ ] `overflow: visible` / `auto` / `scroll` unchanged
- [ ] 14-18 unit tests added and passing
- [ ] Search icon h=0 → h=20 (measured)
- [ ] First-viewport width parity check (no unintended regressions)
- [ ] Post-ADR-039 note in ADR-037 (height gap closed)
- [ ] `main` green: 480 + N cases, all pass, zero warnings
- [ ] CI green (PR + post-merge)
- [ ] ADR-039 status → Implemented in docs commit

**Explicit claim**: search icon h=0 → h=20 (closes ADR-037's deferral).
9 first-viewport elements get `min-height` clamps. 7 get
`box-sizing: border-box`. 5 get `overflow: hidden` clipping.

**Honest caveats**:

1. **`box-sizing: border-box` risk** — isolated to Commit 2 for
   easy revert; verification table shows before/after.
2. **vw/vh in max-width** — 4 uses skipped, treated as `none`
   (preserves current render, defers proper handling).
3. **`overflow: auto | scroll`** — unchanged, deferred to scroll ADR.
4. **`overflow: hidden auto`** two-value — treated as `hidden`.
5. **`overflow-wrap`** — separate property, deferred to typography ADR.
6. **`aspect-ratio`** — deferred, no uses in target CSS.

**Structural value**: box-sizing is a **precondition for real CSS
layout**. Container queries, aspect-ratio, flexbox max-sizing
semantics, all future layout ADRs depend on it. Shipping it
now unlocks a decade of modern CSS features.

**The two-commit split is a strategy**: non-risk features land
first with clean measurement. box-sizing lands separately with
its own measurement + easy revert if needed. Same pattern as
ADR-028's "ship the fix, correct the record if measured-delta
shows otherwise."
