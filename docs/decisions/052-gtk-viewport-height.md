# ADR-052: GTK Viewport Height Threading

**Status:** Implemented
**Date:** 2026-11-01
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-021 (ScrolledWindow), ADR-050 (@supports, sticky chain), ADR-051 (viewport units, closed Deferred item)

## Context

ADR-051 threaded `viewport_height` through `layout()`,
`layout_with_pseudo()`, and `layout_element()`. Its
`render_pipeline.cpp` caller explicitly passes `0.0f`, with the
following deferral note:

> GTK allocated height isn't cleanly in scope here; vh/vw
> resolve to 0 in production. Correctness for `calc(100vh -
> ...)` requires future GTK height threading (separate
> concern). ADR-051's Deferred section names it.

This ADR closes that deferral.

### Measurement (source tree, 2026-11-01)

| Finding | Value |
|---|---|
| `render_pipeline.cpp` passes `viewport_height` | **0.0f** (line 328) |
| `on_draw` tracks `m_last_width` | Yes |
| `on_draw` tracks `m_last_height` | **No** — height changes don't trigger relayout |
| `render_html` / `render_with_css` query widget width | `m_drawing_area.get_width()` |
| `render_html` / `render_with_css` query widget height | **None** |
| `relayout()` signature | `void relayout(float viewport_width)` — no height param |

### Widget structure (from ADR-021)

```
HoneyBeeWindow (Gtk::ApplicationWindow)
  └─ m_scroll_window (Gtk::ScrolledWindow)     ← the visible viewport
      └─ m_drawing_area (Gtk::DrawingArea)     ← content canvas
```

**The correct viewport height source is `m_scroll_window`'s
allocated height.** The DrawingArea's height grows to the full
layout height (via `sync_drawing_area_size` at `window.cpp:587`),
so its `get_height()` returns **content height** (potentially
60,000px on Wikipedia), not viewport height.

CSS `vh` means "the visible viewport height" — what the browser
window shows, not the scrollable content extent. `m_scroll_window`
is the correct source.

### GTK4 API note

The codebase is GTK4 (`m_drawing_area.get_width()`, no
`get_allocated_width()`). In GTK4, `Gtk::Widget::get_height()`
returns the **allocated** height (the GTK3 API
`get_allocated_height()` was removed in GTK4). So the correct
call is:

```cpp
static_cast<float>(m_scroll_window.get_height())
```

not `get_allocated_height()`.

### Edge case: initial render before allocation

`get_height()` returns 0 until the widget has been allocated by
GTK's layout engine. Same as `get_width()` today — the initial
`render_html` call queries before allocation, gets 0, then
`on_draw` re-lays-out with real dimensions.

**No special handling needed.** Existing pattern preserved.

## Decision

Thread `m_scroll_window.get_height()` through the
`render`/`render_with_css`/`relayout` API surface, add
`m_last_height` tracking to `on_draw`, and trigger relayout on
height change.

### Signature changes (all in `render_pipeline.hpp/.cpp`)

```cpp
// Existing (unchanged):
void render(const std::string& html, float viewport_width);
void render_with_css(std::unique_ptr<Document> doc,
                     std::string merged_css,
                     float viewport_width);
void relayout(float viewport_width);

// New (default 0.0f protects ~15 test callers, same pattern
// ADR-051 used):
void render(const std::string& html,
            float viewport_width,
            float viewport_height = 0.0f);
void render_with_css(std::unique_ptr<Document> doc,
                     std::string merged_css,
                     float viewport_width,
                     float viewport_height = 0.0f);
void relayout(float viewport_width,
              float viewport_height = 0.0f);
```

**`viewport_height` propagates to `layout_with_pseudo(...,
viewport_width, image_dims, viewport_height)`** — the call site
ADR-051 prepared.

### `on_draw` changes (`window.cpp:565`)

Current:
```cpp
if (width != m_last_width) {
    m_last_width = width;
    m_pipeline.relayout(static_cast<float>(width));
    sync_drawing_area_size();
}
```

New:
```cpp
const int height = m_scroll_window.get_height();
if (width != m_last_width || height != m_last_height) {
    m_last_width = width;
    m_last_height = height;
    m_pipeline.relayout(static_cast<float>(width),
                        static_cast<float>(height));
    sync_drawing_area_size();
}
```

**Both width AND height changes trigger relayout.** Correct for
window resize (either dimension affects `vh`/`vw` rules).

### `render_html` / `render_with_css` changes (`window.cpp:466, 486`)

```cpp
m_pipeline.render(html,
                  static_cast<float>(m_drawing_area.get_width()),
                  static_cast<float>(m_scroll_window.get_height()));
```

Same for `render_with_css`. The initial render queries both
sources.

### `m_last_height` field (`window.hpp`)

Add next to `m_last_width`:
```cpp
int m_last_height = 0;
```

## Design decisions

### Why `m_scroll_window`, not `m_drawing_area`

