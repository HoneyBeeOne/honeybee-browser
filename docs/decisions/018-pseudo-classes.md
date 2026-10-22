# ADR-018: State Pseudo-Classes

**Status:** Implemented
**Date:** 2026-10-21
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-008 (selector matching), ADR-010 (cascade), ADR-014 (UA stylesheet)

## Context

ADR-008 implemented selector parsing and matching for type, class, id, attribute selectors, and all four combinators. It explicitly deferred pseudo-classes to a future ADR:

> Pseudo-classes (`:hover`, `:link`, `:visited`): not in selector parser; deferred to a future ADR.

As of v0.4.0, that deferral is the most visible remaining gap in the selector layer. `<a>` renders blue via the UA stylesheet rule `a { color: #0000EE }`, but every link looks identical regardless of whether the user has visited it or is hovering over it. That is a user-visible behavior that every other browser has had since the mid-1990s.

This ADR adds three **state pseudo-classes**:

- `:link` — an unvisited `<a>` with an `href`
- `:visited` — a visited `<a>` with an `href`
- `:hover` — an element currently under the mouse cursor

Other pseudo-class categories (structural like `:first-child`, logical like `:not()`, and the rest of Level 4) remain deferred to future ADRs.

## Decision

We implement the three state pseudo-classes with a **state-tracking model** separate from `dom::Element`.

### Architecture

```
   dom::Document (immutable after parse)
        │
        │ render_pipeline adds state layer
        ▼
   RenderState (mutable)
     ├── visited_urls : unordered_set<string>
     ├── hovered_element : const dom::Element*
     └── pointer_position : (x, y) in viewport pixels
        │
        ▼
   cascade(doc, styles, state)     <- computed once per state change
        │
        ▼
   ComputedStyleMap
```

**`dom::Element` does not grow state fields.** The DOM stays a pure tree. State lives in a separate `RenderState` struct owned by `RenderPipeline`. The cascade is extended to accept a `const RenderState&`.

This is the same separation ADR-007 established between DOM (owned tree) and render (computed data). The DOM is data; state is presentation.

### `RenderState`

```cpp
struct RenderState {
    // URLs the user has navigated to. Populated by render_pipeline
    // when a navigation succeeds.
    std::unordered_set<std::string> visited_urls;

    // The element currently under the mouse cursor, or nullptr
    // when the cursor is outside the viewport.
    const dom::Element* hovered_element = nullptr;

    // Viewport pixel position of the cursor. Needed for hit testing
    // in future phases (e.g., :hover on ancestors during layout).
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
};
```

### Selector matching extension

`css::Selector::matches` gains an overload:

```cpp
bool matches(const dom::Node& node, const RenderState& state) const;
```

The existing `matches(const dom::Node&)` becomes a call to the new overload with a default-constructed `RenderState` (empty visited set, no hovered element). This preserves backward compatibility with 200+ existing tests.

**Per-pseudo-class semantics:**

| Pseudo-class | Match condition |
|---|---|
| `:link` | node is an `<a>` element, has a non-empty `href` attribute, and the resolved absolute URL is NOT in `state.visited_urls` |
| `:visited` | node is an `<a>` element, has a non-empty `href` attribute, and the resolved absolute URL IS in `state.visited_urls` |
| `:hover` | node pointer == `state.hovered_element` |

URL resolution: the `href` attribute is resolved against the page's base URL (the URL of the currently-loaded page). v1 uses a simple resolution: if `href` starts with `http://` or `https://`, use as-is; otherwise prepend the base URL + `/`. Full RFC 3986 relative URL resolution is deferred (the base URL from `<base href>` is also deferred).

### Parser extension

`SimpleKind` gains:

```cpp
enum class SimpleKind : std::uint8_t {
    Type, Universal, Class, Id, Attribute,
    PseudoLink, PseudoVisited, PseudoHover,
};
```

Parsing: `:link`, `:visited`, `:hover` after a compound selector. Any other `:name` is a parse error (returns `std::nullopt` for the whole selector). This matches the current strict-parser policy: unknown syntax fails cleanly rather than silently dropping.

**Specificity**: each pseudo-class counts as a class (weight `b` in the `(a,b,c)` triple). Per CSS 2.1 §6.4.3: "pseudo-classes ... count as a class." That means `a:hover` has specificity `(0,1,1)` — one class, one type — and `a:visited` beats `a:link` when both match (same specificity, but the visited rule appears later in the UA stylesheet).

