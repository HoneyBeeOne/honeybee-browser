# ADR-045: Inline Background Images (background-size / position / repeat)

**Status:** Accepted
**Date:** 2026-10-29
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-016 (inline formatting), ADR-018 (link state), ADR-034 (background-image on block boxes), ADR-044 (border-radius)

## Context

ADR-034 renders `background-image: url()` on **block**
elements. Inline elements (`<a>`, `<span>`) get no background
image paint -- `LineFragment`s hold text and per-fragment
element identity (ADR-018 Commit 4b) but paint only draws
glyphs, not backgrounds.

This ADR adds background image painting for inline elements,
scoped to what Wikipedia CSS actually uses.

### Measurement (Wikipedia CSS, 2026-10-29)

**14 rules use `background-size`, 15 use `background-position`,
15 use `background-repeat`** -- but the breakdown is decisive:

| Category | Count | Renders via ADR-045? |
|---|---|---|
| `background-size` on **`mask-image`** targets | **13** | No -- mask-image is a separate subsystem, not implemented |
| `background-size` on **`background-image`** target | **1** | **Yes** -- the only renderable rule |
| `background-position: center` (mask targets) | 13 | No |
| `background-position: center -16px` (hover, mask) | 1 | No |
| `background-position: center right` | 1 | Yes (the a.external rule) |
| `background-repeat: no-repeat` | 15 | All targets of this value |

**The one renderable rule** (`.mw-parser-output a.external`):

```css
background-image: url(/w/.../link-external-small-ltr-progressive.svg);
background-position: center right;
background-repeat: no-repeat;
background-size: 0.857em;
padding-right: 1em;
```

**Article HTML: 242 elements match `.external`** -- external
link arrows next to Wikipedia's citations of `WHATWG`,
`CERN`, `ISO`, etc.

**First `.external` at char 108015** (~3000 words into the
article, in the History section). **Not in first viewport.**

### Honest scope note

This ADR produces **zero first-viewport delta.** Delta is
visible only after scrolling into the article body to reach
the first external link -- roughly 4-5 scroll heights at
typical viewport.

The "verification" is scroll-based PNG: BEFORE (scroll to
first `.external` link, no icon) / AFTER (same scroll
position, icon appears next to external link text). This is
**equivalent rigor** to first-viewport PNG for a feature whose
natural targets are inside article content, not page chrome.

The "no first-viewport delta" is documented honestly in
ADR-045's Implementation section. It's the same disclosure
pattern as ADR-044's "visually subtle at 2px" -- the delta
exists, verifiable, but not where ADR-044 checked.

### What's NOT in scope (deferred)

**`mask-image` (13 of 14 background-size targets)** -- a
different CSS subsystem. Wikipedia uses `mask-image` extensively
for icon coloring (Vector dropdown chevrons, search icons,
progressive-link arrows in a hover state). Rendering
`mask-image` requires a separate paint mechanism (clip + fill
with color). Own ADR if needed.

**Percentage `background-size`, `background-position`
percentage offsets, `background-repeat: repeat | repeat-x |
repeat-y`** -- 0 uses in renderable rules. Deferred.

**Multi-layer backgrounds** (`background-image: url1, url2`)
-- 0 uses in target. Deferred.

## Decision

Add inline-element background-image painting with
`background-size` (fixed length), `background-position`
(keyword pairs), `background-repeat` (`no-repeat` only)
support. Fragment-level background data; paint draws before
glyphs.

### LineFragment gains background fields

```cpp
struct LineFragment {
    // ... existing fields ...
    std::string bg_image_url;   // empty when no background image
    float bg_size_w = 0.0f;     // 0 = auto (intrinsic)
    float bg_size_h = 0.0f;     // 0 = auto (derived from aspect)
    float bg_pos_x_px = 0.0f;   // resolved horizontal offset from fragment origin
    float bg_pos_y_px = 0.0f;   // resolved vertical offset from fragment origin
    bool  bg_no_repeat = false; // no-repeat flag
};
```

**Why per-fragment fields (not a side map)**:

- `LineFragment::element` already points to the source inline
  element (ADR-018 Commit 4b); the fragment is the natural
  unit that has both position and per-element style.
- Paint iterates fragments; reading fields is direct.
- Alternative (side `map<const Element*, InlineBackground>`)
  requires threading the map through `paint_box` /
  `paint_children` / `paint_image` -- signature change ripples.
- Fragment bloat: ~30 bytes per fragment × ~200 fragments in
  target page = ~6KB. Negligible.

### Layout: populate fragment background fields

During line-breaking, when a `LineFragment` is produced for
text whose source `element` has `background-image` in
`ComputedStyleMap`, populate the 6 fields:

