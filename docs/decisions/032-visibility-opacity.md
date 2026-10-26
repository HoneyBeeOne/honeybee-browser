# ADR-032: visibility + opacity Paint Support

**Status:** Implemented
**Date:** 2026-10-26
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-016 (inline), ADR-025 (external CSS), ADR-030 (feature flips), ADR-031 (float)

## Context

ADR-031 shipped float v1 (block-level float + block narrowing)
and hid `.sidebar` at desktop viewport. The layout tree is
correct (infobox at x=914 w=358, intro narrowed to w=590 beside
it), but the rendered PNG shows overlapping content at x=324
alongside the intro paragraph.

A subsequent diagnostic (`/tmp/leak_locate`) walked the layout
tree for text fragments containing `HTML5` / `text/html` /
`WHATWG` / `public.html`. Finding: **all left-side fragments
are at y >= 4317** — far outside the first viewport (y < 900).
Right-side fragments are at the correct infobox position
(y >= 523). **Zero fragments in the layout tree appear in the
overlap zone.**

This means the visual overlap is either:

1. **A stale PNG** — the diagnostic binary was rebuilt but the
   render may not have reflected the current layout.
2. **A paint-layer-only bug** — paint draws a subtree that
   layout skipped (e.g., a `display: none` descendant).

The second is possible if paint doesn't re-check `display` or
`visibility` on each box, and if some element's descendants
were laid out before a CSS rule hid their parent.

Either way, the **principled fix is to make paint authoritative
about visibility**. That's what this ADR covers.

### Measurement (Wikipedia CSS, 2026-10-26)

| Signal | Value |
|---|---|
| `visibility` declarations | **2** total (1 `visible`, 1 `hidden`) |
| `opacity` declarations | **23** total (5 raw `1`, 4 raw `0`, 14 `var(--opacity-*)`) |
| Current parser support | `visibility` appears in `cascade.cpp:16` as part of a property check; neither is stored as a paint signal |
| Current paint filtering | **none** — `paint.cpp` has no `display`/`visibility`/`opacity` checks |

**Honest scope call:** with only 2 `visibility` uses in
Wikipedia's CSS (one already handled by ADR-029's feature-class
flip), this ADR's visible delta on Wikipedia is **near-zero**.
It is structural preparation for:
- Future dropdowns gated by `:checked` (which will use
  `visibility: visible` on open — 14 `:checked` rules use it)
- `opacity` transitions (used for cursor/button hover states)
- The paint-authoritative pattern (needed once `position:
  absolute` lands, since positioned elements need to be drawn
  in the correct order regardless of layout order)

**What this ADR is NOT:** it does not fix ADR-031's apparent
overlap. The measurement says the overlap is not in the layout
tree, so `visibility`/`opacity` culling cannot remove something
that layout already skipped. If a fresh PNG still shows the
overlap after this ADR, the cause is deeper (paint traversal
drawing nodes not in the tree, or the earlier PNG was stale).

## Decision

Add `visibility` and `opacity` as paint-time culling signals,
following the pattern established by ADR-030 (feature-class
flips add CSS strings to `viewport_css`; ADR-032 adds CSS
properties that paint respects).

### 1. Parse `visibility` and `opacity` into ComputedStyle

Both already flow through the cascade as declarations. No
parser change needed — `ComputedStyle::get("visibility")` and
`ComputedStyle::get("opacity")` already return the raw values.

This ADR adds *interpretation*:
- `visibility: hidden | collapse` → skip drawing this subtree
  (children inherit — CSS spec: `visibility` is inherited)
- `visibility: visible` → draw normally (and explicitly re-enable
  painting even if an ancestor is hidden)
- `opacity: 0` (parsed as float) → skip drawing this subtree
- `opacity: 0.00001..1` → draw normally (v1: no alpha blending;
  approximated as opaque)
- `opacity: var(...)` → treat as **1** (no variable resolution
  for opacity in v1; deferred)

### 2. Paint respects visibility and opacity

`paint.cpp` gains two checks per box, before drawing:

```cpp
// ADR-032: paint-authoritative visibility/opacity culling.
// Layout already skips display:none subtrees, but paint must
// also skip visibility:hidden (which affects paint but not
// layout per CSS spec) and opacity:0 subtrees.
bool subtree_visible = true;
if (b.element) {
    if (const auto it = styles.find(b.element);
        it != styles.end()) {
        const auto& cs = it->second;
        if (auto v = cs.get("visibility")) {
            if (*v == "hidden" || *v == "collapse") {
                subtree_visible = false;
            }
        }
        if (subtree_visible) {
            if (auto o = cs.get("opacity")) {
                try {
                    if (std::stof(*o) <= 0.0001f) {
                        subtree_visible = false;
                    }
                } catch (...) {}
            }
        }
    }
}
if (!subtree_visible) return;  // skip entire subtree
```

**Paint signature change:** `paint_box(const LayoutBox&, cairo_t*)`
needs access to `ComputedStyleMap`. Current signature doesn't
pass it. Options:
- (a) Add `styles` parameter to every paint call — bigger diff
- (b) Store a `styles_` reference on `RenderPipeline` and pass
  through `paint(cairo_t*, styles)` — one API change
- (c) Precompute a `visible` flag on each `LayoutBox` during
  layout, then paint reads the flag — no signature change

**Choose (c).** During `layout_element`, compute
`box.visible` from `ComputedStyle::get("visibility")` and
`get("opacity")`. Paint reads `b.visible` as a boolean. Zero
API churn, single source of truth (layout decides, paint
respects).

### 3. `LayoutBox` gains a `visible` bool (default true)

```cpp
struct LayoutBox {
    // ... existing fields ...
    bool visible = true;  // ADR-032: paint culling signal
    // ...
};
```

Set during `layout_element` after ComputedStyle is looked up.
`false` if `visibility: hidden | collapse` OR `opacity: 0`.
Children are not filtered individually — `LayoutBox::visible`
is a subtree flag: paint skips the entire subtree when false.

`visibility: visible` on a descendant of a `visibility: hidden`
ancestor is **not** treated as re-enabling in v1 (documented
limitation; requires per-node override tracking).

## Design decisions

### Why a `visible` flag on LayoutBox rather than passing styles to paint

Two reasons:
1. **API stability.** `paint(cairo_t*)` is called from
   `RenderPipeline::paint`, from tests, from the window. Adding
   a `ComputedStyleMap` parameter would require touching all
   call sites.
2. **Single source of truth.** Layout already reads
   `ComputedStyle` for display, position, float, etc. Adding
   `visible` there keeps all ComputedStyle-derived state on
   `LayoutBox`, not split between layout and paint.

**Trade-off:** when `RenderPipeline::restyle()` runs (hover
changes), it re-runs the cascade. If visibility changes for a
hover state, we'd need to update `visible` on the tree. That's a
future extension — v1 assumes visibility is viewport-state-only
and doesn't change under hover.

### Why not implement opacity blending

Opacity blending requires either Cairo groups (`cairo_push_group`
+ `cairo_pop_group_to_source` + `cairo_paint_with_alpha`) or
per-node alpha on every draw operation. Both are ~50-100 lines
of paint churn. And in Wikipedia's CSS, `opacity` is used for:
- Button disabled state (0.5-0.7)
- Hover transitions (0.87, 1)
- Modal overlays (0.x)

None of these are in the first viewport without user
interaction. **v1 rule: treat any `opacity >= 0.0001` as
opaque.** Skip the subtree at `opacity <= 0.0001`. This gets
the "skip invisible content" benefit without the blending
churn.

### Why not implement visibility re-enabling

CSS spec: `visibility` is inherited, but `visibility: visible`
on a descendant of a `visibility: hidden` ancestor is
**visible** (unlike `display: none` which is fully
subtree-hiding). Implementing this correctly requires walking
the tree with a "currently hidden by ancestor" flag and
re-enabling on any `visibility: visible` override.

Wikipedia uses this pattern in exactly **one** place: the
`.mw-jump-link:not(:focus)` rule (which we already override
with `!important`). No other `visibility: visible` re-enabling
appears in Wikipedia's CSS. Deferred to a future ADR if it
becomes necessary.

### Why this ADR exists even though it fixes nothing visible

Two reasons:

1. **Paint-authoritative culling is a prerequisite for
   `:checked` dropdowns.** The 14 `:checked` rules in Wikipedia
   use `opacity: 1; visibility: visible` to reveal dropdown
   content on click. When ADR-033 (structural `:checked`) lands,
   the initial state (dropdown hidden) will be `visibility:
   hidden; opacity: 0`. Without paint culling, the dropdown
   content would still paint (overlapping the page). This ADR
   makes the initial state paint correctly.

