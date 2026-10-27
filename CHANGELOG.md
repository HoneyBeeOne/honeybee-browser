# Changelog

All notable changes to the HoneyBee Browser are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **`clear` property** (ADR-036): floats with `clear:right |
  left | both` now jump below prior same-direction floats
  instead of overlapping at the same edge; non-float blocks
  with `clear` jump below active floats. Zero visible delta
  on current pages (byte-identical PNG, layout height
  unchanged at 62295), but defensively correct for articles
  with consecutive same-side thumbnails and heading-with-
  `clear:both` section patterns. **Fifth misdiagnosis in the
  arc's correction record**: ADR-036's original spec
  hypothesized attribute selectors and floats were broken;
  diagnostic proved both already worked (attribute selectors
  since ADR-008, floats since ADR-031). The amendment (#188)
  and Implementation section preserve the correction
  alongside the original claim. **Nav overlap at x=324**
  (visible since ADR-029) identified as a separate,
  currently-unidentified element, deferred to its own
  diagnostic cycle.
- **Layout-integrated `<img>`** (ADR-035): `<img>` elements now
  reserve layout space from intrinsic dimensions via a new
  `ImageDimLookup` callback threaded through `layout()` (default
  `nullptr`, zero churn for existing tests). Explicit CSS width
  or height wins; the other axis is aspect-derived. Neither set
  → intrinsic dims (CSS spec fallback). Cache hit = zero cost;
  first render pays the 8 sync fetches at layout time (~10%
  slower first paint, cached after). **Wikipedia wordmark
  (248x39) and tagline (248x19) render correctly in the header**,
  header grows from ~50px to ~94px to fit both, tagline unhides
  (paint-layer workaround from ADR-034 removed — zero
  workarounds left in `paint.cpp`). Footer SVGs render at
  intrinsic (200x200, 100x100). Below-fold thumbnail heights are
  in the tree but don't propagate down (float v1 limitation,
  ADR-036 fixes). JPEG decode still fails (deferred since
  ADR-034). Three honest findings recorded: thumbnail
  propagation through float containers, pre-existing
  navbox/sidebar class token mismatch, JPEG decode dependency.
- **First image rendering** (ADR-034): `<img>` elements now
  paint. PNG via Cairo (`cairo_image_surface_create_from_png_stream`),
  SVG via librsvg (`rsvg_handle_new_from_data` +
  `rsvg_handle_render_document`), format-dispatched on magic
  bytes. Decoded surfaces cached per URL, cleared on page load.
  **The Wikipedia wordmark renders as the first `<img>` in
  HoneyBee's history**, at the top-left of the header, using
  viewBox aspect preservation for the 25th-anniversary
  path-only SVG asset. `background-image` url() also extracted
  and painted (search icon infrastructure works, deferred due
  to `calc()` size). `position: absolute` completed from
  ADR-033. Two SVG bugs found and documented: intrinsic size
  is 1x1 (fixed via viewBox fallback), protocol-relative URLs
  (`//host/path`) resolve incorrectly (deferred). Tagline hidden
  as documented workaround until ADR-035 integrates image
  layout.
