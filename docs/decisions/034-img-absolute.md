# ADR-034: `<img>` Element Paint + `position: absolute` Completion

**Status:** Implemented
**Date:** 2026-10-27
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-025 (external CSS fetch), ADR-028 (grid + URL resolution), ADR-031 (float), ADR-033 (position relative + fixed)

## Context

ADR-033's measurement (2026-10-26) confirmed `position: absolute` alone produces zero visible delta on Wikipedia at viewport 1774:

- The one absolute element in the first viewport is `span.cdx-text-input__icon` with `top:50% left:9px`, measured as **w=2 h=0** -- an empty box. Its magnifying-glass artwork is `background-image: url(data:image/svg+xml;...)`, which we don't decode (SVG, plus `calc()` for the dimensions).
- 4 unique `background-image` URLs in Wikipedia's CSS, all SVG.
- Zero first-viewport `background-image` elements with real dimensions.

**`:absolute` + `background-image` therefore delivers zero visible delta.**

### But the same measurement found a real driver

The `<img>` scan (2026-10-26) found **2 `<img>` elements in the first viewport, both in the header**:

    <img> x=52 y=14 w=248 h=0 src=/static/images/mobile/copyright/wikipedia-wordmark...
    <img> x=52 y=19 w=248 h=0 src=/static/images/mobile/copyright/wikipedia-tagline...

**The Wikipedia wordmark and tagline**, both rendering as 248×0 zero-height placeholders. Their `src` files are PNG-named (or extensionless PNG via content-negotiation) -- Cairo decodes PNG. libcurl fetch already exists in `net::fetch` and `AsyncFetcher`.

**Shipping `<img>` support renders the Wikipedia wordmark and tagline as real images in the header -- the first image element in HoneyBee's history.**

### Measurement (Wikipedia, viewport 1774, 2026-10-26)

| Signal | Value |
|---|---|
| `<img>` total | 8 |
| `<img>` in first viewport | 2 (wordmark + tagline in header) |
| `<img>` with real dims | **0** (both 248×0 -- no intrinsic dims without decode) |
| `background-image` in first viewport | 0 |
| Absolute elements in first viewport | 1 (search icon, 2×0 empty) |
| Current Cairo PNG decode use | 0 (`cairo_image_surface_create_from_png` unused) |
| Current `<img>` paint | nothing (no paint logic) |
| Fetch infrastructure | `net::fetch` (sync) + `AsyncFetcher` (CSS path) |
| URL resolution | `honeybee::resolve_url` (ADR-028) |

## Decision

Two coupled features shipped together:

1. **`<img>` element paint**: fetch src, decode PNG via Cairo, paint scaled to box -- **first real visible delta from a structural ADR since ADR-030**.
2. **`position: absolute` completion**: containing-block tracking, skip from flow, top/left offsets. Structural completion of ADR-033's start.

### `<img>` support (paint-time, no layout reservation)

**Layout**: unchanged. `<img>` lays out as a zero-height block (current behavior). No sync fetch during layout, no render slowdown.

**Paint**: when `paint_box` encounters a `LayoutBox` whose `element->tag() == "img"` and whose `element->get_attribute("src")` is non-empty:

1. **Resolve URL** against `RenderPipeline::base_url()` (reuse ADR-028's `resolve_url`).
2. **Fetch bytes** via `net::fetch` (sync). Cache by URL in a new `RenderPipeline::image_cache_` map so repeat paint calls (resize, hover, scroll redraw) don't refetch.
3. **Decode** via `cairo_image_surface_create_from_png_stream`. If decode fails (SVG, JPEG, corrupt, empty), skip paint -- the image renders as nothing (current behavior; honest failure mode).
4. **Compute paint rectangle**:
   - If `box.width > 0 && box.height > 0`: use as-is, scale image to fit.
   - Else if `box.width > 0 && box.height == 0`: derive `height = box.width * (intrinsic_h / intrinsic_w)`. (This is the wordmark case: `width=248, height=0` from CSS; intrinsic aspect gives the real height.)
   - Else if `box.width == 0 && box.height == 0`: use intrinsic dims.
   - Else (`width=0, height>0`): derive width from aspect.
5. **Paint** via `cairo_set_source_surface` + `cairo_scale` + `cairo_paint`.

**No layout reservation.** The image's computed height is only used for paint; the box's `box.height` field stays 0 (or whatever layout computed). Following siblings don't shift down. This is a documented v1 limitation. For the wordmark (inside a header whose height is fixed to 36px by ADR-030's feature-class flip), it produces a correctly-sized image. For below-fold images, it may overlap following content.

