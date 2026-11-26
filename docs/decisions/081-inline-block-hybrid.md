# ADR-081: Inline-block hybrid layout

**Status:** Implemented
**Date:** 2026-11-17
**Related:** ADR-016 (inline), ADR-031 (float), ADR-080 (list-item), ADR-073 (tracing)

## Context

`display: inline-block` is used 26 times in Wikipedia's CSS. Per CSS 2.1 §9.2.2.1, inline-block elements are:
- Externally: an atomic inline-level box (participates in inline formatting, no line break before/after)
- Internally: a block container (respects `width`/`height`, contains block-level children)

Current behavior: `display_of()` returns `Display::Block` for anything unrecognized, including `"inline-block"`. So all 26 uses render as block — a `.cdx-button` search icon becomes a full-width bar, `.mw-editsection` "[edit]" link becomes a block, etc.

The "flatten to inline" shortcut (like `display: inline`) would break block-containing inline-blocks badly — dropdown menus would jumble into unreadable inline text. That's why ADR-079's earlier flat mapping was wrong.

## Decision

Implement **inline-block as an atomic inline-level box** that internally holds a fully laid-out block subtree. It flows with adjacent inline text but is not flattened into text — its entire internal layout (including block children) is rendered as a single atomic unit positioned by the inline breaker.

### The atomic unit

`LineFragment` gains a `std::shared_ptr<LayoutBox> inline_block_child;` field (nullable). When set:
- `text` is empty
- `width` = child's box.width + horizontal margins
- `height` = child's box.height + vertical margins
- Fragment represents an atomic inline unit, not text

`StyledWord` mirrors this with `const dom::Element* inline_block_element;` (source) + `std::shared_ptr<LayoutBox> inline_block_box;` (populated by `flush_inline_run` before line breaking).

### Detection and flow

`display_of()`:
```cpp
if (*v == "inline-block") return Display::InlineBlock;
```

`layout_block_children`: inline-block children are pushed to `inline_run` (like inline), not laid out as blocks. This makes them participate in the surrounding inline formatting context.

`flatten_inline_to_words`: when the current element has `display: inline-block`, emit an atomic `StyledWord` with `inline_block_element = &el` and no text. Do NOT recurse — the element's internal layout is computed separately.

`flush_inline_run`: before line breaking, pre-lay-out every atomic word by calling `layout_element(*w.inline_block_element, ...)` with `x_before_margin = 0, y_before_margin = 0`. The returned box's content-box top-left is at `(margin_left, margin_top)`. Store the box in `w.inline_block_box`.

`break_styled_words_into_lines`: for atomic words:
- `ww` (word width) = `box->width + box->margin[1] + box->margin[3]`
- `wh` (word height) = `box->height + box->margin[0] + box->margin[2]`
- No internal wrapping — the whole box is one atomic unit
- Emit `LineFragment` with `inline_block_child = w.inline_block_box` (shared_ptr copy), `text = ""`

### Paint

In `paint_box`'s `LayoutKind::InlineContext` case, when iterating `ctx.lines`:

```cpp
for (const auto& frag : ctx.lines) {
    if (frag.inline_block_child) {
        const auto& child = *frag.inline_block_child;

        // Map the child's border-box origin to the fragment's
        // margin-box position plus the child's top/left margins.
        const float paint_offset_x =
            frag.x + child.margin[3] - child.x;
        const float paint_offset_y =
            frag.y + child.margin[0] - child.y;

        // The child's subtree uses local layout coordinates.
        // Convert the document viewport into that coordinate space
        // before descendant culling takes place.
        const bool culling_disabled =
            visible_bottom <= 0.0f ||
            visible_top >= visible_bottom;

        const float child_visible_top =
            culling_disabled
                ? visible_top
                : visible_top - paint_offset_y;
        const float child_visible_bottom =
            culling_disabled
                ? visible_bottom
                : visible_bottom - paint_offset_y;

        cairo_save(cr);
        cairo_translate(cr, paint_offset_x, paint_offset_y);
        paint_box(cr, child, drawer, images,
                  child_visible_top, child_visible_bottom);
        cairo_restore(cr);
    } else {
        paint_inline_image(cr, frag, images);
    }
}
for (const auto& frag : ctx.lines) {
    if (!frag.inline_block_child) {
        drawer(cr, frag);  // Pango text (skipped for inline-block)
    }
}
```

The fragment position represents the inline-block's margin-box origin. Its border box is painted at `(frag.x + margin_left, frag.y + margin_top)`. The same vertical paint offset is subtracted from the document viewport bounds before recursively painting, so descendant culling operates in the inline-block's local layout coordinate system.

### Baseline alignment (deferred)

CSS default: inline-block's bottom margin edge aligns with the parent's baseline. Phase 1 places the margin box at the fragment's line position, with the border box offset by its top and left margins. This is an approximation; proper baseline computation and `vertical-align` support are deferred to a later ADR.

## What this fixes

Wikipedia's inline-block uses (from `/tmp/wiki-css/merged.css` audit):
- `.mw-editsection a` — `[edit]` links next to headings (was full-width block)
- `.catlinks li` — category pills (`HTML | Computer-related | ...`)
- `.mw-cite-backlink`, `.mw-indicator` — inline back-reference markers
- `.navbar ul`, `.navbar` — navigation tab bars
- `.plainlinks`, `.sister-logo`, `.sister-link` — inline images/links
- `.cdx-button`, `.cdx-message__icon` — search/message icons
- `.vector-pinned-container`, dropdown labels

Approximately 30-50 DOM elements on Wikipedia's HTML article that will now render correctly instead of as blocks.

## What this does NOT fix

- **Baseline alignment** — approximated as top-aligned, not baseline-aligned
- **Vertical-align on the inline-block itself** — deferred to ADR-082
- **Nested inline-blocks** — an inline-block inside an inline-block recurses but the inner would be laid out as part of the outer's block layout, which is correct per CSS but untested
- **Width: auto shrink-to-fit** — inline-block with no explicit width falls through to full content_w. Correct behavior would be shrink-to-content. Deferred.

## Tests

ADR-081 coverage includes:
1. `display_of` maps `"inline-block"` to `Display::InlineBlock`
2. Inline-block children are routed through the inline formatting run.
3. Inline-block fragments carry the laid-out child box.
4. Atomic fragment width uses the child's dimensions and margins.
5. Ordinary inline elements do not become atomic inline-block fragments.
6. Painting recursively renders the inline-block subtree.
7. Painting positions the border box after its top and left margins.
8. Descendant culling uses the translated viewport when the inline-block is away from the document origin.

## References

- CSS 2.1 §9.2.2.1 (inline-block definition)
- ADR-016 — inline formatting context
- ADR-073 — permanent tracing
- `/tmp/wiki-css/merged.css` — the audit that quantified need
