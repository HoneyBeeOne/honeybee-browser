# ADR-021: Real-Page Interaction (Scrolling + Incremental Restyle)

**Status:** Implemented
**Date:** 2026-10-22
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-018 (hit-test + hover), ADR-019 (navigation), ADR-020 (cache)

## Context

Loading `https://en.wikipedia.org/wiki/HTML` (799,591 bytes) in the
browser after the User-Agent fix surfaced two architectural
assumptions that every prior ADR implicitly relied on but none
stated:

1. **Pages fit in a viewport.** The `Gtk::DrawingArea` renders
   the full layout tree into a widget of viewport height. Cairo
   clips anything beyond the widget. Wikipedia's article is
   roughly 40,000-60,000px tall on a 1024-wide viewport; only
   the top ~900px (the sidebar navigation) is visible. The rest
   is laid out, painted, clipped, and inaccessible. No scrollbar,
   no wheel handler, no offset — the concept of "scroll" does not
   exist in the browser today.

2. **Hover is cheap.** `on_mouse_motion` calls
   `RenderPipeline::relayout()` whenever the hovered element
   changes. `relayout()` does the full pipeline: extract CSS,
   cascade, layout, line-break, Pango-measure every word, rebuild
   the entire box tree. On a 363-byte test page this is <1ms.
   On Wikipedia's 800KB page it is several hundred milliseconds
   to seconds. Effectively the UI freezes on every hover
   transition, so hover colors never visibly change and clicks
   during the freeze are swallowed. The URL bar also stays
   focused because focus transfer happens during the freeze.

Both are real; both are user-visible; both block "browsing real
pages" as a concept. They are addressed together here because
they touch the same handlers (`on_mouse_motion`, `on_draw`,
`hit_test`) and because fixing only one leaves the browser in a
half-working state that's harder to reason about than fixing
both.

## Decision

Two coordinated changes, each with its own design rationale.

### Change 1: Scrolling via Gtk::ScrolledWindow

Wrap `m_drawing_area` in a `Gtk::ScrolledWindow`. After every
`relayout()`, set the DrawingArea's size request to
`(viewport_width, layout_root.height)`. The ScrolledWindow then:

- Renders a vertical scrollbar when content exceeds the viewport
- Handles mouse wheel, trackpad, and keyboard scrolling
- Automatically translates pointer coordinates into the
  DrawingArea's coordinate space before dispatching motion and
  click events

That last point is critical: **hit-test, hover, and click
coordinates remain in layout space** — the invariant established
by ADR-018, preserved by ADR-019 and ADR-020, continues to hold.
No coordinate translation code is needed in `on_mouse_motion`,
`on_link_clicked`, or `m_pipeline.hit_test`.

`on_draw`'s `width`/`height` parameters become the DrawingArea's
full size (e.g. 1024 x 45000), not the visible region. Cairo
clips the actual raster to the visible region, so paint produces
the whole tree's cairo commands but only the visible portion is
rasterized. For 800KB pages, this is ~5000 cairo commands per
repaint (one per line fragment) — 10-50ms, acceptable for scroll
smoothness.

### Change 2: Incremental restyle for hover

Add `RenderPipeline::restyle(const RenderState& state)`:

1. Re-run the **cascade only** (skip layout, skip Pango
   measurement, skip line breaking)