2. **ADR-031's PNG overlap is (probably) a paint bug.** If
   paint is drawing a `display:none` subtree that layout
   already skipped, a `visible` check will fix it. If the PNG
   was stale, this ADR costs nothing and never regresses.

**Honest acceptance:** if this ADR doesn't fix ADR-031's
apparent overlap, the cause is deeper than `visibility`/
`opacity` (paint traversal bug, stale render, or `shift_subtree_x`
recursion) and requires its own diagnostic cycle.

## Deferred (ADR-033+ candidates)

- **`:checked` pseudo-class** — 14 rules, gates dropdown state
- **`position`** — 47 rules (22 relative / 16 absolute / 3 fixed
  / 2 sticky)
- **`:not()`** — general selector negation
- **`clip` / `clip-path`** — visual clipping
- **`z-index`** — paint order for positioned elements
- **`opacity` alpha blending** — Cairo groups or per-draw alpha
- **`visibility: visible` re-enabling** — descendant override of
  hidden ancestor
- **Per-hover `visibility` updates** — assume viewport-state-only
  in v1
- **`opacity: var(...)` resolution** — no variable resolution
  path in v1; treated as opaque

## Consequences

**Better:**

- **Paint is authoritative about visibility/opacity.** Matches
  CSS semantics for `visibility: hidden` (layout doesn't skip
  it; paint does).
- **Precondition for `:checked` dropdowns.** ADR-033's dropdown
  content will paint correctly (hidden by default, shown on
  `:checked`).
- **Fixes (probably) ADR-031's PNG overlap** if the cause is
  paint drawing a hidden subtree.
- **Preserves 408-test suite** — paint's `visible` default true
  means no behavior change for existing tests.
- **Zero impact on layout tree** — `visible` is a paint-only
  hint, not a layout constraint.

**Accepted limitations:**

- **`opacity` no alpha blending.** Opaque for any value > 0.
- **`visibility: visible` doesn't override hidden ancestor.**
  Documented; only 1 use in Wikipedia's CSS (skip link, already
  worked around).
- **Hover-state visibility not tracked.** `restyle()` doesn't
  update `visible`. Deferred to a future ADR when hover-gated
  visibility becomes needed.
- **`opacity: var(...)` treated as opaque.** Deferred.

**Unchanged:**

- All prior ADRs (018-031).
- Layout algorithm, cascade, feature-class flips.

## Implementation plan

Two commits on `feat/visibility-opacity`:

1. **`visible` field on `LayoutBox` + set during layout +
   paint culling** — add `bool visible = true` to `LayoutBox`;
   compute from `visibility`/`opacity` in `layout_element`;
   early-return in `paint_box` when false. Add 3-4 tests
   (visibility:hidden skips paint, opacity:0 skips paint,
   default visible, both propagates to children).
2. **Docs** — ADR-032 status → Implemented, Post-ADR-032 note
   in ADR-031 recording whether the paint fix resolved the
   apparent overlap, CHANGELOG, README row.

## Implementation

