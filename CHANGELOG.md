# Changelog

All notable changes to the HoneyBee Browser are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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

## [0.4.0]

The baseline release at the time this changelog was created. Prior
history lives in the git log and in `docs/decisions/` (ADRs 001-017
implemented before this file existed).