### UA stylesheet changes

The current rule:

```css
a { color: #0000EE }
```

becomes:

```css
a:link    { color: #0000EE }
a:visited { color: #551A8B }
```

Purple visited links (`#551A8B`) match the classic web default that Firefox and Chrome use. `:hover` on links is deferred to a follow-up rule addition (v0.4.x) because it needs the render state to be threaded through the pipeline first; the initial implementation ships `:link` and `:visited` behavior, with `:hover` support available in the matcher but no UA rule consuming it yet.

Actually -- reconsidering. The ADR should ship all three in the same release. Adding `:hover` support to the matcher but no UA rule would be a half-measure. Let me revise: `a:hover { text-decoration: underline }` as the UA rule. But `text-decoration` is not implemented in paint. So `:hover` on the UA stylesheet would have no visible effect. Hmm.

Option: add `a:hover { color: #0055EE }` (a lighter blue) as a UA rule. That's visible without text-decoration support, and it proves the hover mechanism works. If the user wants a stronger hover effect, that's a follow-up.

**Decision**: ship `a:hover { color: #0055EE }` as the UA rule. Small visible effect that proves the mechanism. Later ADRs that add `text-decoration` will revise the rule.

### Render pipeline changes

`RenderPipeline` gains:

- A member `RenderState state_` (mutable)
- A method `set_hovered_element(const dom::Element*)` that, if the argument differs from the current hovered element, updates `state_.hovered_element` and triggers a cascade + layout recompute (relayout + repaint)
- A method `record_visit(const std::string& absolute_url)` that adds a URL to `state_.visited_urls` and triggers a cascade + layout recompute (or a simpler "recompute cascade only" if the change is purely presentational)
- The `render(html, width)` entry point initializes `state_` (clears visited set for a new page load; retains visited URLs across pages in the same session)

For v1: **full relayout on state change**. This is simpler and correct. Optimization (repaint-only, style-only, etc.) is deferred to a future ADR.

### GTK wiring

`window.cpp` currently does not track mouse position. It gains:

- A `Gtk::EventControllerMotion` on the drawing area, whose `signal_motion` handler:
  - Reads the event's `(x, y)` in widget coordinates
  - Converts to layout coordinates (accounting for any future scroll offset)
  - Hit-tests against the `LayoutBox` tree to find the deepest `Element` under the cursor
  - Calls `pipeline.set_hovered_element(matched_element)`

Hit testing: walk the layout tree, find the deepest `LayoutBox` whose `(x, y, width, height)` contains `(px, py)` and whose `element` is non-null. This is O(n) in layout node count; for our scale, fine.

### Navigation tracking

`window.cpp`'s `on_url_activated` success handler, after a successful fetch, calls:

```cpp
pipeline.record_visit(final_url);
```

This adds to `state_.visited_urls`. When the newly-rendered page loads, links to `final_url` will match `:visited`.

### Documented limitation: browser history privacy

Real browsers partition `:visited` styles: the visited state of a URL is visible to a page only if the user visited it *before* that page loaded. Otherwise a malicious page could detect which other sites you visited by rendering links and measuring their color. v1 does not implement this partition — `:visited` styles apply based on the session's visited set regardless of when the visit occurred.

This is a known privacy limitation, documented in `current-state.md`. Fixing it requires per-page visited snapshots (take a copy of `visited_urls` at page load, use the copy for style computation on that page). That is a follow-up ADR (probably bundled with form autofill, which has similar privacy concerns).

## Rationale

### 1. State lives outside the DOM

Adding `bool visited` to `dom::Element` would pollute the DOM with presentation state. It would also break the ADR-007 rule: the DOM is an owned, immutable-after-parse tree. Putting mutable state on the DOM would require invalidating the "immutable" invariant and touching every DOM test.

Separating state into `RenderState` (owned by `RenderPipeline`) keeps the DOM clean and makes state testable in isolation.

### 2. Recompute cascade on state change is correct and simple

The alternative is incremental style recomputation: track which selectors depend on `:hover` and only update the affected elements. That is what Chromium does[reference:2], and it is significantly more complex. For our scale (a few hundred nodes per page), recomputing the cascade on hover change is fast enough (under 1ms for the sizes we care about).

The pattern: **correct first, optimize when profiling shows it matters.** Chromium's optimization exists because it handles pages with thousands of nodes and complex state interactions; we don't.

### 3. `:visited` needs no history API