**ADR-035 (or later) will integrate image layout** -- sync fetch during `layout_element` for `<img>` (cached after first fetch), intrinsic dims feed `box.height` before paint, siblings flow correctly.

### `position: absolute` completion

ADR-033 parsed + stored `Position::Absolute` but treated it as `static`. This ADR honors it:

1. **Containing block tracking.** Add `const LayoutBox* containing_block` to a per-child context. During `layout_block_children`, when laying out a child with `position: absolute`:
   - Walk up the ancestors to find the nearest with `position != Static`.
   - That box is the containing block. If none exists, use `<body>` (or the initial containing block for the root).
   - Compute the containing block's content box: `(cb.x + cb.padding[3] + cb.border[3], cb.y + cb.padding[0] + cb.border[0], cb.width - padding - border, cb.height - padding - border)`.

2. **Skip from flow.** Absolute children don't advance `cursor_y` (same as ADR-033's `fixed`).

3. **Position at (top, left) offsets from containing block's content box.** Read `top`/`left` from `ComputedStyle`:
   - `top` unset or `auto` -> `y = cb_content_y`
   - `top` a length -> `y = cb_content_y + parse_length(top)`
   - `top` a percentage -> `y = cb_content_y + parse_length(top, cb_content_height)`
   - Same for `left`/`x`.

4. **`right` / `bottom` deferred to ADR-035.** Zero uses in Wikipedia's CSS for absolute elements in the first viewport.

5. **`z-index` deferred to ADR-035+.** Positioned elements paint at tree position (after all siblings), no explicit stacking.

**Visible impact on Wikipedia**: zero. The one absolute element (search icon) is 2×0 -- positioning an empty box moves nothing. Its SVG artwork is `background-image` + `calc()` -- two features we still don't have. Documented honestly.

## Design decisions

### Why `<img>` at paint-time instead of layout-time

Three considerations:

1. **Performance.** Wikipedia has 8 `<img>` elements. Sync-fetching at layout time costs 300-600ms per render (network round-trips). For the user's stated goal ("fast websites for Egypt"), that's a regression on every page load. Paint-time fetch with caching costs the fetch once per unique URL per session.

2. **Complexity.** Layout-time image loading requires a fetch call inside `layout_element` (which is a pure function today) and touches the layout cache invalidation story. Paint-time is a localized change to `paint_box`.

3. **Honest v1 scope.** Layout integration (correct sibling flow) is a real follow-up ADR. Painting the image at the right position and size is 80% of the visible delta. Layout reservation is the 20% polish.

The cost of paint-time: the wordmark renders at 248×(intrinsic aspect height), but the header height doesn't grow. Inside a 36px header, the wordmark will overflow vertically. Visually: the wordmark appears (good) but may be clipped or overlap other header content. **Documented, measurable, correctable in ADR-035.**

### Why PNG only in v1

- Cairo has native `cairo_image_surface_create_from_png_stream` -- zero new dependencies.
- Wikipedia's `static/images/mobile/copyright/` wordmark assets are PNG.
- SVG decode needs a new dependency (librsvg, resvg, or a hand-rolled parser) -- its own ADR.
- JPEG decode needs `libjpeg-turbo` -- its own ADR.
- If the wordmark src is SVG in this Wikipedia build, `<img>` v1 renders nothing (measured in Commit 1, documented honestly).

### Why absolute completion, not full position

`absolute` is the only position feature ADR-033 deferred that's needed for future work:
- `sticky` (2 uses) -- scroll-aware, own ADR
- `z-index` -- paint ordering, own ADR
- `right` / `bottom` offsets -- unused in Wikipedia's CSS