1. `bg_image_url` = element's `background-image` url() value
2. `bg_size_w`, `bg_size_h` from `background-size`:
   - Single value: `w = parse_length(v)`, `h = 0` (auto,
     aspect-derived)
   - Two values: `w = parse_length(tok0)`, `h = parse_length(tok1)`
   - `auto`: both 0
   - `cover` / `contain`: **deferred** (0 uses in renderable
     rules)
3. `bg_pos_x_px`, `bg_pos_y_px` from `background-position`
   keywords (0 uses of the resolved-length form in renderable
   rules):
   - `center right` → `x = frag.width - icon_width` (resolved
     in paint), `y = (frag.height - icon_height) / 2`
   - `center center` → both centered
   - `left top` → both 0 (static fallback)
   - **Percentages / lengths**: deferred
4. `bg_no_repeat` = true if `background-repeat: no-repeat`
   (or any value containing `no-repeat`)

**Inline padding**: `.mw-parser-output a.external` uses
`padding-right: 1em`. Our flattening path may or may not
honor inline-element padding. If it doesn't, the icon
overlaps the text (bad). **Commit 1's diagnostic** confirms
whether `flatten_inline_to_words` adds padding to fragment
width. If not, Commit 1 extends it -- `flatten_inline_to_words`
becomes padding-aware; padding right is added to fragment
width, allowing the icon to render in the reserved gap. This
is an honest scope addition if needed.

### Paint: draw inline background before glyphs

In `paint.cpp`'s `LayoutKind::InlineContext` handler:

```cpp
case LayoutKind::InlineContext: {
    const auto& ctx = static_cast<const LayoutInlineContext&>(*child);
    // ADR-045: draw inline background-images before glyphs.
    for (const auto& frag : ctx.lines) {
        if (!frag.bg_image_url.empty()) {
            paint_inline_image(cr, frag, frag.bg_image_url, images);
        }
    }
    // Existing: draw glyphs.
    for (const auto& frag : ctx.lines) drawer(cr, frag);
    break;
}
```

New helper in `paint.cpp`:

```cpp
void paint_inline_image(cairo_t* cr,
                        const layout::LineFragment& frag,
                        const std::string& url,
                        const ImageProvider* images) {
    if (!images || !images->get_surface) return;
    cairo_surface_t* surf = images->get_surface(url);
    if (!surf) return;
    const float iw = cairo_image_surface_get_width(surf);
    const float ih = cairo_image_surface_get_height(surf);
    if (iw <= 0.0f || ih <= 0.0f) return;

    // Resolve size: auto if bg_size_w == 0, else explicit;
    // height aspect-derived if bg_size_h == 0.
    float pw = frag.bg_size_w > 0.0f ? frag.bg_size_w : iw;
    float ph = frag.bg_size_h > 0.0f ? frag.bg_size_h
             : (frag.bg_size_w > 0.0f
                ? frag.bg_size_w * (ih / iw)
                : ih);

    // Resolve position: bg_pos_x_px / bg_pos_y_px were
    // computed during layout, but they depend on pw/ph
    // (e.g., "center right" needs icon_width to compute x).
    // Recompute here to avoid layout->paint feedback.
    float ix = frag.x + frag.bg_pos_x_px;
    float iy = frag.y + frag.bg_pos_y_px;
    // For "center right": bg_pos_x_px was stored as
    //   frag.width (right edge) minus icon_width.
    // For "center center": bg_pos_x_px was stored as
    //   (frag.width - icon_width) / 2.
    // Layout stores *pre-resolved* fragment-relative offsets,
    // paint applies them directly. (Detailed mapping in
    // Implementation section.)

    cairo_save(cr);
    cairo_translate(cr, ix, iy);
    cairo_scale(cr, pw / iw, ph / ih);
    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}
```

**Positioning simplification**: rather than storing raw offsets
and re-resolving in paint (which requires knowing icon
dimensions at layout time, requiring image loads during
layout -- bad), store **keyword-based position** in the
fragment and resolve fully in paint:

```cpp
enum class BgPosX { Left, Center, Right };
enum class BgPosY { Top, Center, Bottom };
// ... fields ...
BgPosX bg_pos_x = BgPosX::Left;
BgPosY bg_pos_y = BgPosY::Top;
```

Paint computes:
- `right`: `ix = frag.x + frag.width - pw`
- `center` (x): `ix = frag.x + (frag.width - pw) * 0.5f`
- `left`: `ix = frag.x`
- Same for y.

This cleanly separates: layout produces keywords; paint resolves against computed icon dimensions. No image loading during layout.

### `background-repeat: no-repeat` only

All 15 rules use `no-repeat`. If a rule uses `repeat` (0 uses
today), v1 draws it as no-repeat and documents the deviation
(own ADR when a real case appears).

## Design decisions