- **Position layout: relative + fixed** (ADR-033): `Position`
  enum on `LayoutBox` (Static / Relative / Absolute / Fixed /
  Sticky). `layout_element` parses `position` from ComputedStyle.
  `layout_block_children` honors `position: fixed` -- out-of-flow,
  viewport-anchored at `(top, left)`. `relative` is a no-op (v1;
  context-only declarations in Wikipedia's CSS have zero offsets).
  `absolute` / `sticky` are stored but treated as `static` in v1;
  proper handling in ADR-034 (containing-block tracking).
  **Zero visible delta on Wikipedia at desktop 1774** --
  PNG byte-identical to pre-ADR-033, exactly as the spec
  predicted. Structural prep for ADR-034 (absolute) and future
  `z-index`.
- **Visibility + opacity paint culling** (ADR-032): `LayoutBox`
  gains a `visible` flag set during layout from `visibility:
  hidden | collapse` or `opacity: 0`. `paint_box()` early-returns
  when false. Paint is now authoritative about visibility --
  defense-in-depth against paint drawing a subtree that layout
  skipped. Structural precondition for future `:checked` dropdown
  support. Also disproved ADR-031's apparent "paint overlap"
  (diagnostic showed only legitimate article content at x<500,
  y<900; the perceived overlap was the HTML article's intro
  paragraphs mentioning HTML5/XHTML/WHATWG, misread as nav-style
  content). See ADR-031's Post-ADR-032 correction.
- **Feature-class flip table + skip-link hide** (ADR-030): extends
  ADR-029's single-flip helper into a `constexpr` table (entries
  for `main-menu-pinned` and `page-tools-pinned`) and injects
  `.mw-jump-link{display:none !important}` into the desktop viewport
  CSS. On Wikipedia this collapses the Tools panel (687px to 21px),
  moves the article intro from y=834 to y=145, and hides the "Jump
  to content" skip link. Combined with ADR-029's `#p-lang-btn`
  workaround, the first viewport now shows the compact header,
  article title "HTML" at y=84, the Article/Talk tabs with Read/View
  source/View history inline, and the intro paragraph. Principled
  fix for the skip link (`.mw-jump-link:not(:focus)` + `position:
  absolute` + `clip`) and for click-to-open dropdowns (`:checked`
  + `position: absolute`) is ADR-031.
- **Flex layout** (ADR-029): `display: flex` and `display: inline-flex`
  now parse and lay out with a single-line flex algorithm --
  `flex-direction` (row | column), `justify-content`, `align-items`,
  `flex-grow` / `flex-shrink` / `flex-basis`, and `gap` /
  `row-gap` / `column-gap`. `flex` and `flex-flow` shorthands
  expand at parse time (matching ADR-012 / ADR-028 conventions).
- **Viewport feature-class flip** (ADR-029): `RenderPipeline` now
  rewrites `vector-feature-main-menu-pinned-disabled` to
  `-enabled` on `<html>` at viewports >= 1120, mimicking what
  Wikipedia's inline JS does at page load. Without this flip,
  Wikipedia's `.vector-main-menu-landmark` renders at 711px
  (mobile hamburger menu stacked as blocks), pushing the entire
  grid container down by 165px. With the flip, the header
  collapses from 727px to 36px and the article content sits
  above the fold for the first time.
- **Language dropdown hidden at desktop** (ADR-029 follow-up): `#p-lang-btn`
  (the "In other languages" 140-item list) is a `:checked` /
  `position: absolute` dropdown in real Wikipedia, visible only on
  hover/click. Without those features, its 140 `<li>` children render
  as visible blocks consuming 4766px in the article column, pushing
  the article body below the fold. `RenderPipeline::relayout()`
  injects `#p-lang-btn{display:none}` at viewports >= 1120, matching
  the visible behavior of real Wikipedia with JS. Principled fix is
  ADR-030 (`position: absolute` + `:checked`).
- **State pseudo-classes** (ADR-018): `:link`, `:visited`, and `:hover`
  now parse, match, and render. `<a href>` links render blue when
  unvisited, purple when visited, and lighter blue when hovered --
  matching the behavior of every mainstream browser since the mid-1990s.
- **Click navigation** (ADR-018): clicking a rendered `<a href>` now
  navigates to it. Before this, links were styled but inert and the
  only way to load a page was typing in the URL bar.
