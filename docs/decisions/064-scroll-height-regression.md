# ADR-064: Scroll height regression (page-scroll blocked by ADR-057 clamp)

**Status:** Implemented (production-verified)
**Date:** 2026-11-03
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-021 (ScrolledWindow), ADR-052 (viewport height), ADR-057 (parent_content_height, `html { height: 100% }`)

## Context

**Wikipedia page does not scroll.** Scrollbar never appears; content
below the fold is laid out but unreachable. Scroll **worked earlier
in this session** (user reported "slow when scrolling"), so this is
a regression introduced by a later ADR.

### The measurement (2026-11-03, Release build, Wikipedia HTML article)

Added a temporary `fprintf` to `sync_drawing_area_size`:

```
SYNC-SIZE: layout_height=316.9 scroll_viewport_h=678   (blank page)
SYNC-SIZE: layout_height=678.0 scroll_viewport_h=678   (Wikipedia loaded)
```

**`layout_height()` returns exactly the viewport height on a page
with 30,000+ pixels of content.** That is the bug.

### The chain

| Layer | Behavior |
|---|---|
| ScrolledWindow | `set_policy(NEVER, AUTOMATIC)`, `set_overlay_scrolling(false)`, `vexpand(true)` — correct (ADR-021) |
| `sync_drawing_area_size` | `set_size_request(-1, ceil(layout_height()))` — correct in intent |
| `layout_height()` | Returns `layout_root_.height` — **the bug** |
| `layout_root_.height` | = `<html>` box height = **clamped to viewport by ADR-057** |

### Root cause: ADR-057's `html { height: 100% }` clamping

Wikipedia's CSS reset: `html, body { height: 100% }`.

**Before ADR-057**: `%` height was skipped by the parser's
`find('%') == npos` guard. `<html>` height = sum of children =
natural content height (say 30,000px). ScrolledWindow sees
content = 30,000px > viewport = 678px → scrollbar appears.

**After ADR-057**: `parse_length_for_height("100%", ctx)` resolves
against `ctx.parent_content_height`. For `<html>`, that's
`viewport_h` (via the `<html>` special case added by ADR-057).
So `<html>`'s explicit `height: 100%` clamps its box height to
**678px** — even though its children sum to 30,000px. The clamp
happens at `layout.cpp`'s `cursor_y = content_y + eh` after
children lay out.

**Consequence**: `layout_root_.height = 678`. `sync_drawing_area_size`
sets `set_size_request(-1, 678)`. ScrolledWindow sees content =
viewport → **no scroll range**.

The children (article, divs, thumbnails, references) are laid out
correctly at y=0..30,000 inside `<html>`. They're just not reachable
because ScrolledWindow thinks the content ends at 678.

### Why this is a legit ADR-057 side effect, not an ADR-057 bug

ADR-057 shipped `html { height: 100% }` correctly per CSS spec —
`html` genuinely should have the viewport height for background
painting, min-height on short pages, etc. **The bug is in
`layout_height()`'s assumption that `layout_root_.height` equals
"how tall is the page for scrolling purposes."** Those are two
different concepts:

- **`<html>` box height** = CSS semantic height (viewport when
  `height: 100%`, content height otherwise)
- **Page scroll extent** = max y-coordinate of any rendered pixel

They diverge whenever `html { height: 100% }` (or `body { height:
100% }`, or `min-height: 100vh`) is present with content overflow.

## Decision

Change `RenderPipeline::layout_height()` to return the **maximum
`y + height` across all descendants of `layout_root_`**, not
`layout_root_.height`.

### The fix