2. Walk the existing layout tree, updating colors in place
   from the new cascade output:
   - `LayoutBox::background` and `LayoutBox::border_color` from
     the box's `element`'s computed `background-color` and
     `border-color`
   - `LayoutInline::color` from the inline's `element`'s
     computed `color`
   - `LayoutText::color` from the text's `element`'s computed
     `color` (new `LayoutText::element` field, backfilled from
     ADR-018's precedent)
   - `LineFragment::color` from `fragment.element`'s computed
     `color`
3. Trigger `queue_draw` on the drawing area

`on_mouse_motion` calls `restyle()` instead of `relayout()`.
Hover becomes sub-frame-latency regardless of page size.

**Invariant this establishes:** restyle must not change any
node's `x`, `y`, `width`, or `height`. `:hover` only affects
paint colors in v1 (the only UA rules that fire on hover are
`a:hover { color: #0055EE }`). If a future pseudo-class or
author rule made `:hover` change layout (e.g. `a:hover { font-size: larger }`),
restyle would silently produce stale layout. That's a known
v1 limitation, documented in Deferred below. The invariant is
testable: add a `debug_assert` or a test that captures layout
dimensions before/after restyle and compares.

### Backfilled change: LayoutText::element

`LayoutText` today has `color` but no `element*` pointer. The
color is set at creation from the containing block's computed
style. To update it during restyle, we need to know which
element produced it.

Add `const dom::Element* element = nullptr` to `LayoutText`,
populated at creation in `layout.cpp`'s text-only path
(`lt->element = &el`). Mirrors ADR-018's `LineFragment::element`
addition — same design shape, same purpose.

## Design decisions

### Why ScrolledWindow, not manual offset

The alternative is a manual scroll offset: `float m_scroll_y`,
a wheel event handler that mutates it, `on_draw` translates the
cairo context by `-m_scroll_y` before painting, and
`on_mouse_motion` / `on_link_clicked` add `m_scroll_y` to
incoming y before hit-testing. This works, and it produces
correct-looking behavior.

It's strictly worse than ScrolledWindow for v1 because:

- **Coordinate translation is bug-prone.** Every handler that
  receives (x, y) must remember to translate. Hit-test must
  translate. Paint must translate. Missing one site produces a
  silent visual bug (hover works but click is off by scroll_y,
  or hover works on page A but not page B). ScrolledWindow
  handles this in GDK, once, correctly.
- **Manual scrollbar reimplementation.** ScrolledWindow gives
  scrollbar rendering, thumb sizing, drag-to-scroll, and
  keyboard scroll for free. Reimplementing these is a whole
  subsystem.
- **Smooth scroll semantics.** ScrolledWindow handles trackpad
  gesture acceleration, momentum scroll, and pixel-perfect
  wheel events. Manual handlers get the delta but not the
  smoothing.

The cost of ScrolledWindow is that the DrawingArea's size
request grows to the full layout height (60,000px for Wikipedia).
If GTK4 empirically rejects sizes that large or renders
glitchy, the implementation falls back to manual offset — this
fallback is documented here so the choice is visible.

### Why restyle, not "fast relayout"

An alternative would be to keep `relayout()` but optimize it —
cache the cascade output, skip unchanged branches, avoid Pango
re-measurement for unchanged text. That's a bigger architectural
change: it turns layout from a pure function of (DOM, styles,
viewport width) into a mutable incremental system with
dirty-tracking.

`restyle()` is dramatically simpler because hover **doesn't
touch layout** — `a:hover` only changes color. So instead of
optimizing the whole pipeline, we skip the expensive half
(measure + line-break + box tree construction) and only re-run
the cheap half (cascade + color assignment).

The v1 limitation is explicit: if hover ever changes sizes,
restyle produces stale layout. This is documented as Deferred
and will require a "hover can trigger relayout for specific
properties" mechanism in a future ADR. In practice, the CSS
`:hover` property list in real browsers is essentially limited
to `color`, `background-color`, `border-color`, `opacity`,
`text-decoration`, `box-shadow`, and `transform` — only the
first three are relevant to this browser's v1 style surface.

### Why cascading twice

`render()` does `cascade(ua, author, doc, state)` then
`layout(doc, styles, measurer, viewport_width)`. `restyle(state)`
does just the cascade, then walks the layout tree.

The cascade is fast (pure string comparison and property
application; ~10ms on Wikipedia's 800KB across thousands of
elements). Layout is slow (Pango text measurement, line breaking
across the whole document, allocation of thousands of layout
nodes). So cascade-twice / layout-once-per-navigation is
asymptotically better than layout-twice.

### :visited and cache interaction

`:visited` still works during restyle — the cascade re-runs
against `state.visited_urls`, so if a page becomes visited, its
links render purple on the next restyle. This is a happy
consequence: Back to a page after visiting elsewhere will show
the correct visited colors without a full relayout.

The cache (ADR-020) is unaffected: restyle doesn't re-parse HTML
or re-extract CSS; it just re-cascades against the current
state. Cached page bodies remain valid.

### The `(void)height;` in on_draw

The current `on_draw` explicitly discards `height`. After this
ADR, `height` becomes meaningful: it's the full DrawingArea
height (layout height), and paint iterates the whole tree,
letting Cairo clip to the visible region. The `(void)height;`
line is removed.

## Deferred

- **Scroll position restoration** on Back/Forward. v1 scrolls to
  top on every navigation. Restoring scroll is a separate ADR.
- **Hover changes sizes.** If an author styles `a:hover { font-size: larger }`
  or any other size-affecting property, restyle produces stale
  layout. v1 requires hover to be color-only. A future ADR
  addresses this (either by extending restyle to detect
  size-affecting changes and trigger a partial relayout, or by
  forbidding hover from affecting sizes in the parser).
- **Horizontal scrolling.** v1 renders single-column vertical
  content at viewport width; there's no horizontal overflow.
- **Scroll anchoring during resize.** If the user scrolls and
  then resizes the window, content re-flows and scroll offset
  may become nonsensical. v1 scrolls to top on resize.
- **Smooth scroll / momentum.** Handled by ScrolledWindow's
  native implementation; no custom code needed.
- **Scroll-driven CSS** (`:scroll-timeline`, `scroll-driven
  animations`). Not in scope for any v1 selector work.
- **Partial repaint.** `queue_draw()` repaints the whole visible
  region. A future optimization would invalidate only the
  regions that changed (the hovered link). For 800KB pages,
  full-visible-region repaint is ~10-50ms — acceptable.
- **`:hover` on non-anchor elements.** The UA stylesheet only
  defines `a:hover`. Author CSS can define hover on any element,
  and restyle handles it, but the visual payoff is limited to
  what authors style.

## Consequences

**Better:**

- Wikipedia (and every real-world page) becomes scrollable end
  to end. The content that was laid out but inaccessible is now
  reachable with the wheel, scrollbar, or Page Down.
- Hover works on any page size. The freezing UI disappears;
  hover transitions become smooth regardless of document size.
- Click-to-navigate works on any page size. The events that
  were previously swallowed by relayout during hover now fire
  instantly.
- Coordinates from ADR-018's hit-test remain in layout space
  with no translation — the invariant is preserved by choosing
  ScrolledWindow over manual offset.
- Back to a visited page now shows correct `:visited` colors
  without relayout (a happy consequence of restyle re-cascading).

**Accepted limitations:**

- Hover cannot change layout sizes. v1 documents this; future
  ADR addresses it.
- Scroll position resets on navigation and resize. A separate
  ADR adds restoration.
- Repaint is full-visible-region. On 800KB pages, 10-50ms per
  repaint; acceptable but visible on rapid scroll.

**Unchanged:**

- `:link`, `:visited`, `:hover` semantics — no matcher changes.
  The same rules produce the same colors; they're just applied
  faster during hover.
- Cache behavior — restyle doesn't touch it. Cache hits still
  serve cached HTML to `render()`; restyle runs after.
- Navigation history — restyle doesn't touch it.
- The `hovered_element` clear-on-render fix from ADR-018 — still
  runs on every navigation.

## Implementation plan

Three commits on `feat/real-page-interaction`:

1. **Scrolling infrastructure** — wrap `m_drawing_area` in
   `Gtk::ScrolledWindow` in the constructor; add
   `RenderPipeline::layout_height()` accessor; after every
   `relayout()`, set DrawingArea's size request to
   `(viewport_width, layout_height())`. Remove `(void)height;`
   from `on_draw`. Verify by scrolling Wikipedia.

2. **Incremental restyle** — add `LayoutText::element` backfill
   to `layout.cpp`; add `RenderPipeline::restyle(state)` that
   re-cascades and walks the layout tree updating colors; wire
   `on_mouse_motion` to call `restyle()` instead of `relayout()`.
   Verify by hovering links on Wikipedia (instant color change).

3. **Docs update** — ADR-021 status → Implemented, README row,
   CHANGELOG `[Unreleased]` entries.

## Verification

Unit tests (`tests/test_layout.cpp` addition, `tests/test_pipeline.cpp` new):

- `LayoutText::element` is populated for text-only content
- `layout_height()` returns the root node's height
- Restyle with the same state does not change any node's
  `(x, y, width, height)` — asserts the "restyle never changes
  layout dimensions" invariant
- Restyle with a `hovered_element` set updates fragment colors
  (fragments belonging to the hovered element change color; all
  others unchanged)

Visual verification (Wikipedia, 800KB page):

- Scroll down: article content visible (previously inaccessible)
- Hover link: color changes to lighter blue instantly (no freeze)
- Click link: navigates (previously frozen)
- Back: returns to article, scrolls to top (documented limitation)
- :visited update: after visiting another page, Back shows the
  clicked link as purple
- Small page (link-test-a) still works correctly — no regression

## Acceptance criteria

ADR-021 is complete when:

- [ ] Wikipedia article is scrollable from top to bottom
- [ ] Hover on Wikipedia links shows lighter blue, no freeze
- [ ] Click on Wikipedia links navigates
- [ ] Back returns to article (scroll resets to top — expected)
- [ ] Small pages (link-test-a) still scroll-less (fit viewport, no scrollbar)
- [ ] Restyle tests confirm dimensions unchanged
- [ ] `main` is green: 253 + N cases, all pass, zero warnings
- [ ] Screenshots confirm all the above in the running browser

## Implementation

Implemented on `feat/real-page-interaction` across three commits:

| Commit | Scope |
|---|---|
| `473f00e` | ScrolledWindow wrap + `RenderPipeline::layout_height()` accessor + `HoneyBeeWindow::sync_drawing_area_size()`. `set_overlay_scrolling(false)` was the actual fix for the missing scrollbar (see diagnostic corrections below). |
| `728bc89` | `RenderPipeline::restyle()` + `restyle_walk()`; window wiring (hover uses `restyle()` instead of `relayout()`); CMake restructuring (pipeline + pango moved to `honeybee_lib`); dimension-preservation test |
| (this commit) | Docs: ADR-021 status -> Implemented, README features row, CHANGELOG entries |

**Verified behavior** (visual, on Wikipedia's HTML article, 799KB, ~44,000 CSS px tall):

1. Non-overlay scrollbar visible on the right side of the window
2. Article body reachable by scrolling (previously clipped at viewport height, inaccessible)
3. References section reachable at the bottom
4. Hover on article-body links changes color (`#0000EE` -> `#0055EE`), reverts on un-hover
5. Hover on sidebar links also works
6. No multi-second UI freeze (previous behavior) -- see "Measured performance"

**Measured performance (honest):**

Restyle on the Wikipedia article takes ~700ms per hover transition in debug build. Breakdown: extract CSS ~2ms, parse stylesheet ~4ms, cascade ~690ms (10,000 elements x 17 UA rules + Wikipedia's inline style rules), restyle_walk ~5ms. Previous implementation (full `relayout()` on every hover) took 2-3 seconds on the same page. So restyle is 3-5x faster and eliminates the multi-second freeze, but does NOT reach the "instant" (sub-frame) latency hypothesized in the Implementation plan above.

**Diagnostic corrections to the plan:**

1. **`LayoutText::element` backfill was not needed.** Reading `paint.cpp` revealed that only `LayoutBox::background`, `LayoutBox::border_color`, and `LineFragment::color` are consulted at paint time. `LayoutText::color` and `LayoutInline::color` are dead state -- inline nodes are flattened during layout and `LayoutText` is not drawn directly (only its `LineFragment`s are). So `restyle_walk` goes directly from `fragment.element` (already backfilled by ADR-018) to the computed style, with no intermediate `LayoutText::element` pointer needed.

2. **The overlay-scrollbar concern turned out to be the actual bug.** Default GTK4 overlay scrollbars use a `Gtk::Overlay` wrapper that misroutes pointer events to children owning gesture controllers -- the DrawingArea owns `EventControllerMotion` (ADR-018) and `GestureClick` (ADR-019), so scroll events never reached ScrolledWindow. `set_overlay_scrolling(false)` attaches the child directly, restoring normal event routing. An interim explicit `EventControllerScroll` added as a workaround received zero events once overlay mode was off and was removed.

3. **CMake restructuring was necessary for the restyle test.** `render_pipeline.cpp` and `pango_text.cpp` were in the `honeybee` executable target, so unit tests (which link only `honeybee_lib`) could not link against `RenderPipeline`. Moved both to `honeybee_lib` -- they are GTK-free and belong there per the library's own documented intent. Added `pkg_check_modules(PANGOCAIRO ...)` at top-level (previously inherited transitively via `GTKMM4` on the executable target).

## Followup: ADR-022 (targeted cascade)

The measured ~700ms bottleneck is entirely in the cascade phase. Re-cascading every element on every hover transition is wasteful: only two elements (the previously-hovered one and the newly-hovered one) actually changed state.

ADR-022 (proposed) will implement targeted cascade:

- New cascade API: `cascade_one(element, ua_sheet, author_sheet, state, cached_styles) -> ComputedStyle`
- `restyle()` keeps a `ComputedStyleMap` cache from the previous restyle; on hover change, re-cascade only the two affected elements
- Inheritance propagation: if the changed element has children, those children's inherited colors must also update -- probably via a "dirty subtree" propagation after the targeted re-cascade
- Expected result: <5ms per hover transition regardless of page size

Out of scope for ADR-021 because:
- New cascade API surface (touches ADR-010 territory)
- Cache invalidation is subtle (what invalidates a cached ComputedStyle? how does inheritance propagate through the cache?)
- Needs its own test suite to be confident the cache is never stale

Deserves a dedicated ADR rather than being rushed as a tail-end addition to ADR-021.

