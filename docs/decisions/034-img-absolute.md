# ADR-034: `<img>` Element Paint + `position: absolute` Completion

**Status:** Accepted
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