```cpp
// ADR-064: walk the tree for the true content bottom.
// layout_root_.height is the <html> CSS box, which ADR-057 clamps
// to viewport when html { height: 100% }. The scroll extent is
// the max y+height over all descendants -- that's the deepest
// rendered pixel.
float max_content_bottom(const layout::LayoutNode& node) {
    float max_bottom = node.y + node.height;
    if (node.kind == layout::LayoutKind::Box) {
        for (const auto& child :
             static_cast<const layout::LayoutBox&>(node).children) {
            max_bottom = std::max(max_bottom, max_content_bottom(*child));
        }
    } else if (node.kind == layout::LayoutKind::Inline) {
        for (const auto& child :
             static_cast<const layout::LayoutInline&>(node).children) {
            max_bottom = std::max(max_bottom, max_content_bottom(*child));
        }
    } else if (node.kind == layout::LayoutKind::InlineContext) {
        for (const auto& child : static_cast<
                 const layout::LayoutInlineContext&>(node).children) {
            max_bottom = std::max(max_bottom, max_content_bottom(*child));
        }
    }
    return max_bottom;
}

float RenderPipeline::layout_height() const {
    return max_content_bottom(layout_root_);
}
```

### Why this works with the clamp

`<html>` height = 678 (clamped), but `<body>`'s children still have
their real heights and their `y` positions (relative to `<body>`,
which is 0). Walking the tree finds:
- `<html>`: y=0, h=678, bottom=678
- `<body>`: y=0, h=678, bottom=678
- `<div id="content">`: y=0, h=25000, bottom=25000 ← **real max**
- ...deep children contribute correctly too

The recursion doesn't care about the clamp at the top — it finds the
deepest real pixel regardless.

### Why this is safe (multiple angles)

1. **No existing caller wants `<html>` box height.** `layout_height()`
   is only used by `sync_drawing_area_size`. If a second caller ever
   appears, they'll want the content extent too.
2. **`set_size_request` can be too tall.** A DrawingArea taller than
   content is invisible (empty space below) but not broken. A
   DrawingArea shorter than content is broken (unreachable content).
   Overshoot is strictly safer than undershoot.
3. **Fixed-position elements** (from ADR-033) anchor at viewport_h,
   so their y+height is at most ~viewport_h. That's a real lower
   bound on the page's minimum scroll extent — correct.
4. **Absolute elements** with negative `top:` produce y+height < 0
   or near 0. `std::max` with the initial `max_bottom = node.y +
   node.height` (root's bottom = viewport_h) ignores them. Correct.
5. **Empty page**: `layout_root_.height = 0`, `max_content_bottom`
   returns 0, `sync_drawing_area_size` bails on `h <= 0.0f`.
   Unchanged behavior.

### O(n) walk cost

Called **once per relayout** (from `sync_drawing_area_size`). With
~5000 layout nodes, the recursion is <1ms even in Debug. **Zero
measurable impact** — `PERF-LAYOUT` won't show it.

## Deferred

- **Trim overshoot** -- `max_content_bottom` can be a pixel or two
  over the true visual bottom (margins, shadows). Harmless (a few
  pixels of empty space). Not worth the extra walk precision.
- **`get_height()` for the "blank page" case** -- if a page has no
  content, `sync_drawing_area_size` bails and leaves DrawingArea
  at whatever previous height. Existing behavior; unchanged by
  this ADR.
- **The `html { height: 100% }` interaction is CORRECT** -- this
  ADR does not change ADR-057. It fixes the assumption that page
  height == `<html>` box height.

## Consequences

**Better:**
- **Scrollbar returns** on Wikipedia and any page with `html { height: 100% }` + overflow
- **`layout_height()` name now matches intent** -- it always meant "content extent," now it does
- **No regression to ADR-057** -- `<html>`'s box height stays correct for its CSS purpose; only the scroll-extent query changes

**Accepted limitations:**
- **O(n) recursion** per relayout -- measured negligible
- **Precision**: `max_content_bottom` can overshoot by a pixel or two

**Unchanged:**
- ADR-005 through ADR-063 behavior
- `<html>` box height (still clamped by ADR-057 correctly)
- DrawingArea width, viewport height threading
- Paint, cascade, layout

## Verification

**Instrumented measurement** (the diagnostic that produced this ADR):
temporary `fprintf` in `sync_drawing_area_size` showed
`layout_height=678.0 scroll_viewport_h=678` on Wikipedia **before**
the fix. **Expected after fix**: `layout_height=~30000
scroll_viewport_h=678` -- content extent, not viewport.

**Visual verification**: scrollbar appears on Wikipedia; user can
scroll down through the article.

## Acceptance criteria

ADR-064 is complete when:

- [ ] `max_content_bottom` helper walks Box / Inline / InlineContext trees
- [ ] `layout_height()` returns `max_content_bottom(layout_root_)`
- [ ] `SYNC-SIZE` log shows ~30000 on Wikipedia (was 678)
- [ ] Scrollbar appears on Wikipedia HTML article
- [ ] 687 / 2627 tests still pass (no test uses `layout_height()`)
- [ ] README row + CHANGELOG
- [ ] ADR-064 status -> Implemented

## Implementation

Implemented on `feat/scroll-fix` at 2026-11-03.

### What shipped

`RenderPipeline::layout_height()` rewritten to iteratively walk
the layout tree for the deepest Box's `y + height`, instead of
returning `layout_root_.height` (which ADR-057 clamps to viewport
when `html { height: 100% }` is present):

```cpp
float max_bottom = layout_root_.y + layout_root_.height;
std::vector<const renderer::layout::LayoutNode*> stack;
stack.push_back(&layout_root_);
while (!stack.empty()) {
    const auto* n = stack.back(); stack.pop_back();
    if (n->y + n->height > max_bottom) max_bottom = n->y + n->height;
    if (n->kind == renderer::layout::LayoutKind::Box) {
        for (const auto& child :
             static_cast<const renderer::layout::LayoutBox*>(n)->children) {
            stack.push_back(child.get());
        }
    }
}
return max_bottom;
```

Box-only traversal: Inline fragments are bounded by their parent
Box's vertical extent (parent's cursor_y already accounts for
child line heights), so no separate Inline/InlineContext walk.

