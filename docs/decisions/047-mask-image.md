# ADR-047: CSS Mask Image

**Status:** Accepted
**Date:** 2026-10-29
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-016 (background paint), ADR-034 (background-image and ImageProvider), ADR-037 (calc()), ADR-045 (inline backgrounds, established mask-image deferral)

## Context

ADR-045's measurement found 14 rules using `background-size`, but
noted that **13 of them target `mask-image`, not
`background-image`**. ADR-045 deferred `mask-image` to its own ADR
with the note: "different subsystem (clip + fill with color)".

This ADR adds that subsystem.

### Measurement (Wikipedia CSS, 2026-10-29)

**127 rules reference `mask-*` properties.** Full breakdown:

| Category | Count |
|---|---|
| Rules **with** `background-color` (fill present, renderable) | **14** |
| Rules **without** `background-color` (fill via companion rules) | 113 |
| Data URL mask images | 13 |
| External SVG mask images | 1 |
| Other URL / unparsed | 113 |

**Rendering pattern**: `.vector-icon` base rule sets
`background-color` and `mask-image: url(data:image/png;base64,...)`
(a 1×1 transparent PNG placeholder). Per-icon subclasses like
`.mw-ui-icon-wikimedia-eye` override `mask-image` with the real
icon. Multiple selectors match the same element, so the companion
fill applies.

**Real renderable masks** (14 combinations of mask + fill):

| Selector | First-viewport | Notes |
|---|---|---|
| `.cdx-message__icon` (and variants) | ✅ **Visible** | Issue-box icon, y≈400 at viewport 1774 |
| `.cdx-search-input__start-icon` | ⚠️ Header dependent | Search field icon |
| `.vector-icon` + `.mw-ui-icon-*` | ⚠️ Header dependent | Theme selector, hamburger, dropdown chevrons |
| `.vector-dropdown-label::after` | ⚠️ Header dependent | Dropdown chevrons |

**`.cdx-message__icon` is the first-viewport target.** Everything
else depends on ADR-029's header flex layout being complete.

**No existing `mask-*` parsing** in the codebase (grep confirmed).

### What mask-image does (CSS Masking 1 §5)

`mask-image: url(m)` uses image `m` as an alpha mask. The
element's `background-color` fills the box; `m`'s alpha channel
determines where that fill shows through:

- alpha = 1 → fill visible
- alpha = 0 → transparent
- alpha = 0.5 → half-opacity fill

v1 assumes **alpha mode** (default). `mask-mode: luminance` is
**deferred** (0 uses in target).

## Decision

Add `mask-image` parsing + paint. Support `mask-size` (fixed
length), `mask-position` (keyword pairs), `mask-repeat`
(no-repeat only), with `-webkit-` prefix acceptance. Both
`background-image` and `mask-image` can be present simultaneously
(different CSS subsystems — both render).

### Parse: `mask-*` properties (both `-webkit-` and unprefixed)

**`mask-image` / `-webkit-mask-image`**:
- Extract `url(...)` target (same helper as ADR-034's
  background-image)
- Handle `url(data:image/svg+xml;utf8,<svg...>)` — raw inline SVG
  passes through to ImageProvider unchanged
- Handle `url("data:image/svg+xml;...")` — quoted form
- `none` → no mask (skip)

**`mask-size` / `-webkit-mask-size`**:
- `auto` → intrinsic (bg_size_w = 0)
- Fixed length (`calc(var(--font-size-medium,1rem) + 4px)`,
  `0.857em`, `16px`, `20px`) → `parse_length`
- `cover` / `contain` / percentage → 0 (deferred; same as ADR-045)

**`mask-position` / `-webkit-mask-position`**:
- Keyword pairs (`center`, `left top`, `center right`) → same
  `BgPosX` / `BgPosY` enums as ADR-045
- Percentage / length forms → deferred (0 uses in target)

**`mask-repeat` / `-webkit-mask-repeat`**:
- `no-repeat` → true
- `repeat` / `repeat-x` / `repeat-y` → false (deviation; same as
  ADR-045; 0 uses in target)

