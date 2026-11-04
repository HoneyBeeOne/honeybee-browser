# ADR-069: Viewport-clipped paint architecture

**Status:** Implemented (production-verified)
**Date:** 2026-11-04
**Related:** ADR-021 (DrawingArea+ScrolledWindow), ADR-064 (scroll height fix), ADR-068 (Cairo CPU workaround)

## Context

ADR-021 / ADR-064 established HoneyBee's rendering as:

- Gtk::DrawingArea whose size_request = full layout height (~30,000px for Wikipedia)
- Gtk::ScrolledWindow wrapping the DrawingArea, providing scrollbar + scroll

This worked until ADR-064 made scroll actually work -- at which point GTK4's GSK GPU renderer crashed on the huge DrawingArea surface. ADR-068 shipped a workaround: `setenv("GSK_RENDERER", "cairo", 0)` forcing the Cairo CPU renderer (the 213 MB core dump showed zero HoneyBee frames from frame #11 onward, pure GTK4/GSK internal).

The workaround fixed the crash but did not fix the root cause: HoneyBee still handed GSK a ~30,000px surface, and any GTK update could reintroduce the bug. ADR-069 addresses the root cause.

## Decision

**Never render into a surface larger than the viewport.**

The DrawingArea's height is always = viewport height. ScrolledWindow still provides wheel / keyboard / scrollbar / kinetic scrolling -- but its child is now a Box with three elements:

```
VBox m_content_box
  top_spacer     (height = scroll_y)
  DrawingArea    (height = viewport_h)
  bottom_spacer  (height = content_h - scroll_y - viewport_h)
```

On each scroll adjustment change:
1. `m_scroll_y = vadjustment.value`
2. Resize spacers (`set_size_request` on each)
3. `queue_draw` on DrawingArea

In `on_draw`:
1. `cr->translate(0, -m_scroll_y)` -- layout y=scroll_y maps to screen y=0
2. Pass `visible_top = scroll_y`, `visible_bottom = scroll_y + viewport_h` to `pipeline.paint`
3. paint culls off-screen content (see below)

Mouse coordinates: DrawingArea is viewport-relative. Convert to layout-space with `y += m_scroll_y` in `on_mouse_motion` and `on_link_clicked`.

## The culling lesson (the actual learning)

**First attempt:** cull in `paint_children` -- skip any child whose layout box is entirely outside `[visible_top, visible_bottom]`:

```cpp
if (child->y + child->height < visible_top - MARGIN) continue;
if (child->y > visible_bottom + MARGIN) continue;
```

**Bug:** A Box's `y / height` does NOT bound its descendants. Children can overflow their parent box (absolute positioning, floated figures, overflow:visible). When we skip an off-screen Box, we skip everything inside it -- including descendants that ARE visible.

Wikipedia exposes this: the infobox is a tall Box whose direct children (rows) extend past its height. Scrolling to History showed the Tim Berners-Lee photo (inside the infobox subtree) disappearing -- the whole Box got culled at its y-boundary.

**Fix:** cull only leaf nodes. Always recurse into Box nodes.

| Kind | Where culled | Why |
|---|---|---|
| Box | Inside `paint_box`, on **paint ops** (background, border, shadow, mask, images) | Leaf descendants may still be visible |
| Text | Inside `paint_children`, on the child itself | No descendants |
| InlineContext | Inside `paint_children`, on the child itself | No descendants |

In `paint_box`:

```cpp
const bool box_on_screen =
    (box.y + box.height >= visible_top - MARGIN) &&
    (box.y <= visible_bottom + MARGIN);

if (box_on_screen) {
    // shadow, border, background, mask, gradient, background-image, <img>
}
// ALWAYS recurse into children
paint_children(cr, box.children, ..., visible_top, visible_bottom);
```

`CULL_MARGIN = 128.0f` covers translateY transform (ADR-058 magnifier), box-shadow spread, rotate bounding box (ADR-054).

Default arguments `visible_top=-1e30f, visible_bottom=1e30f` make culling a no-op for any caller that doesn't pass a range -- preserves all pre-existing callers and tests.

## Verified

Production-verified 2026-11-04:

| Check | Before ADR-069 | After ADR-069 |
|---|---|---|
| DrawingArea height on Wikipedia | ~30,378px | 921px (viewport) |
| GSK GPU renderer | Crash (ADR-068 backtrace) | **No crash** -- `GSK_RENDERER=gl` works end-to-end |
| Scroll perf | Smooth (Cairo CPU) | Smooth (Cairo CPU and GSK) |
| Content visible at all scroll positions | Yes | Yes -- culling does not skip visible descendants |
| Tim Berners-Lee photo in History | Renders | Renders |
| PNG / SVG / JPEG | All render | All render |
| Hover on link | Works | Works |
| Click on link | Works | Works |
| Tests | 691 / 2641 | 691 / 2641 (unchanged) |

**The GSK no-crash result is the architectural payoff:** ADR-068 was a workaround (force CPU renderer). ADR-069 eliminates the root cause (never hand GSK a large surface). GSK GPU renderer works when the surface stays viewport-sized.

## What this does NOT do

- **Does not remove ADR-068's Cairo setenv.** `setenv("GSK_RENDERER", "cairo", 0)` still runs. Belt-and-braces: GSK might have other unrelated bugs, and the fallback is cheap. Removing it is a future ADR candidate.
- **Does not change hover / click hit-test math except for the `+scroll_y` offset.** Everything else in the interaction model is unchanged.
- **Does not remove Cairo clipping.** `cairo_clip` remains as defense-in-depth. With correct culling, clip is redundant -- but cheap.

## Deferred

- **ADR-070 candidate: remove Cairo fallback.** Now that GSK works, drop the setenv. Needs broader testing (multiple pages) before removing.
- **`Gtk-WARNING: Trying to snapshot gtkmm__GtkBox ... without a current allocation`** -- pre-existing before ADR-069. Cosmetic. Root cause unclear. Not introduced by this ADR.
- **Full `Gtk::Scrollable` implementation** -- the current spacer-tetris pattern works but is slightly indirect. A proper Scrollable on DrawingArea would be cleaner. Deferred -- no consumer pain today.
- **Sparse content culling** -- the current cull is per-box. A smarter version might precompute visible ranges from an index. Not needed at Wikipedia's tree size (already fast).

## The diagnostic chain that found the culling bug

Iteration 1: full ADR-069 patch applied -- build clean, tests pass, but Wikipedia goes blank mid-scroll.

Iteration 2: added `DRAW-DIAG` fprintf. Tetris math was **correct** (`top + draw + bot = h_px` at every tick), adjustment pointer stable, scroll signal firing correctly.

Iteration 3: `HONEYBEE_NO_CULL=1` test -- content appears. Culling confirmed as the culprit. Root cause: Box y-bound does not bound descendants. Fix: leaf-only cull + always-recurse Box.

**Lesson (recorded):** when culling a tree walk, distinguish leaf nodes (safe to cull) from container nodes (must recurse). "Box" does not mean "opaque rectangle" -- it means "a group that might overflow."

## References

- ADR-021 -- DrawingArea + ScrolledWindow architecture
- ADR-064 -- scroll height fix that exposed the GSK crash
- ADR-068 -- Cairo CPU workaround (retained as defense-in-depth)
- ADR-058 -- translateY transform (cull margin source)
- ADR-054 -- rotate transform (cull margin source)