A user visiting a URL is the only way to mark it visited. We track the visited set in-memory for the session, and v1 does not persist to disk. That is sufficient to make `:visited` behave visibly correctly during a browsing session — the classic `#551A8B` purple appears when you revisit a link you clicked earlier. Persistence (across sessions) is deferred.

### 4. Three pseudo-classes, not fifty

`:link`, `:visited`, and `:hover` are the most user-visible state pseudo-classes. Structural pseudo-classes (`:first-child`, `:nth-child`) are a separate category (they depend on tree structure, not state, and match statically — no cascade recomputation needed). Logical pseudo-classes (`:not()`, `:is()`) are a third category. Shipping one category at a time keeps each ADR focused and testable.

## Alternatives Considered

### Add `:visited` state directly to `dom::Element`

**Rejected.** Breaks ADR-007's "DOM is data" invariant. Also makes DOM testing more complex: every test that constructs an element would need to be aware of the `visited` flag.

### Use Chromium's incremental recomputation pattern in v1

**Rejected for v1.** Much more complex than full recomputation. Requires a dependency graph from selector to matched elements, invalidation logic, and careful handling of the cascade order. Defer to a performance ADR when profiling shows the full recompute is too slow.

### Implement `:hover` and `:active` together

**Rejected for v1.** `:active` requires mouse button down/up events and a state machine (pressed → released inside → released outside). `:hover` only needs motion events. Adding `:active` doubles the state complexity for marginal visible gain in v1. Deferred.

### Implement `:focus` in v1

**Rejected for v1.** `:focus` requires a focusable-element model (which elements can receive focus?), a focus stack, and interaction with `<input>` and `<button>` which we don't render yet. Deferred until form elements land.

### Use `text-decoration: underline` for `:hover` effect

**Deferred.** Paint doesn't support `text-decoration` yet. The v1 hover effect uses a color change instead (`#0000EE` → `#0055EE`). A later ADR adding `text-decoration` will revise the hover rule.

### Persist visited URLs to disk

**Deferred.** v1 tracks visited URLs in-memory per session. Cross-session persistence needs a storage layer and a privacy policy (can users clear their history?). Deferred to a future ADR about browser storage.

### Implement privacy partitioning for `:visited`

**Deferred.** The current implementation lets a page detect which URLs were visited (by rendering a link and checking if it renders purple). Chromium blocks this by snapshotting visited state at page load. v1 documents this limitation. A follow-up ADR will add per-page visited snapshots when we tackle privacy as a first-class concern.

## Consequences

### Files changed

- `src/renderer/css/selectors.hpp` — `SimpleKind` gains `PseudoLink`, `PseudoVisited`, `PseudoHover`
- `src/renderer/css/selectors.cpp` — parser recognizes `:link`/`:visited`/`:hover`; matcher matches against state; specificity counts pseudo-classes as class weight
- `src/renderer/css/cascade.hpp` — `RenderState` struct; `cascade()` overload that accepts it
- `src/renderer/css/cascade.cpp` — threading `RenderState` through matching
- `src/renderer/css/ua_stylesheet.cpp` — `a:link { color: #0000EE }`, `a:visited { color: #551A8B }`, `a:hover { color: #0055EE }`
- `src/browser/render_pipeline.hpp` / `.cpp` — `RenderState` member, `set_hovered_element`, `record_visit`
- `src/browser/window.cpp` — mouse motion handler, hit-testing
- `src/renderer/layout/layout.hpp` / `.cpp` — hit-testing helper (optional, could live in window.cpp)
- `tests/test_selectors.cpp` — parse tests for the three pseudo-classes, malformed pseudo-class tests, specificity tests
- `tests/test_cascade.cpp` — match tests for each pseudo-class in each state
- `tests/test_ua_stylesheet.cpp` — rule count update

No changes to `renderer::dom`, `renderer::layout`, `renderer::html`, `renderer::paint`.

### Behavior examples

| HTML + CSS | State | Expected |
|---|---|---|
| `<a href="https://x.com">X</a>` | not visited, not hovered | blue (`#0000EE`) |
| `<a href="https://x.com">X</a>` | visited | purple (`#551A8B`) |
| `<a href="https://x.com">X</a>` | hovered | lighter blue (`#0055EE`) |
| `<a>no href</a>` | any | inherits body color (no `:link` match) |
| `<div class="x">X</div>` with `.x:hover { color: red }` | hovered | red |
| `<p>text</p>` with `p:hover { color: red }` | hovered | red |
| `<a href="relative.html">X</a>` with base `http://example.com/` | visited relative.html | purple |
| `.selector:hover` (malformed) | n/a | parse error |