**Priority**: if both `mask-image` and `-webkit-mask-image` are
present (some Wikipedia rules have both for older browser
support), use unprefixed. Same for size / position / repeat.

### LayoutBox fields

```cpp
// ADR-047: CSS mask (alpha channel from image, fill = background)
std::string mask_image_url;      // empty = no mask
float mask_size_w = 0.0f;        // 0 = auto (intrinsic)
float mask_size_h = 0.0f;        // 0 = auto (aspect from width)
BgPosX mask_pos_x = BgPosX::Left;
BgPosY mask_pos_y = BgPosY::Top;
bool mask_no_repeat = false;
bool mask_has = false;           // gates paint
```

Parsed in `layout_element` alongside `background-image` /
`background-color` / `box-shadow` / `border-radius` (same style
block).

### Paint: fill background, mask it, composite

```cpp
void paint_mask_image(cairo_t* cr, const layout::LayoutBox& box,
                      const ImageProvider* images) {
    if (box.mask_image_url.empty()) return;
    if (!images || !images->get_surface) return;
    cairo_surface_t* surf = images->get_surface(box.mask_image_url);
    if (!surf) return;
    // Compute mask draw rect (same logic as paint_image, but
    // mask_has is true only when caller ensured background_color
    // exists -- see below).
    // Apply mask using Cairo: cairo_set_source_surface at computed
    // size/position, then cairo_mask or a pattern.
}
```

### Two-phase draw: fill then mask