- **`honeybee::resolve_url`** in `base/url.{hpp,cpp}` -- one canonical
  implementation used by both the CSS matcher (to decide whether a
  link's target is visited) and the browser layer (to fetch on click),
  guaranteeing the string marked visited is byte-identical to the
  string fetched.
- **`renderer::layout::hit_test`** -- maps mouse coordinates to the
  deepest DOM element under the point, walking the layout tree and
  inspecting `LineFragment::element` for flattened inline text.
- **`LineFragment::element`** -- carries source-element identity
  through inline layout so `<a>` inside `<p>` remains identifiable
  for hit-testing and future per-element features (text selection,
  tooltips).
- **`RenderState`** threaded through the cascade -- `visited_urls`,
  `hovered_element`, `base_url` all travel with `cascade()` so the
  selector matcher can consult them.
- `tests/fixtures/` not yet introduced; manual test HTML files
  currently live in `docs/` (see `docs/README.md`).

- **Back and forward navigation** (ADR-019): toolbar `<-` and `->`
  buttons are now functional. History is session-scoped: every
  successful navigation pushes a URL, Back/Forward move a cursor
  through the list, and a new navigation truncates any forward
  history (the branching case). Reload does not create history
  entries -- pressing it N times still requires one Back to leave.
- **`honeybee::browser::NavigationHistory`** in
  `src/browser/navigation_history.hpp` -- header-only history
  stack with `push` / `back` / `forward` / `can_back` /
  `can_forward`. No GTK dependency; unit-tested.

- **Page caching for Back/Forward** (ADR-020): the browser now
  caches HTML per URL in a session-scoped in-memory store.
  Back/Forward serve from cache without a network round-trip;
  the status bar shows "من الذاكرة" (from memory) to distinguish
  cache hits from fetches. New navigations and Reload always
  fetch fresh -- Reload additionally refreshes the cache entry.
- **`honeybee::browser::PageCache`** in
  `src/browser/page_cache.hpp` -- header-only URL -> HTML map
  with O(1) `bytes()` via an incrementally-maintained counter.
  No GTK dependency; unit-tested.

- **Large-page scrolling** (ADR-021): the view area is wrapped
  in `Gtk::ScrolledWindow`; content taller than the viewport is
  scrollable with the mouse wheel, trackpad, scrollbar, and
  Page Up/Down. The `DrawingArea`'s size request grows to the
  full layout height (44,000 CSS pixels for a Wikipedia article),
  letting ScrolledWindow manage the scroll naturally.
- **Incremental restyle for hover** (ADR-021): hover changes
  re-run only the cascade and update paint colors in place,
  skipping layout entirely. Hover latency on Wikipedia's 800KB
  article drops from ~2-3s (previous full-relayout path) to
  ~700ms. Still not sub-frame on very large pages -- a followup
  ADR (targeted cascade, ADR-022) is proposed to reach that.

- **Targeted cascade for hover** (ADR-022): hover changes
  re-cascade only the elements whose state changed, keeping the
  rest of the ComputedStyleMap cached. Measured on Wikipedia's
  799KB article (debug build): link hovers drop from ~700ms
  (ADR-021) to **14-25ms** (~30-50x improvement); medium element
  hovers to 55-67ms. Hover on large structural elements remains
  ~900ms (~6% of observed transitions) -- same cost as before,
  because their subtrees get re-cascaded entirely. A followup
  ADR (ADR-023, skip-subtree optimization) is proposed to close
  that gap.

- **Skip-subtree optimization for hover** (ADR-023): wrapper-element hovers
  now cascade only hover-sensitive descendants, not every element
  in the subtree. Measured on Wikipedia (debug build): structural-
  element hovers drop from ~900ms (ADR-022) to **~400ms** (~2.2x
  faster). Level detection works correctly (all 63 observed
  transitions were Level 1 -- UA `a:hover` only). Link hovers
  unchanged at ~20ms. A followup ADR (ADR-024, hover-match
  transition check) is proposed to close the remaining gap --
  the ADR-023 spec predicted ~20ms for wrapper hovers; actual is
  ~400ms because real pages have ~2,500 `<a>` per wrapper div,
  not the ~10 in the synthetic test. Same scale-estimate failure
  mode as ADR-021 -> ADR-022's prediction miss.

- **Hover-match transition check** (ADR-024): `restyle()` now cascades
  only elements whose `:hover` match state actually changed
  between old and new hovered element. Measured on Wikipedia
  (debug build): wrapper-element hovers drop from ~400ms (ADR-023)
  to **~8ms** (~50x). Link hovers unchanged at ~18ms. Cumulative
  across ADR-022 -> 023 -> 024: wrapper hover 900ms -> 8ms (~120x).
  The measured result beat ADR-024's ~25ms prediction by ~3x,
  because the pre-existing `did_work` early-return skips propagate
  and restyle_walk when zero elements cascade. Documented as
  Meta-lesson-v2: predictions need a complete code-path model,
  not just component measurements.

- **External CSS loading** (ADR-025): external stylesheets
  (`<link rel="stylesheet">`) are now fetched, parsed, and
  applied alongside inline `<style>` CSS. Verified visually on
  Wikipedia's HTML article: article-body typography, heading
  hierarchy, inline link styling, citation markers, and
  references formatting are all transformed; sidebar link colors
  partially improved. Sidebar/header/TOC **layout** remains
  UA-default (blocked by `@media` and `:root`; ADR-026/027 close
  those gaps). Page cache now stores `(html, external_css)` per
  URL, so Back/Forward re-render without re-fetching.

  Notable finding: 27% parser acceptance translated to ~40-50%
  visual improvement (higher than expected). The parseable rules
  concentrated in content-typography; the unparseable 73% is
  dominated by layout rules. Acceptance rates alone don't predict
  user-visible impact -- visual verification is what revealed the
  concentration.

  Deferred (three-ADR continuation): ADR-026 (`:root` + `var()`),
  ADR-027 (`@media`), ADR-028 (`:not()` + attribute operators).

- **CSS variable resolution** (ADR-026): `:root` selector
  support + parse-time `var(--x[, fallback])` resolution. On
  Wikipedia's CSS: 542 `var()` declarations; 341 are whole-value
  (299 of those target properties our layout engine consumes);
  194 are partial-value (left unchanged, deferred). The 299
  consumed-property resolutions are real and cascade-integrated,
  but **visible delta over ADR-025 is negligible** -- the rules
  containing these declarations either lose the cascade on
  specificity, are gated by `@media` (ADR-027), or produce
  values visually similar to what ADR-025 already applies.

  Meta-lesson-v3 documented: "consumed-property coverage is
  necessary, not sufficient, for visible change. The rule
  containing the resolved declaration must also win the cascade
  after parsing." Same shape as ADR-023/024's earlier Meta-
  lessons.

  Also fixes a pre-existing `display: none` gap in
  `flatten_inline_to_words`: `<style>` tags nested inside inline
  elements were rendering their CSS text as visible article
  content (Wikipedia has 18 such tags in `<body>`, several
  inline-nested). Cascade correctly computed `display: none`;
  the inline-flattening recursion simply wasn't checking it.
  Surfaced by ADR-026's visual verification. One-line fix +
  regression test.

- **@media block support** (ADR-027): @media blocks are
  now parsed (with simple `calc(Npx ± Mpx)` support) and
  cascade-filtered by viewport. At viewport 1024, 33 of
  Wikipedia's 64 @media blocks fire (52%) -- including the
  `max-width: calc(1120px - 1px)` tablet-layout blocks that
  calc support specifically unlocks. Unsupported queries
  (`print`, `prefers-*`, `aspect-ratio`, `orientation`,
  `resolution`, `hover`, `pointer`, range syntax) drop their
  rules entirely, per CSS spec.

  **Visible delta over ADR-026: partial.** Wikipedia's bottom
  navbox now renders with structured section headers and
  colored backgrounds -- @media `background-color` declarations
  winning cascade and consumed by paint.

  **Not delivered: sidebar / header / TOC layout transform.**
  The 106 layout-property declarations inside @media
  (`grid-template-columns`, `flex-*`, `column-*`, `float`,
  `position`) enter the cascade correctly but our layout
  engine ignores those properties (ADR-011 scoped layout to
  block + inline flow). The structural transform requires a
  future layout-engine ADR.

  **Meta-lesson-v3 refinement**: predicting visible delta from
  any CSS subsystem requires tracing three paths -- does it
  parse, does it win the cascade, does its winning property get
  consumed by paint/layout? ADR-027 measured all three, so the
  prediction was closer than ADR-025/026's.

- **CSS grid layout** (ADR-028): `display: grid`
  now parses and lays out. Fixed, `fr`, and `minmax()` track
  sizing; named grid areas via `grid-template-areas` and
  per-child `grid-area`; `column-gap`. On Wikipedia at viewports
  >= 1120px: sidebar becomes a ~325px left column, article
  content indents to the right. Article's vertical position
  within its cell is off by ~660px (deferred).

  **Hidden deliverable**: fixing a root-relative URL resolution
  bug (`base/url.cpp`) that had been silently 404ing every
  external CSS fetch since ADR-025. Post-fix, ADR-025's
  external CSS loading, ADR-026's var() resolution, and
  ADR-027's @media rules all reach the cascade on Wikipedia
  for the first time. Their measured deltas were inline-only;
  post-fix deltas are larger.

### Fixed

- **Use-after-free segfault on click-navigation** (ADR-018): after
  clicking a link while hovering over it, the browser crashed when
  the new page finished loading. Root cause: `RenderState::hovered_element`
  pointed into the previous DOM; `RenderPipeline::render()` replaced
  `doc_` without clearing the pointer, and the next cascade's
  `PseudoHover` matcher dereferenced freed memory. Fixed at the source
  in `render()`, where `doc_` is the only place replaced.

### Changed

- UA stylesheet rule `a { color: #0000EE }` replaced by three
  per-pseudo-class rules (`a:link`, `a:visited`, `a:hover`). Side
  effect: `<a>` without an `href` no longer renders blue (correct --
  it is not a link).
- `resolve_url` relocated from `selectors.cpp`'s anonymous namespace
  to `base/url.cpp` so both the CSS layer and the browser layer share
  one implementation.

- Back/Forward buttons start disabled (history empty) and
  transition to enabled/disabled as the history cursor moves.
  Previously both were enabled but non-functional (stubs that
  printed "غير مُفعَّل بعد" to the status bar).
- Reload button now calls `navigate_to(url, NavKind::Reload)`
  rather than re-dispatching through `on_url_activated()`, so
  history is preserved.

- Back/Forward are effectively instantaneous in normal use.
  On localhost the improvement is invisible (a fetch was
  already milliseconds), but on any real network the difference
  between a round-trip and a re-parse is significant.
- Session integrity: killing the HTTP server mid-session
  leaves Back/Forward functional for any page already visited.
  Reload and New correctly fail (they must fetch).

- `render_pipeline.cpp` and `pango_text.cpp` moved from the
  `honeybee` executable target to `honeybee_lib`. Both are
  GTK-free and testable; the move unblocks unit testing of
  `RenderPipeline` (needed by the new restyle dimension-invariant
  test). `PANGOCAIRO` pkg-config added to top-level dependencies.
- Hover on Wikipedia-scale pages is responsive within ~700ms
  (previously 2-3s freeze). See "Large-page scrolling + hover"
  under Added; targeted cascade (ADR-022) will address the
  remaining latency.

- Hover latency on large pages: link hovers are now ~20ms
  (median) instead of ~700ms. Structural-element hovers are
  unchanged at ~900ms and documented as a known limitation
  (ADR-023). User-visible: hovering links on Wikipedia feels
  instant; hovering a large wrapper element still lags.

- Hover on Wikipedia-scale pages: link hovers unchanged (~20ms).
  Structural-element hovers improved from ~900ms to ~400ms (2.2x),
  but short of the ~20ms ADR-023 predicted. Root cause: real pages
  have far more `<a>` descendants per wrapper div than the synthetic
  test document assumed. ADR-024 proposes the transition-check fix.

- Hover performance final state: link hovers ~18ms, wrapper-element
  hovers ~8ms on Wikipedia-scale pages. Full hover responsiveness
  is now sub-perceptual on all element types, closing the thread
  ADR-021 opened. The three-ADR arc (022 -> 023 -> 024) went from
  ~900ms to ~8ms on wrapper hovers (~120x total).

## [0.4.0]

The baseline release at the time this changelog was created. Prior
history lives in the git log and in `docs/decisions/` (ADRs 001-017
implemented before this file existed).

