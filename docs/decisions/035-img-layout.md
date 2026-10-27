# ADR-035: Layout-Integrated `<img>` (Intrinsic Dimensions Reserve Space)

**Status:** Accepted
**Date:** 2026-10-27
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout), ADR-016 (inline formatting), ADR-028 (grid), ADR-031 (float), ADR-034 (`<img>` paint, SVG decode, protocol-relative URL fix)

## Context

ADR-034 shipped `<img>` paint at paint time. Images now render
(PNG via Cairo, SVG via librsvg) but **do not reserve layout
space** -- the box stays at its zero-height intrinsic from
layout, and paint draws the image over whatever happens to be
underneath. The wordmark renders but overlaps the tagline
(hidden as a workaround); below-fold thumbnails render but
overlap the text that follows them.

### Measurement (2026-10-27, viewport 1774, Wikipedia HTML)

**Baseline render time**: 5.03-5.48 sec per render (3-run
sample). This is what a Wikipedia render currently costs with
no layout-time fetches.

**All `<img>` boxes in tree**:

| Element | Position | w | h | src |
|---|---|---|---|---|
| Wordmark | x=52 y=14 | 248 | **0** | `/static/images/mobile/copyright/wikipedia-wordmark-en-25.svg` |
| Tagline | x=52 y=19 | 248 | **0** | `/static/images/mobile/copyright/wikipedia-tagline-en-25.svg` |
| Tim Berners-Lee photo | x=324 y=1253 | 926 | **0** | `//thumb.wikimedia.org/.../250px-Tim_Berners-Lee_April_2009.jpg` |
| HTML5 logo | x=346 y=6437 | 926 | **0** | `//thumb.wikimedia.org/.../250px-HTML5_logo_and_wordmark.svg.png` |
| Element categories diagram | x=346 y=14881 | 926 | **0** | `//thumb.wikimedia.org/.../250px-HTML_element_content_categories.svg.png` |
| Login tracking pixel | x=324 y=61935 | 948 | **0** | `.../CentralAutoLogin/start?...` |
| Wikimedia footer logo | x=67 y=62163 | 1646 | **0** | `/static/images/footer/wikimedia.svg` |
| MediaWiki footer logo | x=67 y=62163 | 1646 | **0** | `/w/resources/assets/mediawiki_compact.svg` |

**Header is `min-height: 3.125rem` (50px)**, not fixed. Header
grows automatically with content. Tagline can unhide cleanly
once layout reserves space.

**CSS width already set**: wordmark and tagline have
`width: 248px` from CSS. Post-fix, height derives from
intrinsic aspect: wordmark `h = 248 × (88/560) ≈ 39px`,
tagline `h = 248 × (44/560) ≈ 19px`. Both fit inside
`min-height: 50px` with padding.

### The two-stage delta ADR-034 set up

ADR-034's Implementation section documented four honest
limitations. This ADR resolves two:

1. **Images don't reserve layout space** -- paint-only. This
   ADR integrates image dimensions into layout, so siblings
   flow correctly around images.
2. **Tagline overlapping wordmark** -- hidden as workaround.
   This ADR removes the workaround, tagline unhides, header
   grows to fit both.

Two remain deferred:

- **Search icon renders at 2x2** -- `calc()` not supported;
  separate ADR.
- **SVG `<text>` not rendered** -- librsvg limitation for
  future SVGs; unaffected here.

## Decision