`mask-image` in CSS uses `background-color` as the fill. The
paint is:
1. Fill `box` with `background_color` (already done by
   ADR-016's existing `fill_rect`).
2. Mask that fill with `mask_image_url` (new paint step).

Cairo approach: use `cairo_mask_surface(cr, mask_surf, x, y)` after
setting source to `background_color`. This multiplies the current
source (bg) through the mask's alpha, compositing the result.

**Order of operations** — mask draws after background,
before border/children:

```
1. box-shadow (ADR-043)
2. background fill (ADR-016)
3. **mask (ADR-047)** — new
4. border (ADR-016)
5. background-image (ADR-034)
6. <img> (ADR-034)
7. clip_children (ADR-039)
8. children (recursive)
```

**Why mask after background, before border**: mask affects the
background fill, not the border. Matches CSS spec order.

### v1 limitation: no fill = no paint

`.cdx-message__icon` and `.vector-icon` both have
`background-color` rules. **Other elements with mask but no fill
(from color inheritance)** are deferred — v1 renders only mask
when `background_color.a > 0`. Documented deviation.

**Why**: extracting fill from `color` requires resolving `color`
through inheritance (which works today for text, but `mask-image`
consumer requires knowing "does this element have a fill?"). Full
implementation would paint bg-color regardless (transparent bg
still means no mask visible). For v1, skip mask when
`background_color.a == 0` — no visual artifact, no regression.

## Design decisions

### Why two-step fill-then-mask vs single cairo_mask

Cairo `cairo_mask_surface` multiplies the *current source* through
a mask pattern. It doesn't fill with a color and mask that fill —
it masks whatever `cairo_set_source_*` produced.

**Approach**: 
1. `cairo_set_source_rgba(bg_color)`, `cairo_paint()` → solid fill
2. `cairo_set_source_surface(mask_surf)`, `cairo_mask(pattern)` → 
   intersect with mask

This is two Cairo calls but matches CSS semantic order. Alternative
(single `cairo_mask` with color source) is more efficient but
requires the mask pattern to be the source's clip — different
direction.

Actually the cleanest is:
```cpp
cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
cairo_mask_surface(cr, mask_surf, x, y);
```

`cairo_mask_surface` masks the *current source* (which is our
bg color) through the surface's alpha. Single call, exactly the
CSS semantics. This is what we'll use.

### Why `display: inline-block` falls to Block (good here)

`.vector-icon` sets `display: inline-block`. `display_of()` in
`layout.cpp` recognizes `inline`, `none`, `grid`, `flex`; falls
to `Block` for anything else. That's **correct for mask painting**
— mask applies to a box, which is what `LayoutBox` represents.
Inline-level mask painting on `LayoutInline` would be a different
path (deferred; not exercised by these targets).

### Why `-webkit-` prefix acceptance matters

Many rules set **both** `mask-image` and `-webkit-mask-image`
(same value, browser compatibility). Our parser reads both;
unprefixed wins if both present. Cost: ~4 extra lines of parse
code. Benefit: rules that only have `-webkit-` form (rare but
possible) still work.

### Why `.cdx-message__icon` is the first-viewport target

The issue box (`.mw-message-box` / `.cdx-message`) is at y≈400
in the first viewport (verified by ADR-040, ADR-043 screenshots).
Its `.cdx-message__icon` child — a small info/warning icon — gets
a mask-based icon. First-viewport delta depends on whether the
icon element exists in fetched HTML AND its parent chain renders.

**Likely visible**: "This article has multiple issues" box should
show a small icon at its left edge.

**Header icons depend on ADR-029**: theme selector, hamburger,
dropdown chevrons need header flex layout (ADR-029) to be
properly positioned. Their mask-image will render once the box
positions are correct.

### What's NOT in scope (deferred)

- **`mask-mode: luminance`** (v1 alpha mode only; 0 uses in
  target — SVG masks are always alpha)
- **`mask-composite`** (0 uses)
- **`mask-clip` / `mask-origin`** (0 uses; v1 uses box content
  box matching `background-clip` default)
- **`mask-border`** (0 uses)
- **Percentage `mask-size` / `mask-position`** (0 uses in target)
- **`mask-repeat: repeat | repeat-x | repeat-y`** (0 uses in
  target; all no-repeat)
- **`-webkit-mask-*` prefixed-only rules** (0 in target; both
  prefixes present together)

## Consequences

**Better:**
- **Issue-box icon appears** (`.cdx-message__icon`,
  first-viewport target)
- **Header icons render** once ADR-029 completes (currently
  deferred; mask painting correct, positioning deferred)
- **Mask subsystem complete** — matches `background-image` +
  `mask-image` patterns standard across sites
- **`.vector-icon` fills with color** — the base icon template
  renders shape (real per-icon SVG comes from subclass rules)

**Accepted limitations:**
- **Header icons depend on ADR-029** — 3 of 4 target categories
  need header flex layout
- **No fill = no paint** — elements with mask but no
  background-color (via color inheritance) deferred
- **`mask-mode: luminance` deferred** — 0 uses in target
- **Percentage `mask-size` / `mask-position` deferred**
- **Repeat variants deferred**
- **`mask-composite` / `mask-border` / `mask-clip` deferred**

**Unchanged:**
- All ADR-005 through ADR-046
- `background-image` (ADR-034) — different subsystem, both
  render
- Border-radius (ADR-044) — mask applies to rounded background
  correctly (mask is a separate layer, ignores box shape)

**Possible regression risks (to verify in Commit 2):**
- `cairo_mask_surface` call may not clear previous source state
  after paint — save/restore needed around mask call
- Mask should NOT affect border/children (only background) —
  verified by drawing sequence

## Implementation plan

Three commits, same discipline as ADR-040 through ADR-046:

1. **Parse + layout fields** (behavior-neutral):
   - `LayoutBox` gains `mask_image_url`, `mask_size_w/h`,
     `mask_pos_x/y`, `mask_no_repeat`, `mask_has`
   - Parse `mask-*` and `-webkit-mask-*` in
     `layout_element`'s style block
   - Priority: unprefixed wins if both present
   - Extract `url(...)` including `data:image/svg+xml;utf8,<svg...>`
     raw form and quoted variants
   - Zero paint changes — no visible delta
   - ~12 parse tests

2. **Paint** (visible):
   - New `paint_mask_image(cr, box, images)` helper
   - `paint_box` calls it after background fill, before border
   - `cairo_mask_surface` with current source = `background_color`
   - **PNG verification gate before merge** (first-viewport if
     `.cdx-message__icon` renders; scroll-PNG if only header
     icons appear)
   - Zero new unit tests (paint verified via PNG)

3. **Docs**:
   - ADR-047 status -> Implemented
   - Post-ADR-047 notes in ADR-034 (background-image vs
     mask-image subsystems), ADR-045 (mask-image deferral
     closed)
   - README row + CHANGELOG

## Verification

**Unit tests (Commit 1, `[layout][mask][adr-047]`):**

Parse:
- `mask-image: url(x.svg)` -> `mask_image_url = "x.svg"`,
  `mask_has = true`
- `-webkit-mask-image: url(x.svg)` (only) -> same
- both `mask-image` and `-webkit-mask-image` ->
  `mask_image_url` from unprefixed
- `mask-image: none` -> `mask_has = false`
- `mask-size: 16px` -> `mask_size_w = 16.0f`
- `mask-size: auto` -> `mask_size_w = 0.0f`
- `mask-position: center right` -> `mask_pos_x = Right`,
  `mask_pos_y = Center`
- `mask-position: left top` -> defaults
- `mask-repeat: no-repeat` -> `mask_no_repeat = true`
- `mask-repeat: repeat` -> false (deviation)
- `data:image/svg+xml;utf8,<svg...` (raw) -> url captured
- `url("data:image/...")` (quoted) -> url captured

**Visual verification (Commit 2, PNG before merge):**

| Check | BEFORE | AFTER |
|---|---|---|
| `.cdx-message__icon` (issue box icon) | No icon visible | **Small icon at left edge of "multiple issues" box** (first-viewport) |
| `.vector-icon` (theme selector) | (depends on header) | Same as before or icon appears once header layout completes |
| No regression: title underline (042), toolbar line (043), issue box border (040) | Present | Present |
| No unintended icons in body text | -- | -- |

**Verification category**: **First-viewport PNG** IF `.cdx-message__icon` renders. Fallback to **scroll-PNG** if only some other icon becomes visible.

**ADR-039 lesson codified**: render and inspect PNG BEFORE merge.

## Acceptance criteria

ADR-047 is complete when:

- [ ] 7 mask-* fields on LayoutBox
- [ ] `mask-image` parsed (url + data URL forms)
- [ ] `mask-size` parsed (fixed length; auto default)
- [ ] `mask-position` parsed (keyword pairs; flexible order)
- [ ] `mask-repeat` parsed (no-repeat only)
- [ ] `-webkit-mask-*` prefixes accepted
- [ ] `mask-image: none` clears mask
- [ ] `paint_mask_image` uses `cairo_mask_surface`
- [ ] `paint_box` calls mask paint after background, before
      border
- [ ] PNG verification: `.cdx-message__icon` appears
      (first-viewport) OR at least one masked icon visible
      (scroll-PNG)
- [ ] No regression: 586 pre-existing tests still pass
- [ ] 12+ unit tests
- [ ] Post-ADR-047 notes in ADR-034 and ADR-045
- [ ] `main` green: 586 + N cases, all pass
- [ ] ADR-047 status -> Implemented

**Explicit claim**: `.cdx-message__icon` renders as a mask-filled
icon in the first-viewport (issue box icon). Header icons
(theme selector, hamburger, chevrons) render as mask-filled once
header layout (ADR-029) positions them correctly.

**Honest caveats:**
1. **Header-dependent targets**: 3 of 4 renderable categories
   require ADR-029 header layout. `.cdx-message__icon` is the
   only guaranteed first-viewport target.
2. **No fill = no paint**: elements with mask but no
   `background-color` (via color inheritance) deferred.
3. **`mask-mode: luminance` deferred** (0 uses in target;
   SVG masks are alpha)
4. **`mask-composite`, `mask-border`, `mask-clip` deferred**
5. **Percentage `mask-size` / `mask-position` deferred**