### What this does not change

- Type, class, id, attribute, universal selectors — same syntax, same matching
- Combinators — same
- Specificity calculation for non-pseudo selectors — same
- `display`, `dir`, inline formatting — same
- Layout algorithm — unchanged (except new hit-testing helper, which is standalone)

## Open Questions

### `:hover` on inline elements

If a `<p>` contains `<a>`, hovering over the `<a>` fires `:hover` on both the `<a>` and the `<p>`? Per CSS 2.1, yes: `:hover` matches an element when the pointer is over that element *or any descendant*. Our hit test finds the deepest element under the cursor (the `<a>`), then walks the ancestor chain matching `:hover` on each. This gives the correct behavior. Implementation: `set_hovered_element` receives the deepest element and stores the ancestor chain (or stores just the deepest and the matcher checks "is the candidate an ancestor of hovered_element or hovered_element itself").

**Design decision for v1**: store only the deepest element; the matcher walks the ancestor chain on demand. That avoids re-allocating a vector on every mouse move.

### `:hover` while scrolled

Our `hit_test` uses layout coordinates, which are always absolute (from the viewport origin). If we add scrolling in a future ADR, the hit-test needs to convert widget coordinates to layout coordinates by subtracting the scroll offset. That is a one-line change when scrolling lands.

### Pointer leaves the window

`Gtk::EventControllerMotion` fires a `signal_leave` when the pointer leaves the widget. Our handler should call `set_hovered_element(nullptr)`. This is required for the hover state to clear correctly when the mouse leaves the window.

### `:visited` and page reload

If the user reloads a page, the visited set is unchanged (URL still visited). The `:visited` styles continue to apply. If the user navigates back in history to a previously-visited URL, it is still visited. That matches browser behavior.

### Fingerprinting via `:visited`

A page can detect visited state by measuring link colors via `getComputedStyle`. v1 does not implement the browser partition. Documented in `current-state.md` under the new "Pseudo-classes" section.

### Cascade performance

Full cascade recompute on every hover change is O(rules × elements). For a 100-element page with 50 rules, that's 5000 match checks per mouse move — likely 1-2ms. If this proves too slow on real pages, the optimization ADR would add: (a) skip cascade if no rules match `:hover`; (b) cache the non-hover cascade, only recompute the affected elements. The first is trivial; the second is complex.

## Record of Changes

- **2026-10-21:** Initial decision.

## Implementation

Implemented on `feat/pseudo-classes` across six commits:

| Commit | Scope |
|---|---|
| `59d2042` | RenderState plumbing: `visited_urls`, `hovered_element`, `pointer_x/y`, `base_url` threaded through `cascade()` as an unused parameter |
| `c1c4dbc` | Parser recognizes `:link`, `:visited`, `:hover`; matcher uses `RenderState`; specificity counts each as class weight |
| `92143bf` | UA stylesheet replaces `a { color: #0000EE }` with `a:link` / `a:visited` / `a:hover` rules |
| `1ff7f9e` | `RenderPipeline::record_visit` populates `visited_urls`; relative href resolution against base URL for `:visited` matching |
| `834be39` | `layout::hit_test` walks the layout tree; `LineFragment::element` carries source-element identity; `Gtk::EventControllerMotion` drives `hovered_element` from mouse motion |
| `a50e202` | Click-navigation via `Gtk::GestureClick`; `honeybee::resolve_url` public API; clear `hovered_element` on DOM replacement to prevent a use-after-free |

**Verified behavior** (screenshots confirmed):

- `:link` renders `#0000EE` blue for unvisited anchors
- `:visited` renders `#551A8B` purple for visited anchors
- `:hover` renders `#0055EE` lighter blue for the element under the mouse cursor
- Clicking a rendered `<a href>` navigates; the destination becomes `:visited` on return
- `:hover` fires from live mouse motion and clears via `signal_leave`
- Full test suite: 237 cases, 905 assertions, all pass

**Deferred to future ADRs:**

- Other pseudo-class categories: structural (`:first-child`), logical (`:not()`, `:is()`), remaining state (`:focus`, `:active`, `:target`)
- `:visited` link privacy (real browsers restrict getComputedStyle on `:visited` to prevent history leaks; out of scope for a local browser)
- Back/forward navigation buttons (still stubs; not part of ADR-018 scope)