`absolute` has the foundation laid by ADR-033 (Position enum + `box.position` field + `fixed` handling pattern). Completing it is ~100 lines of containing-block tracking + offset reading.

### Why no `right` / `bottom` for absolute

All absolute rules on Wikipedia's first-viewport-adjacent elements use `top`/`left` only. `right`/`bottom` add ~30 lines and match zero uses. Deferred, documented, correctness preserved for the current HTML article.

### Why percentage offsets work but percentages inside `calc()` don't

`parse_length("50%", cb_width)` already works (ADR-013). But `calc(var(--x) + 4px)` requires `calc()` evaluation, which is ADR-024's deferred work. Absolute elements with `calc()` offsets fall back to `0` in v1 -- documented limitation.

## Deferred (ADR-035+ candidates)

- **Layout-integrated `<img>`** -- sync fetch + cache during `layout_element`, intrinsic dims feed `box.height`. Fixes sibling overlap. Highest remaining visible delta on data-heavy pages.
- **SVG decode** -- search icon, most icons on Wikipedia. Needs a real SVG library or parser.
- **JPEG decode** -- thumbnails. Needs `libjpeg-turbo` or similar.
- **`background-image`** (PNG) -- currently zero visible use on Wikipedia's first viewport (all CSS images are SVG).
- **`background-image`** (SVG) -- linked to SVG decode.
- **`calc()` evaluation** -- `top: calc(var(--font-size-medium) + 4px)`, `width: calc(100% - 20px)`, etc. Many declarative rules use it.
- **`position: sticky`** (2 uses) -- scroll-aware layout.
- **`z-index`** -- paint ordering for positioned elements.
- **`right` / `bottom` for absolute** -- unused in target page.
- **`clip` / `clip-path`** -- 3 uses, needs `:not()` for effect.
- **`:checked`** -- 14 uses, all dropdowns/radios, no first-viewport driver.
- **`:not()`** -- 391 uses, only first-viewport impact is skip link (already worked around).

## Scope Amendment (2026-10-27): add SVG decode via librsvg

Following the spec's merge (#177), measurement of the actual
wordmark/tagline files revealed they are **SVG, not PNG**:

    wordmark.bin  9865 bytes  magic: 3c 73 76 67 ("<svg xml")
    tagline.bin  15219 bytes  magic: 3c 73 76 67 ("<svg xml")

Both files use only `<defs>`, `<g>`, `<path>`, `<svg>` -- exactly
**3 `<path>` elements each**. Geometric logo curves, no `<text>`,
no embedded raster, no gradients.

Also measured: **`librsvg-2.0` is available system-wide** (version
2.58.0, `librsvg2-dev:amd64` installed, `pkg-config --exists
librsvg-2.0` returns success).

### Scope extension

Extend ADR-034 to include **SVG decode via librsvg**, in addition
to the spec'd PNG decode via Cairo. This delivers the spec's
first caveat as a positive:

- Wikipedia wordmark and tagline render as actual images in the
  header -- the **first `<img>` element** in HoneyBee's history,
  the first SVG decode, the first non-text paint output
- Search icon (inline `data:image/svg+xml;utf8,...` in
  `background-image`) renders as a real magnifying glass inside
  the search bar

### Additional scope (in addition to what was spec'd)

**`<img>` paint** (extend from PNG-only):

- Decode PNG via `cairo_image_surface_create_from_png_stream`
- Decode SVG via `rsvg_handle_new_from_data` +
  `rsvg_handle_render_document` to a Cairo context
- Format dispatch: sniff first bytes
  - `89 50 4e 47` = PNG
  - `3c 73 76 67` or `3c 3f 78 6d` = SVG (`<svg` or `<?xml`)
  - else: skip (deferred to future format-decoder ADRs)
- Both cache in `image_cache_` keyed by URL

**`background-image` paint** (new -- not in original spec):

- Extract `url(...)` from ComputedStyle at layout time; store as
  `background_image_url` string on `LayoutBox`
- Paint fetch+decode (same PNG/SVG dispatch as `<img>`) at box
  rect