**Implemented 2026-10-26.** Two PRs (spec #171, impl #172).
Small structural follow-up.

### What shipped

- `LayoutBox::visible` (default true), ADR-032 comment
- `layout_element` sets `visible=false` for `visibility:hidden |
  collapse` OR `opacity <= 0.0001` (parsed via `std::stof`;
  catch-all treats `var()` as opaque)
- `paint_box()` early-returns on `!box.visible` -- single choke
  point (both root at line 80 and children at line 50 route
  through it)
- 6 tests in `[layout][adr-032]`

### The measurement that reframed ADR-031's "known bug"

After ADR-032 shipped, paint culling didn't change the PNG.
That led to a `find_leak` diagnostic walking the layout tree
for every box with content at `x<500, y<900` (first viewport,
left column).

Result: **60 boxes, every single one legitimate article
content**:

| Element | y | w | What |
|---|---|---|---|
| `<h1>#firstHeading>` | 84 | 948 | Article title "HTML" |
| `<table>.box-Multiple_issues>` | 259 | 948 | Maintenance banner 1 |
| `<table>.box-Howto>` | 305 | 948 | Maintenance banner 2 |
| `<table>.box-More_citations_needed>` | 351 | 948 | Maintenance banner 3 |
| `<table>.box-Copy_edit>` | 420 | 948 | Maintenance banner 4 |
| `<p>#mwEg>` | 497 | **590** | Intro paragraph 1 |
| `<p>#mwHw>` | 613 | **590** | Intro paragraph 2 |
| `<p>#mwJg>` | 729 | **590** | Intro paragraph 3 |

**No navbox. No sidebar. No leaked infobox content. The intro
paragraphs are correctly narrowed to w=590 beside the infobox
at x=914.** The layout tree is clean.

### The misdiagnosis, corrected

ADR-031 shipped with a "known paint overlap" note in its commit
message. The apparent overlap at x=324 in PNGs was **article
intro text being misread as nav-style content**:

- The HTML article's intro legitimately mentions HTML5, XHTML,
  text/html, WHATWG -- those strings appear in the first-
  viewport paragraphs
- When the intro paragraphs' lines wrap (4-5 lines each at
  Pango's 23px line-height), the visual density resembles a
  nav list at a glance
- **It was my misreading, twice.** The earlier leak_locate
  diagnostic (which searched for the same strings) already
  showed all infobox-related hits at `y >= 4317`, way below
  the fold. That was the signal that should have stopped the
  investigation.

**Same pattern as ADR-028's grid misdiagnosis**: measurement
catches the misread claim, correction is preserved in the
record, no rewrite.

### Structural value delivered (independent of the misreading)

Even with the misreading corrected, ADR-032 delivers its
intended structural value:

1. **Paint is now authoritative about visibility/opacity.**
   Before this ADR, `paint_box` drew every box without checking
   -- if layout skipped a `display:none` child, paint wouldn't
   (defense-in-depth gap). Now paint respects the layout-
   computed `visible` flag.
2. **Precondition for future `:checked` dropdowns.** The 14
   `:checked` rules use `visibility:visible` to reveal content.
   The initial hidden state now paints correctly.
3. **Pattern established** for future culling (`z-index`,
   `clip-path`, `position` once those land).

### v1 limitations shipped

- No `opacity` blending (any value > 0 drawn opaque)
- `visibility: visible` does not re-enable on a hidden-ancestor
  descendant (only 1 use in Wikipedia's CSS, already worked
  around with `!important`)
- `opacity: var(...)` treated as opaque (no variable resolution
  in v1)

### Test count

- Before ADR-032 Commit 1: 408 cases / 1692 assertions
- After Commit 1: **414 cases / 1710 assertions** (+6 / +18)
- Full suite all pass, zero warnings

## Verification

**Unit tests** (new `[paint][adr-032]` tag):

- `visibility: hidden` subtree not painted
- `visibility: visible` subtree painted
- `opacity: 0` subtree not painted
- `opacity: 0.5` subtree painted (v1: opaque)
- `opacity: var(--x)` subtree painted (var doesn't resolve,
  treated as opaque)
- Default (no visibility/opacity) painted
- Nested: hidden parent hides visible child (v1 limitation)

**Visual verification:** re-render Wikipedia PNG at viewport
1774. If the infobox overlap is gone → ADR-031's apparent bug
was paint culling. If still there → paint traversal bug, needs
its own diagnostic.

## Acceptance criteria

ADR-032 is complete when:

- [ ] `visible` field added to `LayoutBox`, defaults true
- [ ] `visibility: hidden | collapse` sets `visible = false`
- [ ] `opacity: 0` sets `visible = false`
- [ ] Paint early-returns when `visible == false`
- [ ] 4-6 unit tests added and passing
- [ ] Visual verification attempted (fixes-or-doesn't, both
      documented)
- [ ] ADR-031 Post-ADR-032 note updated with paint result
- [ ] `main` is green: 408 + N cases, all pass, zero warnings
- [ ] ADR-032 status → Implemented in Commit 2

**Explicit claim**: paint respects `visibility: hidden` and
`opacity: 0`. Any element (or subtree) with either is skipped
during paint.

**Honest caveat**: **this ADR may not fix ADR-031's apparent
overlap.** The measurement says all suspected leaked fragments
are at y >= 4317 (outside first viewport). If the visual overlap
persists after this ADR, the cause is not visibility/opacity
culling — it's a deeper issue (stale render, paint traversal
bug, or `shift_subtree_x` recursion). That diagnostic becomes
its own cycle.

**Second honest caveat**: this ADR's **visible delta on
Wikipedia today is near-zero** (2 visibility uses, both already
handled by ADR-029's feature flip). Its primary value is
preconditioning ADR-033 (`:checked` dropdowns) and establishing
paint-authoritative culling.