`m_drawing_area.get_height()` returns **content height** (set by
`sync_drawing_area_size` to match `layout_root_.height`). For a
Wikipedia page, that's ~60,000px. CSS `vh` means "visible
viewport", not content extent. Feeding 60,000 to `LengthContext.
viewport_h` would make `100vh = 60000` — catastrophically wrong.

`m_scroll_window.get_height()` returns the GTK-allocated visible
height (~800px typical), which is exactly what `vh` needs.

### Why add `m_last_height` (not just read fresh each draw)

`on_draw` already tracks `m_last_width` because reading
`get_width()` every draw then running `relayout()` unconditionally
would cause redundant relayouts on scroll-only frames. The same
logic applies to height — track `m_last_height` and skip relayout
when neither changed.

### Why default `viewport_height = 0.0f` on the pipeline methods

Same pattern as ADR-051's `layout()` / `layout_with_pseudo()`.
All ~15 existing test callers continue compiling unchanged; the
default is 0, which is the pre-ADR-051 semantics (vh/vw resolve
to 0). New code (production + new tests) opts in explicitly.

**Zero test churn** — this is a signature extension, not a
signature change.

### Why this is still zero visible delta (honestly)

**Even with correct height threaded**:
1. `vh` feeds only `max-height` / `max-width` (the 7 renderable
   rules from ADR-051)
2. Layout doesn't read `max-height` / `max-width` yet (deferred
   to a separate min/max clamp ADR)
3. `.vector-sticky-pinned-container` has `h=0` from ancestor
   culling rules (`.vector-toc-landmark { display: none }`, etc.)

**So the value that changes**:
- `parse_length("calc(100vh - 48px)")` now computes `752` (was
  `-48`)
- That value lands in `max-height` in `ComputedStyle` — correct
  semantically
- Layout ignores it (no min/max clamp support yet)
- PNG delta: **zero**

**The value that ships**: one fewer prerequisite on the sticky
sidebar chain. ADR-051's Deferred item "GTK allocated height
threading to `render_pipeline`" is closed. The next cycle
(min/max clamp) finds `max-height` already computed correctly.

## Deferred

- **`max-height` / `max-width` honoring in layout** — separate
  min/max clamp ADR; this ADR makes `viewport_h` correct so
  that ADR doesn't need to touch it
- **`svh` / `lvh` / `dvh`** — remain deferred (ADR-051)
- **Percentage `max-height`** — remains deferred (needs
  `parent_content_height`, separate concern)
- **Real-time height update on continuous window resize** — the
  `on_draw` path fires on resize; fine-grained continuous
  resize is GTK's default behavior, no additional work needed

## Consequences

**Better:**
- **`viewport_h` in production is correct** — matches CSS `vh`
  semantics for the first time
- **ADR-051's Deferred item closed** — "GTK height threading"
  no longer applies
- **`calc(100vh - 48px)` computes real values in production**
  (752 at typical window height instead of -48)
- **Width OR height resize triggers relayout** — correct for
  both axes
- **Zero test churn** — default `viewport_height=0.0f` protects
  all existing callers

**Accepted limitations:**
- **Zero visible delta** — `max-height` still ignored by layout,
  ancestor culling still blocks the sticky sidebar. The
  infrastructure is correct; visible delta requires the min/max
  clamp ADR
- **`m_last_height` is added in parallel to `m_last_width`** —
  small duplication, no abstraction (matches existing pattern)

**Unchanged:**
- All ADR-005 through ADR-051 behavior
- `m_drawing_area.get_width()` remains the width source
- `sync_drawing_area_size` unchanged
- GTK widget structure unchanged

**Possible regression risks (to verify in Commit 1):**
- `on_draw` signature unchanged; internal height tracking added
- If any test calls `render()`/`render_with_css()`/`relayout()`
  with the old 2-arg or 1-arg form, default `viewport_height=0.0f`
  covers it
- Real-time resize: if GTK fires `on_draw` faster than relayout
  can complete, `m_last_height` updates but `relayout` runs
  multiple times — same as existing `m_last_width` behavior,
  no new concern

## Implementation plan

Two commits (small enough to skip a separate "refactor" commit):

1. **Threading + `m_last_height`** (behavior-neutral for tests):
   - 3 pipeline signature extensions with `viewport_height = 0.0f`
   - `render_pipeline.cpp` forwards to `layout_with_pseudo(...,
     viewport_height)`
   - `window.cpp`: `render_html` / `render_with_css` pass
     `m_scroll_window.get_height()`
   - `window.cpp`: `on_draw` tracks `m_last_height`, relayouts
     on either dimension change
   - `window.hpp`: `int m_last_height = 0;`
   - All 644 existing tests unchanged (default param protects)

2. **Docs**: ADR-052 → Implemented, ADR-051 Post-ADR-052
   note closing the deferred item, README row + CHANGELOG

## Verification

**Unit tests:** none added. Threading is mechanical; behavior
verified via log + manual.

**Log verification (Commit 1):**

```bash
HONEYBEE_LOG=layout ./build/bin/honeybee
```

**Before**: `[honeybee][layout][info] layout: viewport=1774x0`
**After**:  `[honeybee][layout][info] layout: viewport=1774x800`
(or whatever the actual allocated window height is)

This is the definitive proof — `viewport_h` in production is now
nonzero, meaning `parse_length("100vh", ctx)` returns the actual
window height, and `calc(100vh - 48px)` computes real values.

**PNG verification (Commit 1):** before/after screenshots show
identical rendering. **Zero visible delta documented honestly.**

**Layout-tree verification (Commit 1):**
- Cascade of `.vector-sticky-pinned-container`'s `max-height`:
  `calc(100vh - 48px)` remains as a string (unchanged — cascade
  doesn't re-resolve `calc()`); the parse-layer now resolves it
  to `window_height - 48` when `parse_length` is called on it
- This is **also a verification without an observable downstream
  effect** (layout doesn't read max-height yet), but it means
  the value is **correct when the min/max clamp ADR lands**

## Acceptance criteria

ADR-052 is complete when:

- [ ] `render()` / `render_with_css()` / `relayout()` gain
      `viewport_height` param (default 0.0f)
- [ ] `render_pipeline.cpp` forwards `viewport_height` to
      `layout_with_pseudo(...)`
- [ ] `window.cpp`: `render_html` / `render_with_css` pass
      `m_scroll_window.get_height()`
- [ ] `window.cpp`: `on_draw` tracks `m_last_height`, relayouts
      on either dimension change
- [ ] `window.hpp`: `m_last_height` field
- [ ] All 644 existing tests pass unchanged
- [ ] `HONEYBEE_LOG=layout` shows `viewport=WxH` with H > 0
- [ ] First-viewport PNG delta: **zero**, documented honestly
- [ ] Post-ADR-052 note in ADR-051 (GTK height threading
      Deferred item closed)
- [ ] ADR-052 status → Implemented

**Explicit claim**: production `viewport_h` is now the actual
window allocated height, matching CSS `vh` semantics. ADR-051's
Deferred item is closed.

**Honest caveats:**
1. **Zero visible delta** — `max-height` still ignored by layout,
   sticky sidebar still blocked by ancestor culling. Same
   category as ADR-047/048.
2. **`m_last_height` tracking** duplicates `m_last_width`
   pattern — no abstraction, matches existing code.
3. **Continuous resize**: GTK fires `on_draw` rapidly; `relayout`
   runs once per unique (W, H) pair via `m_last_width` /
   `m_last_height` guards. Same as existing width-only behavior.
4. **Initial render before allocation** — `get_height()` returns
   0 until GTK allocates; `on_draw` re-runs with real dims.
   Existing pattern preserved.

## Implementation

Implemented on `feat/gtk-viewport-height` across two commits
(Commit 1 = threading, Commit 2 = this docs update).

### What shipped

- `render()` / `render_with_css()` / `relayout()` gain
  `viewport_height` param (default `0.0f`, protecting all ~15
  existing test callers)
- `render_pipeline.cpp` forwards `viewport_height` through to
  `layout_with_pseudo(..., viewport_height)` -- the call site
  ADR-051 prepared
- `window.cpp`: `on_draw` reads `m_scroll_window.get_height()`,
  tracks it in `m_last_height`, triggers relayout on **either**
  width or height change
- `window.cpp`: `render_html` / `render_with_preparsed_doc`
  pass `m_scroll_window.get_height()` alongside
  `m_drawing_area.get_width()`
- `window.hpp`: `int m_last_height = -1;`

### Verified delta

**Log verification (`HONEYBEE_LOG=layout` on Wikipedia HTML
article):**

```
[honeybee][layout][info] layout: viewport=0x0       <- first render (pre-allocation)
[honeybee][layout][info] layout: viewport=1024x678  <- second render (real viewport)
```

`viewport_h = 678` is `m_scroll_window.get_height()`, the
actual allocated visible viewport. **Before ADR-052**: `viewport
= 1024x0`.

### Honest delta framing

**Zero visible PNG delta.** Even with correct height threaded:

1. `vh` feeds only `max-height` / `max-width` (7 renderable
   rules from ADR-051)
2. Layout doesn't read either (deferred to min/max clamp ADR)
3. `.vector-sticky-pinned-container` still `h=0` from ancestor
   culling

**What changes**:
- `parse_length("calc(100vh - 48px)")` returns `window_height -
  48` in production (630 at this 678px window, was -48)
- Lands in `ComputedStyle.max-height` -- correct semantically
- Layout ignores it (no min/max clamp yet)

**Same category as ADR-047/048**: correct infrastructure
shipped before its consumer.

### Test count

- 644 cases, 2465 assertions unchanged (default
  `viewport_height=0.0f` protects all existing test callers)
- Zero new warnings introduced

### Deferred (documented)

- **`max-height` / `max-width` honoring in layout** -- separate
  min/max clamp ADR; finds `viewport_h` now correct
- **`svh` / `lvh` / `dvh`** -- remain deferred (ADR-051)
- **Percentage `max-height`** -- remains deferred (needs
  `parent_content_height`, separate concern)

### What this closes

**ADR-051's Deferred item** "GTK allocated height threading to
`render_pipeline`" -- verified via log showing nonzero
`viewport_h` in production for the first time.

**One fewer prerequisite** on the sticky sidebar chain. The
max-height clamp ADR can now read correct `calc(100vh - ...)`
values without touching viewport-height plumbing.