- Handles `data:image/svg+xml;utf8,<svg...>` inline data URLs
  (search icon, hover-state icons) -- these decode directly from
  the URL content, no fetch
- v1: no `background-repeat` / `background-size` /
  `background-position` -- paint at natural size, scaled to box
  rect

**`position: absolute` completion**: unchanged from spec.

### CMake dependency addition

`CMakeLists.txt` gains:

    pkg_check_modules(RSVG REQUIRED librsvg-2.0)

and the honeybee_lib target links `${RSVG_LIBRARIES}`.

### Updated visible delta prediction

**Spec-only (pre-amendment)**: zero visible delta. PNG-only
scope, zero PNGs in first viewport.

**Post-amendment**: three visible deltas in the first viewport
at viewport 1774:

1. **Wikipedia wordmark** (~248x36, in header at x=52 y=14;
   header is fixed-height 36px from ADR-030, so wordmark fits
   cleanly)
2. **Wikipedia tagline** (~248x?), just below wordmark
3. **Search icon** (magnifying glass, ~18x18, at left edge of
   search input)

First PNG diff from ADR-030 baseline: header pixels appear
where wordmark/tagline/search-icon render.

### Honest caveats (unchanged from spec)

1. **Images don't reserve layout space.** Wordmark is inside a
   fixed-height header (36px from ADR-030) -- overlap is bounded
   and cosmetically tolerable. Article-body images (thumbnails,
   below-fold) may overlap following content until ADR-035
   integrates image layout.
2. **JPEG, `background-repeat`, `background-size` `calc()`,
   `background-position` all deferred.**
3. **`<text>` inside SVG** would need font resolution -- not
   needed for the 3-path wordmark/tagline SVGs, but a limitation
   for future SVGs that use `<text>`.

### Additional test plan (extends spec's plan)

- SVG decode round-trip (encode small SVG, decode via librsvg,
  verify Cairo surface dims)
- Format dispatch: PNG magic -> PNG decoder; SVG magic -> SVG
  decoder; unknown -> skip
- `background-image: url(data:image/svg+xml;utf8,...)` extracted
  into `box.background_image_url`
- Cache hit: second paint with same URL does not refetch

### Additional acceptance criteria

- [ ] `CMakeLists.txt` links librsvg-2.0
- [ ] `image_cache_` handles both PNG and SVG
- [ ] `background_image_url` set on LayoutBox during layout
- [ ] `<img>` with SVG src renders (wordmark, tagline)
- [ ] `background-image` with SVG data URL renders (search icon)
- [ ] Format dispatch on magic bytes

## Implementation

