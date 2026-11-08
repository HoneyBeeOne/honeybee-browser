# ADR-071: Inline Routing, Culling Regression, and Async Logging

**Status:** Implemented (production-verified)
**Date:** 2026-11-08
**Related:** ADR-016 (inline context), ADR-046 (logging), ADR-069 (viewport culling), ADR-070 (font pipeline)

## Context

After reverting ADR-070 (font weight pipeline) due to a Wikipedia regression, we discovered that Wikipedia's section headings (`<h2>`) were completely invisible in the main article body. Investigation revealed three overlapping bugs that had been masking each other:

1. **Inline Routing**: MediaWiki's Vector 2022 CSS sets `<h2>` to `display: inline` inside a `.mw-heading` wrapper. Our `layout_block_children` function blindly treated *all* children of a block as blocks. This gave the `<h2>` a local `y=0` coordinate, causing its text fragments to be culled as "off-screen."
2. **Viewport Collapse**: On initial load, `on_draw` fired before GTK allocated the window, passing a `viewport_h` of `0` to the layout engine. This caused percentage heights to collapse, breaking the layout tree.
3. **Culling Regression**: ADR-069 established that only leaf nodes (Text / InlineContext) should be culled, because a Box's height does not bound its descendants. However, a regression in `paint_children` restored culling for *all* nodes. Because `<body>` was clamped to the viewport height (`641px`) while its child (`mw-content-text`) was `30440px`, scrolling past `641px` caused the culler to skip `<body>` entirely—resulting in a blank page.

## Decision

### 1. Fix Inline Routing
`layout_block_children` now checks `display_of(child_style)`. If the child is `Display::Inline` or `Display::InlineFlex`, it is pushed to `inline_run` instead of being laid out as a block. Before processing a true block child, `flush_inline_run()` is called. Crucially, `flush_inline_run()` is now also called at the *end* of the loop to catch any trailing inline elements (like a final `<h2>` in a section).

### 2. Guard Initial Layout
`on_draw` and `sync_drawing_area_size` now return early if `viewport_h <= 0` or `width <= 0`. This prevents the layout tree from collapsing before the window is actually sized.

### 3. Restore ADR-069 Leaf-Only Culling
`paint_children` only applies culling to `LayoutKind::Text` and `LayoutKind::InlineContext`. `LayoutKind::Box` nodes are *never* culled at the parent level. They are always passed to `paint_box()`, which will either paint their background or recurse into their children. This ensures that deeply nested content (like the Wikipedia article body) is always reached.

### 4. Comprehensive Async Logging
Debugging these overlapping issues required exhaustive tracing without freezing the UI. We upgraded ADR-046's logging infrastructure:
- Added `LayoutDetail` and `PaintDetail` categories.
- Implemented an **asynchronous logger**: `log_writev` now pushes formatted strings to a thread-safe queue. A background thread pops them and writes to the file.
- Hooked `log_shutdown()` into `main.cpp` to flush the queue cleanly on exit.

## Verified Delta

| Check | Before | After |
|---|---|---|
| **Wikipedia h2 headings** | Invisible (blank bands) | **Visible** (`History`, `Markup`, `HTML5`) |
| **Wikipedia scrolling** | Blank page after 641px | **Smooth, no blank pages** |
| **Initial viewport** | Layout collapsed to 0-height | Renders correctly on first frame |
| **Logging performance** | Blocked main thread (async) | Zero-cost when disabled, non-blocking when enabled |

## The Diagnostic Chain (Lessons Recorded)

- **`HB_LOG` is essential:** The `layout_element: EXIT` trace revealed `<body> h=641` vs `<div#mw-content-text> h=30440`. Without this, the culling bug was invisible.
- **Culling containers is dangerous:** A Box's `height` is not the same as its visual bounds. Never cull a container; only cull leaves.
- **Async logging prevents Heisenbugs:** Synchronous logging during paint altered the timing enough to cause the compositor to blank the window, hiding the actual bug.

## References

- ADR-016 — inline formatting context
- ADR-046 — runtime-gated logging (upgraded to async here)
- ADR-069 — viewport-clipped paint (leaf-only culling rule)
- ADR-070 — font-weight pipeline (deferred, preserved on `feat/adr-070-preserve`)