### Why fragment-level fields (not box-level)

`<a class="external">` is inline. It becomes a `LayoutInline`
(flattened into `LineFragment`s during line-breaking), **not a
`LayoutBox`**. There's no box to attach fields to. The
fragments ARE the painted representation. Per-fragment fields
mirror ADR-018's per-fragment `element` pointer -- same
architectural pattern.

### Why keyword position (not raw pixel offsets)

Layout doesn't have icon dimensions (image loads happen in
paint, not layout). So layout can't compute `x_px = frag.width
- icon_width` without loading the image -- which would be a
layout/paint feedback loop. Storing keywords (`Right` +
`Center`) preserves the CSS semantic. Paint resolves against
the icon it just loaded.

**Alternative** (layout loads images via ImageDimLookup):
image_dims exists (ADR-035) for `<img>` intrinsic sizing,
but is NULL for background-image URLs today. Extending it to
accept background-image URLs is scope creep. Keyword-position
is simpler and matches the 1 rule's value shape.

### Why scope-limited to `a.external` (1 rule)

The measurement decisively shows **only 1 rule uses
background-image + size/position/repeat in the target page**.
The other 13 size rules target `mask-image` (different
subsystem). Shipping a comprehensive
`background-image`-on-inline implementation for 1 rule would
be over-engineering; shipping for the 1 rule that renders is
correctly scoped.

Future sites using `background-image` on inline elements
(badges, status indicators, decorative spans) benefit from
the same infrastructure -- but the range of `background-size`
/ `background-position` values they use is a future
measurement, not this ADR's scope.

### Why scroll-verified PNG (not first-viewport)

`.external` appears in article body content (citations, links
to W3C / WHATWG / ISO), not in page chrome. First `.external`
at char 108015, in History section. Verifying requires
scrolling 4-5 viewport heights into the article. **The
delta is unambiguous when reached** -- icon either appears
(16×14px next to external link text) or doesn't. This is
equivalent rigor to first-viewport PNG for a feature whose
targets live inside article content.

### Why `mask-image` deferred (13 of 14 size rules)

`mask-image` is a different CSS property: it uses a
greyscale image as an alpha mask, filled with a color. That
requires a paint mechanism distinct from `background-image`
(Cairo would need `cairo_mask` or a clip + fill). Ship
`mask-image` in its own ADR when the use case justifies it.

## Deferred

- **`mask-image`** (13 of 14 background-size rules) -- own ADR
- **`background-size: cover | contain`** -- 0 uses in
  renderable rules
- **Percentage `background-size` / `background-position`** --
  0 uses in renderable rules
- **`background-repeat: repeat | repeat-x | repeat-y`** --
  0 uses in renderable rules (all no-repeat)
- **Multi-layer backgrounds** (`url1, url2`) -- 0 uses
- **`background-clip` / `background-origin`** -- 0 uses in
  renderable rules
- **`background-attachment: fixed`** -- 0 uses

## Consequences

**Better:**
- **242 external link icons appear** next to `WHATWG`, `CERN`,
  `ISO`, etc. throughout the article (verified by scroll PNG)
- **Inline background infrastructure** ready for future sites
  using decorative inline backgrounds
- **`background-size` / `background-position` /
  `background-repeat` parsing** complete for the 1 rule that
  uses them via background-image

**Accepted limitations:**
- **`mask-image` not implemented** -- 13 of 14 background-size
  rules target mask-image (Vector icons, dropdown chevrons,
  magnify icons). Zero delta from those.
- **First-viewport delta: zero** -- all external links are in
  article body content past char 108015. Verifiable only after
  scroll.
- **`cover` / `contain` / percentage size / repeat** deferred

**Unchanged:**
- All ADR-016 through ADR-044 features
- Block-element background-image (ADR-034) -- unchanged; this
  ADR extends to inline elements
- `LayoutInline` / `LayoutInlineContext` structures preserved
  (only `LineFragment` gains fields)
- `paint_box` / `paint_image` signatures unchanged (inline
  paint is a new helper called from `paint_children`'s
  `InlineContext` case)

**Possible regression risks (to verify in Commit 2):**
- Inline fragments now have 6 new fields. Any code that
  constructs fragments manually (rare, mostly tests) may need
  default initialization (defaults are safe -- empty
  `bg_image_url` = no background, so paint skips).
- `paint_children` InlineContext case iterates fragments
  twice (once for backgrounds, once for glyphs). Traversal
  cost: O(2 × fragments) vs. O(fragments). Negligible for
  typical articles.

## Implementation plan

Three commits, same discipline as ADR-040 through ADR-044:

1. **Parse + fragment fields** (behavior-neutral):
   - `LineFragment` gains 6 background fields + 2 keyword enums
   - `flatten_inline_to_words` (or the line-breaking site
     that populates `LineFragment::element`) populates
     background fields from `ComputedStyleMap`
   - **Diagnostic-first**: confirm where `frag.element` is
     assigned; if inline padding is not currently honored,
     extend `flatten_inline_to_words` to add padding_right to
     fragment width (scope addition, documented in Commit 1
     message)
   - Zero paint changes -- no visible delta
   - ~10 parse tests (background-size / position / repeat
     parsing into fragment fields)

2. **Paint** (visible, scroll-verified):
   - New `paint_inline_image()` helper in `paint.cpp`
   - `paint_children`'s `InlineContext` case draws backgrounds
     before glyphs
   - `background-position` keywords resolve in paint against
     loaded icon dimensions
   - **Scroll-position PNG verification** (BEFORE / AFTER at
     first `.external` link, roughly 4-5 scroll heights down)
   - ~5 tests (layout-side fragment-field assertions; paint
     verified via scroll PNG)

3. **Docs**:
   - ADR-045 status -> Implemented
   - Post-ADR-045 note in ADR-034 (inline extension)
   - README row + CHANGELOG

## Verification

**Unit tests (Commit 1, `[layout][inline-bg][adr-045]`):**

- `<a class='external'>text</a>` with `background-image: url(x);
  background-size: 16px; background-repeat: no-repeat` ->
  fragment has `bg_image_url = "x"`, `bg_size_w = 16`,
  `bg_no_repeat = true`
- `background-position: center right` -> `bg_pos_x = Right`,
  `bg_pos_y = Center`
- `background-position: left top` -> both default (`Left` +
  `Top`)
- Malformed size (`background-size: foo`) -> `bg_size_w = 0`
- `background-repeat: repeat` -> `bg_no_repeat = false`
  (documented deviation: v1 doesn't tile)
- `<a>` with no background-image rule -> `bg_image_url`
  empty, paint skipped
- Inline padding: verify padding-right extends fragment width
  (if Commit 1 extends flatten_inline_to_words); regression:
  text fragments without padding unchanged
- Text-only fragments in same inline context unaffected by
  sibling fragment's background

**Visual verification (Commit 2, scroll PNG before merge):**

| Check | BEFORE | AFTER |
|---|---|---|
| First `.external` link icon | No icon | **16×14px icon next to "WHATWG" or similar text** |
| Second `.external` link | No icon | Icon appears |
| Non-external links | No change | No change |
| Block backgrounds (ADR-034) | Existing | Unchanged (regression check) |
| Title underline (ADR-042) | Present | Unchanged |
| Toolbar line (ADR-043) | Present | Unchanged |
| Issue box border (ADR-040) | Present | Unchanged |

**Scroll position for verification**: scroll to roughly 4-5
scroll-heights down (until the `History` or `Markup` section
is visible). First external link appears next to `WHATWG`
in the History section. Screenshot BEFORE and AFTER at that
same scroll position.

**ADR-039 lesson codified**: render and inspect PNG (at
scroll position for this ADR) BEFORE merge.

## Acceptance criteria

ADR-045 is complete when:

- [ ] 6 background fields on `LineFragment` + 2 keyword enums
- [ ] `background-size` parsed (fixed length; auto default)
- [ ] `background-position` parsed (keyword pairs)
- [ ] `background-repeat` parsed (no-repeat only)
- [ ] Fragment production populates fields from
      `ComputedStyleMap`
- [ ] Inline padding handling confirmed or extended
      (documented in Commit 1 message)
- [ ] `paint_inline_image` draws before glyphs in
      `InlineContext` case
- [ ] **Scroll PNG verification: `.external` icons appear
      next to external links**
- [ ] No regression: block backgrounds, title underline,
      toolbar line, issue box border all preserved
- [ ] 10+ unit tests
- [ ] Post-ADR-045 note in ADR-034
- [ ] `main` green: 562 + N cases, all pass
- [ ] ADR-045 status -> Implemented

**Explicit claim**: **242 external link icons appear**
next to external links in article body content, verified
by scroll-position PNG comparison. First-viewport delta:
**zero** (all targets are article body content, not page
chrome). `mask-image` (13 of 14 background-size rules in
Wikipedia CSS) is deferred to its own ADR.

**Honest caveats:**
1. **Zero first-viewport delta** -- verifiable only after
   scroll. Documented prominently.
2. **`mask-image` deferred** -- 13 of 14 background-size
   rules in Wikipedia CSS target mask-image, not
   background-image. Zero delta from those.
3. **`background-size: cover | contain` deferred** -- 0
   uses in renderable rules
4. **`background-repeat: repeat` deferred** -- 0 uses in
   renderable rules (all no-repeat)
5. **Percentage size / position** -- 0 uses in renderable
   rules