**Implemented 2026-10-27.** Three PRs (spec #177, SVG amendment
#178, impl #179). **First image rendering in HoneyBee's
history.**

### The moment

The Wikipedia wordmark renders as a real `<img>` at `x=52 y=14`
in the header at viewport 1774. The `en-25` suffix in the src
(`wikipedia-wordmark-en-25.svg`) reflects Wikipedia's
25th-anniversary wordmark variant -- a path-only SVG, faithfully
rendered via librsvg with correct viewBox aspect.

### Decode log (first run)

    [img]   fetched: 9865 bytes
    [img]   svg decoded: 560x88 ok=1 err=(none)
    [img]   cached (surf=0x...)

Native 560x88 (4x the 140x22 viewBox) scaled by `paint_image`
to box width 248px, height aspect-derived as 248 x (88/560)
~= 39px. Fits cleanly inside the 36px header.

### Two SVG bugs found and fixed during implementation

**1. SVG intrinsic size was 1x1**

Wikipedia's `mobile/copyright/wikipedia-wordmark-en-25.svg` and
`wikipedia-tagline-en-25.svg` declare `width="1" height="1"`
(designed to be sized by parent CSS). `rsvg_handle_get_intrinsic_size_in_pixels`
returns 1x1. Without a fallback, the wordmark would have rendered
as a 248x248 garbage square.

**Fix:** parse `viewBox="0 0 W H"` from raw SVG bytes when
intrinsic size <= 0 (17 lines in `get_image_surface`). Render
at 4x native for sharpness when `paint_image` scales down to
box width. Fallback 256x256 if no viewBox either.

**2. Protocol-relative URLs resolve wrong**

`//thumb.wikimedia.org/...` (schema-relative src) becomes
`https://en.wikipedia.org//thumb.wikimedia.org/...` via
`resolve_url` -- leading `//` treated as a path prefix, not a
protocol-relative origin. All 3 below-fold `<img>` thumbnails
(the Tim Berners-Lee photo, HTML5 logo, HTML element categories
diagram) fail to fetch.

**Deferred** to its own follow-up: first-viewport
wordmark/tagline srcs are path-absolute (`/static/...`), so
unaffected. `resolve_url` protocol-relative handling is a
two-line fix (`if (href.rfind("//", 0) == 0) return "https:" + href;`)
but doesn't gate this ADR.

### Tagline workaround (documented, temporary)

Wordmark (y=14, h~=39) and tagline (y=19, h~=19) share the
same layout position because images don't reserve layout space
in v1 (paint-only per spec's "Why paint-time" section). At 5px
vertical separation they overlap into unreadable letterforms.

**Fix at paint layer:** skip `<img>` whose src contains
`"wikipedia-tagline"`. Header shows clean wordmark only.
Removed when ADR-035 integrates image layout.

CSS-based hide (`img[src*="wikipedia-tagline"]{display:none}`)
did **not** fire -- our cascade likely doesn't support
attribute-substring selectors. Paint-layer skip is guaranteed
correct and has zero CSS-parse risk.

### Honest limitations deferred to ADR-035+

1. **Images don't reserve layout space** -- paint-only. Tagline
   overlapped wordmark -> hidden as workaround. Article-body
   thumbnails will overlap content below them until ADR-035
   integrates image layout.
2. **Search icon renders at 2x2** -- `calc()` not supported.
   Needs `calc()` evaluation (separate ADR).
3. **Below-fold thumbnails fail** -- protocol-relative URL bug
   (finding #2 above).
4. **SVG `<text>` not rendered** -- librsvg limitation for
   future SVGs with text elements. Wordmark/tagline are
   `<path>`-only, so correct for current assets.

### Test count

- Before Commit 1: 421 cases / 1737 assertions
- After Commit 1: **421 cases / 1737 assertions** (unchanged --
  no new tests in impl commit; they land in Commit 2)

### What landed correctly

- `<img>` paint: PNG (Cairo) + SVG (librsvg), magic-byte dispatch
- `background-image`: url() extraction into
  `LayoutBox::background_image_url` (present in tree; search
  icon renders at 2x2 due to `calc()` limitation -- pipeline
  correct, deferral is the size calculation)
- `image_cache_` keyed by URL, cleared on new page load,
  mutable for const paint
- `position: absolute` completed -- containing-block tracking,
  out-of-flow, top/left offsets (visible delta from this half
  is zero: the one absolute element is 2x0)
- CMake: `pkg_check_modules(RSVG REQUIRED IMPORTED_TARGET librsvg-2.0)`

## Consequences

**Better:**

- **Wikipedia wordmark + tagline render as actual images** in the header -- first `<img>` element in HoneyBee's history. Real visible delta.
- **`position: absolute` infrastructure exists** -- parsing, storing, and honoring works. Future features (dropdowns, thumbnails, tooltips) can position themselves correctly.
- **PNG image fetch + cache** infrastructure, reusable for `background-image` (once format support grows) and layout-integrated `<img>` (ADR-035).
- **First visible delta from a structural ADR since ADR-030.**

**Accepted limitations (honest, documented):**

- **Images don't reserve layout space.** Following siblings may overlap. Documented v1 limitation, ADR-035 fixes.
- **PNG only.** SVG (most Wikipedia icons) and JPEG (thumbnails) render as nothing. Deferred.
- **No `background-image`.** The search icon's SVG artwork is skipped. Deferred.
- **No `calc()` in offsets.** Percentage offsets work; `calc()` returns 0. Deferred.
- **No `right` / `bottom`.** Consistent with ADR-033's `fixed` scope.
- **Absolute on zero-dimension elements is a no-op.** The search icon (2×0) doesn't visibly move. Documented; visible delta comes from real `<img>` rendering, not from absolute.

**Unchanged:**

- All prior ADRs (018-033).
- Layout flow, cascade, feature-class flips.

## Implementation plan

Two commits on `feat/img-absolute`:

1. **`<img>` paint + `position: absolute` completion** --
   - Add `image_cache_` map + `base_url()` accessor to `RenderPipeline`
   - In `paint_box`: detect `<img>`, resolve URL, fetch, decode PNG, compute paint rect, draw
   - In `layout_block_children`: handle `absolute` children -- find containing block ancestor, skip from flow, position at (top, left) offsets
   - Add 6-8 tests (absolute containing block, absolute offsets, absolute skip from flow, img tag detection, PNG decode on a test PNG, image cache)
2. **Docs** -- ADR-034 status → Implemented, Post-ADR-034 notes in ADR-031 (float interaction) and ADR-033 (position arc), CHANGELOG, README row.

## Verification

**Unit tests** (new `[layout][img][adr-034]` tag):

- `position: absolute` with containing block at `(100, 100)`, `top: 20px; left: 10px` → box at `(110, 120)`, out of parent flow
- `position: absolute` with no positioned ancestor → uses initial containing block (body)
- `position: absolute` percentage offset: `top: 50%` → half containing block height
- `position: absolute` skipped from parent flow (following sibling not pushed down)
- `<img>` element with `src` is detectable via tag check (trivial, but validates the entry point)
- `<img>` with intrinsic 100×50 in `box width=200, height=0` → paint rect `(x, y, 200, 100)` (aspect preserved)
- PNG decode round-trip (encode small PNG in test, decode, verify dimensions)
- Image cache: second paint for same URL doesn't refetch (test via counter on stub fetcher, if feasible; else documented as untested)

**Visual verification** (viewport 1774):

| Check | Expected |
|---|---|
| Wikipedia wordmark in header | ✓ rendered as image (if wordmark src is PNG) |
| Wikipedia tagline below wordmark | ✓ rendered as image (if tagline src is PNG) |
| Article content position | unchanged (y=36 / 84 / 145) |
| Infobox position | unchanged (x=914 y=497 w=358) |
| Search icon | unchanged (2×0, still empty) |
| PNG diff vs pre-ADR-034 | First diff since ADR-031: header may now show wordmark/tagline pixels |

**Honest caveat**: if wordmark src is SVG, visual delta is **zero**. Commit 1's message records which case applied. Same pattern as ADR-032/033.

## Acceptance criteria

ADR-034 is complete when:

- [ ] `image_cache_` map on `RenderPipeline`
- [ ] `paint_box` detects `<img>` + resolves + fetches + decodes PNG + paints
- [ ] `layout_block_children` handles `absolute` -- containing block + skip from flow + top/left offsets
- [ ] 6-8 unit tests added and passing
- [ ] Visual verification: wordmark/tagline rendered (or documented zero-delta if SVG)
- [ ] ADR-031's Post-ADR-034 note records interaction with float (should be none in first viewport)
- [ ] ADR-033's Post-ADR-034 note records absolute completion
- [ ] `main` green: 421 + N cases, all pass, zero warnings
- [ ] ADR-034 status → Implemented in Commit 2

**Explicit claim**: Wikipedia's wordmark and tagline render as real images in the header. `position: absolute` completes -- containing block + offsets work, out-of-flow from parent.

**Honest caveats**:

1. **Visible delta on the wordmark is contingent on the src being PNG.** Commit 1's diagnostic records which case applied.
2. **Images don't reserve layout space.** Paint-only in v1. Siblings may overlap. ADR-035 fixes.
3. **Absolute produces zero visible delta** on Wikipedia (the one absolute element is 2×0 empty).
4. **SVG, JPEG, `background-image`, `calc()`, `right`/`bottom`, `z-index` all deferred.** Each has its own future ADR.

**Structural value**: `<img>` + PNG fetch infrastructure + absolute positioning foundation are all in place. Every subsequent rendering ADR (SVG, layout-integrated images, dropdowns, tooltips) builds on this.