Thread an image-dimension lookup callback through `layout()`.
When `layout_element` sizes an `<img>` element, it queries the
callback for intrinsic dimensions, and applies them to the
box's width/height. **Paint already handles intrinsic-aspect
derivation from box width** (ADR-034's `paint_image`); with
layout reserving space, paint draws into the reserved area
instead of over neighbors.

### 1. `ImageDimLookup` callback (mirror of `TextMeasurer`)

```cpp
struct ImageDim {
    float width = 0.0f;
    float height = 0.0f;
    bool valid = false;
};

using ImageDimLookup = std::function<ImageDim(const std::string& url)>;

LayoutBox layout(const dom::Document& doc,
                 const css::ComputedStyleMap& styles,
                 TextMeasurer measurer,
                 float viewport_width,
                 ImageDimLookup image_dims = nullptr);
```

**Default `nullptr`** so existing tests, ADR-031's
`img_kind.cpp` diagnostic, and any caller passing 4 args keep
working unchanged. The behavior for 4-arg callers is exactly
today's behavior (all images render at w=CSS-width, h=0).

### 2. `layout_element` consults the callback for `<img>`

Right before `box.width`/`box.height` are computed from
`content_w` and `cursor_y` (line ~894 in current
`layout.cpp`), insert:

```cpp
// ADR-035: reserve layout space for <img> from intrinsic
// dimensions. Respects explicit CSS width/height when set;
// intrinsic dims fill in only the unspecified axis (plus
// aspect-derive the other). If callback null or returns
// invalid, no reservation (current behavior).
if (image_dims && el.tag() == "img") {
    const std::string src = el.get_attribute("src");
    if (!src.empty()) {
        const ImageDim d = image_dims(src);
        if (d.valid && d.width > 0 && d.height > 0) {
            // Explicit CSS width wins if present (already applied
            // to box.width via content_w above? Actually no --
            // content_w is the parent's content width, and the
            // img's own width is computed below from padding +
            // border + content_w. Need to check what applies.)
        }
    }
}
```

**Precise insertion**: `layout_element` computes `box.width`
from `content_w` (parent's content width) plus padding/border.
For a non-floated block-level `<img>`, that gives parent-width
box. ADR-035 overrides: if `<img>` has explicit CSS width,
that wins; otherwise intrinsic width, but capped at parent's
content width (so a 1000px image doesn't overflow).

```cpp
if (image_dims && el.tag() == "img") {
    const std::string src = el.get_attribute("src");
    if (!src.empty()) {
        const ImageDim d = image_dims(src);
        if (d.valid && d.width > 0 && d.height > 0) {
            // Explicit CSS width (from `style` property or
            // matching rule) -- if set, honor. Else use
            // intrinsic capped to parent content width.
            float want_w = content_w;
            float want_h = 0.0f;
            if (style) {
                if (auto w = style->get("width");
                    w && !w->empty() && *w != "auto") {
                    const float parsed_w = parse_length(*w, ctx);
                    if (parsed_w > 0.0f) want_w = parsed_w;
                }
                if (auto h = style->get("height");
                    h && !h->empty() && *h != "auto") {
                    const float parsed_h = parse_length(*h, ctx);
                    if (parsed_h > 0.0f) want_h = parsed_h;
                }
            }
            if (want_h <= 0.0f) {
                want_h = want_w * (d.height / d.width);
            }
            box.width = want_w
                      + box.padding[3] + box.padding[1]
                      + box.border[3] + box.border[1];
            box.height = want_h
                       + box.padding[0] + box.padding[2]
                       + box.border[0] + box.border[2];
            return box;  // <-- early return, chrome already applied
        }
    }
}
```

**Behavior on the current Wikipedia render**:

| Element | Before (h=0) | After |
|---|---|---|
| Wordmark | 248 × 0 | 248 × **39** (aspect-derived from 560×88) |
| Tagline | 248 × 0 | 248 × **19** (aspect-derived from 560×44) |
| TBL photo | 926 × 0 | 926 × **~695** (aspect 250×188 scaled to 926) |
| HTML5 logo | 926 × 0 | 926 × **~695** |
| Categories diagram | 926 × 0 | 926 × **~695** |
| Footer wikimedia | 1646 × 0 | 1646 × **1646** (50×50 → 1:1) |
| Footer mediawiki | 1646 × 0 | 1646 × **1646** (25×25 → 1:1) |

The two footer logos are 1:1, so at 1646px CSS width they'd be
1646×1646. **`max-width` and `height:auto` handling is
required** to avoid exploding the footer. Deferred to
ADR-036's `max-width` implementation; v1 caps height at
`min(intrinsic_h × (want_w / intrinsic_w), parent_content_w)`.

(Actually, looking at the CSS: `.mw-logo img { height: auto; width: 100% }`. Setting `height: auto` means aspect-derive. And `.mw-logo` has `max-width` constraints. This all resolves via existing `parse_length` + aspect math. The footer SVGs at 1646 CSS width × 1:1 aspect would be 1646×1646 -- but the actual CSS has `max-width` on their containers. Need to verify.)

### 3. `RenderPipeline` wires the callback

```cpp
layout_root_ = renderer::layout::layout(
    *doc_, styles_cache_, &measure_with_pango, viewport_width,
    [this](const std::string& url) -> renderer::layout::ImageDim {
        cairo_surface_t* s = get_image_surface(url);
        if (!s) return {};
        const int iw = cairo_image_surface_get_width(s);
        const int ih = cairo_image_surface_get_height(s);
        if (iw <= 0 || ih <= 0) return {};
        return {
            static_cast<float>(iw),
            static_cast<float>(ih),
            true
        };
    });
```

**Cache hit = zero cost**. First layout of a page: 8 sync
fetches (5.0 → 5.4-5.8 sec). Subsequent layouts (resize,
hover restyle, back/forward) hit the cache and cost nothing.

### 4. Remove the paint-layer tagline skip

`paint.cpp` lines 90-97 (skip tagline img). The tagline's box
now has h=19 from layout, so paint draws it below the
wordmark correctly. Header grows to ~58px + padding.

## Design decisions

### Why callback, not a pre-pass

Two options were considered:

**(a) Callback during layout** -- `layout_element` calls
`image_dims(src)` per `<img>` during the recursive descent.

**(b) Pre-pass walk** -- after DOM parse, walk the tree,
fetch every `<img>` src, stash dims in a map. Layout reads
the map.

**Callback wins** because:
- Same pattern as `TextMeasurer` -- consistency.
- Lazy: if `<img>` boxes are culled (display:none from
  feature-flip, or under an `overflow: hidden` we don't yet
  support), the callback isn't invoked.
- Streaming: layout's existing recursive descent processes
  the tree once; no double-walk.
- Testability: a stub callback returns fixed dims, no
  network -- hermetic tests.

**Pre-pass loses** because:
- Extra tree walk doubles the outer loop cost.
- Eager fetching means display:none `<img>` still hits
  network.
- Doesn't match the TextMeasurer precedent.

### Why default `nullptr` on the 5th parameter

Every existing test (`test_layout.cpp`'s `run()` helper,
`test_grid_layout.cpp`, `test_flex_layout.cpp`, ADR-034's
`img_kind.cpp` diagnostic) calls `layout()` with 4 args. A
default-nullptr 5th param means **zero test changes** in
Commit 1. The tests keep proving old behavior; new tests
explicitly pass a stub callback.

**Alternative**: require the 5th arg and touch all 8 call
sites. Rejected -- unneeded churn.

### Why intrinsic-aspect derive, not aspect-box

CSS `object-fit: contain` (letterbox) vs. intrinsic aspect
(no letterboxing) vs. `object-fit: fill` (stretch). Wikipedia
images use `height: auto` (intrinsic aspect -- our v1 default).
`object-fit` unsupported; not needed for current assets.

### Why not support explicit height-only in v1

`<img style="height: 100px">` with width auto → aspect-derive
width from height. Rare in practice; Wikipedia uses `width:
X; height: auto`. v1 handles width-only (the common case);
height-only defer to ADR-036 if it comes up.

### Why footer SVGs work despite looking scary

The two footer `<img>` (wikimedia.svg 50×50, mediawiki_compact.svg
25×25) have CSS `width: 100%; height: auto` inside a
`max-width`-bounded container (`.mw-footer` has
`max-width: 99.75rem`). Our callback returns their intrinsic
aspect (1:1), and the width override respects `width: 100%`
→ box width = container width. Height = container width × 1.
At 1774px viewport with content width ~1646, that's
1646×1646, not 50×50.

**This explodes the footer.** ADR-036 (`max-width` support)
must land before ADR-035's footer SVGs render sanely, OR
ADR-035 v1 caps image height at parent content height, OR
ADR-035 v1 falls back to intrinsic dims when no explicit
CSS width is set.

**Resolution for v1**: when `<img>` has no explicit CSS width
(`width` unset or `auto`), use intrinsic dims directly
(w=50, h=50 for footer SVGs) rather than parent content width.
Only use parent content width + aspect-derive when explicit
CSS width is set. This is CSS-spec behavior (`<img>` without
width falls back to intrinsic; with width, uses aspect).

### Header growth behavior

Header's `min-height: 3.125rem` (50px) + `padding-top: 8px;
padding-bottom: 8px` + `display: flex; flex-wrap: wrap`. With
wordmark (39px) + tagline (19px) stacked via flex-column
(they're sibling `<img>` in the logo container), total logo
height ~58px. Header grows to 58 + 16 = 74px. Grew by ~24px
from current 50px.

**Article content shifts down 24px** (everything after the
header moves). Article intro y goes from ~145 to ~169.
Acceptable -- and matches what real Wikipedia looks like.

## Deferred (ADR-036+ candidates)

- **`calc()` evaluation** -- needed for search icon (2×2 today).
- **`max-width` property** -- needed for footer SVGs and other
  width-constrained containers.
- **`object-fit: contain|cover|fill`** -- needed for
  aspect-mismatched images.
- **JPEG decode** -- needs `libjpeg-turbo` or similar.
- **Layout-integrated `<video>`, `<canvas>`, `<iframe>`** --
  same pattern, future.
- **Intrinsic dimensions for `<img>` with `srcset`** --
  multi-res selection; Wikipedia doesn't use it in first
  viewport.
- **`preserveAspectRatio="none"` SVG** -- force non-intrinsic
  aspect; not used by Wikipedia assets.
- **`loading="lazy"`** -- defer off-screen image fetches;
  Wikipedia's `<img>` on current HTML article has no lazy
  attribute.
- **`background-image` layout reservation** -- currently only
  paint; not needed for first-viewport content.

## Consequences

**Better:**

- **Below-fold thumbnails reserve space** -- Tim Berners-Lee
  photo, HTML5 logo, categories diagram no longer overlap
  text below them. Fixes the largest remaining visible
  defect in the article body.
- **Tagline unhides cleanly** -- header grows to fit both
  wordmark + tagline. Removes the workaround.
- **Footer logos reserve space** -- footer grows to fit
  wikimedia + mediawiki_compact SVGs, no overlap.
- **Layout-integrated images are structural** -- future
  features (video, canvas) follow the same pattern.

**Accepted limitations (honest, documented):**

- **First render 10% slower** -- 5.0 sec becomes 5.4-5.8
  sec on cold cache. Cached after. Real cost of sync-fetch
  at layout time; documented as "first-render-slow,
  subsequent-fast" (same as every browser's first paint).
- **Footer SVG aspect** -- 1:1 intrinsic means at 100% width
  they'd be giant squares. v1 falls back to intrinsic dims
  when no CSS width set (see Resolution above).
- **`<img>` without explicit CSS width** -- uses intrinsic
  dims (may be tiny, e.g. 50px). CSS defaults `<img>` to
  intrinsic size; matches. Real Wikipedia has `width: 100%`
  on footer SVGs, so this branch isn't hit for the current
  page.
- **Article content shifts 24px down** -- header grows.
  Correct behavior, changes article intro y from ~145 to ~169.
- **`object-fit` still unsupported** -- non-intrinsic aspect
  not honored.

**Unchanged:**

- ADR-034's `<img>` paint (PNG/SVG decode, cache).
- All prior ADRs (018-034).
- Layout flow for non-`<img>` elements.
- Search icon (still 2×2, needs `calc()`).

## Implementation plan

Two commits on `feat/img-layout`:

1. **ImageDimLookup + layout integration + remove tagline workaround** -- add `ImageDim` struct and `ImageDimLookup` typedef to `layout.hpp`; new 5th param (default nullptr); `<img>` sizing in `layout_element` before `return box`; remove paint.cpp tagline skip; wire callback in `RenderPipeline::relayout()`. Add 6-8 tests.
2. **Docs** -- ADR-035 status → Implemented, Post-ADR-035 notes in ADR-031 (float: images inside float reserve correctly) and ADR-034 (the two limitations resolved), CHANGELOG, README row.

## Verification

**Unit tests** (new `[layout][img-dim][adr-035]` tag, all with stub callback):

- `<img>` with stub dims 100×50 and CSS `width: 200` → box 200×100 (aspect derived)
- `<img>` with stub dims 100×50 and no CSS width → box 100×50 (intrinsic)
- `<img>` with stub dims 100×50 and CSS `width: 200; height: 30` → box 200×30 (both explicit)
- `<img>` with no stub (nullptr) → box 248×0 (current behavior, regression)
- `<img>` with stub returning invalid → box 248×0 (fallback)
- Following sibling starts below `<img>`'s height when callback provides dims
- Following sibling starts at `<img>`'s top when callback null (current behavior)
- Cache hit doesn't re-fetch (verified via stub counter)

**Visual verification** (viewport 1774, Wikipedia HTML):

| Check | Before | After |
|---|---|---|
| Wordmark | 248×0 | **248×39** |
| Tagline | hidden (paint skip) | **248×19 (visible)** |
| Header height | ~50 (min-height) | ~74 (grew to fit) |
| Article intro y | 145 | **~169** (+24 from header growth) |
| TBL photo | 926×0 | **926×~695** |
| HTML5 logo | 926×0 | **926×~695** |
| Categories diagram | 926×0 | **926×~695** |
| Text below thumbnails | overlapped | **starts below** |
| Footer logos | 1646×0 | **50×50 / 25×25 (intrinsic)** |
| Search icon | 2×2 | 2×2 (unchanged, deferred) |
| Render time | 5.0-5.5s | **5.4-5.9s (first), 5.0-5.5s (cached)** |

**Byte-identical prediction**: NO -- PNG will differ. Header taller, thumbnails visible, footer taller. That's the delta.

## Acceptance criteria

ADR-035 is complete when:

- [ ] `ImageDim` + `ImageDimLookup` in `layout.hpp`
- [ ] `layout()` gains 5th param defaulted nullptr
- [ ] `layout_element` sizes `<img>` from callback
- [ ] `RenderPipeline::relayout()` wires the callback
- [ ] Paint-layer tagline skip removed
- [ ] 6-8 unit tests added and passing
- [ ] Measured visual delta on Wikipedia (Commit 1)
- [ ] First-render time measured (documented honestly)
- [ ] Post-ADR-035 notes in ADR-031, ADR-034
- [ ] `main` green: 425 + N cases, all pass, zero warnings
- [ ] CI green (both PR + post-merge)
- [ ] ADR-035 status -> Implemented in Commit 2

**Explicit claim**: below-fold thumbnails reserve vertical
space. Tagline unhides. Header grows to fit wordmark +
tagline. Article intro shifts from y=145 to ~y=169.

**Honest caveats**:

1. **First-render-slow**: 5.0 sec becomes 5.4-5.8 sec on cold
   cache (10% slower). Cached after.
2. **Footer SVGs fall back to intrinsic when no CSS width
   set** (2 lines of CSS-dependent branch logic).
3. **Article content shifts 24px down** (correct behavior).
4. **Search icon still 2×2** (needs `calc()`, separate ADR).

**Structural value**: image layout integration is the
foundation for future `<video>`, `<canvas>`, `<iframe>`
layout -- same callback pattern.