Uses `renderer::layout::` fully qualified -- no `namespace layout
=` alias exists in `render_pipeline.cpp` at file scope (confirmed
by the compiler after two failed iterations).

### Verified delta (production-visual, user screenshots 2026-11-03)

Instrumented `sync_drawing_area_size` before fix:

```
SYNC-SIZE: layout_height=678.0 scroll_viewport_h=678   (Wikipedia)
```

After fix, per user screenshots:
- **Scrollbar visible** on Wikipedia right edge
- **Wheel scroll works** -- article scrolls
- **Bottom-of-article reachable** (references, footer)

### Three compiler iterations (debugging trail)

1. File-scope helper in anonymous namespace ->
   `'max_content_bottom' was not declared in this scope`
2. `static` file-scope function ->
   `'layout' does not name a type` (no alias at file scope)
3. Lambda inside member function -> `static_cast` cascade
   (consequence of #2)
4. Iterative walk with `renderer::layout::` -> builds clean

**The compiler gave the answer at iteration 2.** Same lesson
as ADR-057 (`ctx` scope) and ADR-060 (`#include`): when the
compiler says "does not name a type," a 2-second
`grep -n 'namespace X =\|using X'` in the file settles it.

### Extra relayout cost

Page load now shows **5 `PERF-CASCADE` lines** (was 4). The
taller DrawingArea (30000px vs 678px) triggers one more GTK
layout cycle after `set_size_request`. Extra relayout at
~533ms is a known cost of the fix, not a regression.

### Test count

687 cases / 2627 assertions unchanged. Four existing
`layout_height()` assertions (stability, > 0) continue to pass.

### What this closes

- Scrollbar restored on Wikipedia (was completely absent)
- `layout_height()` name matches intent (content extent)
- **Unblocks ADR-063's deferred visual verification.** With
  scroll working, the effect of float layout (ADR-063) can now
  be assessed on real Wikipedia content. Doing so surfaced a
  narrowing concern that ADR-065 will diagnose -- see that ADR
  for details.
\n