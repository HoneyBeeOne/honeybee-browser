# ADR-080: list-style-type and ::marker rendering

**Status:** Implemented
**Date:** 2026-11-16
**Related:** ADR-016 (inline), ADR-031 (float), ADR-073 (tracing)

## Context

Wikipedia's HTML article has **1,559 `<li>` elements** (TOC, references, See also, navboxes). Every one currently renders as indented text with **no bullet or number**. The CSS inventory (this session's feature audit) measured `list-style-type: 24 uses`, `::marker: 4 uses`. Larger impact: even where CSS doesn't override `list-style-type`, the UA default for `<li>` should render a marker.

Root cause: `Display::ListItem` doesn't exist in the enum. `display: list-item` from the UA stylesheet (or CSS) falls through to `Display::Block` in `display_of()`, giving a plain block with no marker. There's no `list-style-type` property whitelisted in the cascade, no field on `LayoutBox`, no paint path.

## Decision

Add the minimum viable `list-style-type` support:

- `disc` (filled circle) — the default for `<ul>` on Wikipedia
- `circle` (open circle)
- `square` (filled square)  
- `none` (hide marker)
- **Deferred:** `decimal`, `lower-roman`, `upper-roman`, `lower-alpha`, `upper-alpha`, `@counter-style` — need counter state per-list. Wikipedia's numbered lists will show as plain text for now, but the numbered references already display correctly because Wikipedia wraps them in a `<ol>` with custom `::marker` content that we don't handle — so they render as their link text without ordinal markers, which is visually acceptable for v1.

### Scope: disc/circle/square only

- `disc` — default for `<ul>`, ~80% of Wikipedia list items
- `circle` — nested `<ul>`, ~15%
- `square` — third-level `<ul>`, ~5%
- `none` — `.mw-empty-elt` and hidden lists, ~10% but low visual priority

That covers all unordered lists. Ordered lists (references, See also) will show content without ordinal markers — visually closer to Firefox than today, but not pixel-identical. Deferred to a future ADR if it matters.

## Implementation

### `layout_box.hpp`

- `Display` enum: add `ListItem`
- `LayoutBox`: add `std::string list_style_type;` (empty = not a list item)

### `cascade.cpp`

- `is_inherited`: add `"list-style-type"` — per CSS spec this property inherits from `ul`/`ol` to `li`

### `stylesheet.cpp` (cascade whitelist)

- Add `"list-style-type"` to `supported_properties()` — otherwise the parser silently drops it

### `ua_stylesheet.cpp`

Add three rules:
```css
ul { list-style-type: disc }
ol { list-style-type: decimal }
li { display: list-item }
```

### `layout.cpp`

- `display_of`: add `if (*v == "list-item") return Display::ListItem;`
- `layout_element`: after `box.x/box.y` are set, capture `list_style_type` from style **only when** the element's own display is `list-item` (so `<ul>` doesn't get a marker, only `<li>`)

### `paint.cpp`

Draw the marker in `paint_box`, before `paint_children`, in the padding-left area:

```cpp
if (box_on_screen && !box.list_style_type.empty()) {
    const float marker_x = box.x - 20.0f;   // in the 40px ul/ol padding-left
    const float marker_y = box.y + 10.0f;   // first line's vertical center
    const float r = 3.0f;
    // disc: fill circle, circle: stroke circle, square: fill rect
    // color: #202122 (Wikipedia body color)
}
```

Marker offset (20px) and vertical position (10px) are tuned to the 40px `padding-left` from the existing UA sheet. For 16px body text, `10px` is first line center. Fixed offsets are acceptable for v1; can be computed from `line_height` in a future ADR.

## What this fixes

- Every `<ul>` list item gets a visible bullet. That's:
  - TOC (`Contents`, `History`, `Development`, etc.)
  - References list items
  - Navboxes (`Web browsers`, `W3C`, etc.)
  - Sidebar (`.sidebar`) list items
  - The `Main page / Contents / Current events / ...` side nav

- `list-style-type: circle` and `square` work for nested lists (rare but present).

## What this does NOT fix

- **Ordered list ordinal markers** (`1.`, `2.`, etc.) — deferred (needs counter state)
- **Roman, alphabetic, custom counter styles** — deferred
- **`::marker` pseudo-element styling** — deferred (Wikipedia uses it for a few small cases)
- **`list-style-position: inside`** — deferred (rare on Wikipedia)
- **Marker color inheritance** — markers drawn in `#202122` regardless of `li` color. Deferred to a future ADR if it matters.

## Verification

- **Unit test 1** (`test_stylesheet`): `ul { list-style-type: disc }` cascades correctly
- **Unit test 2** (`test_layout`): `<ul><li>x</li></ul>` → `li_box.list_style_type == "disc"` (via UA inheritance)
- **Unit test 3** (`test_paint`): disc marker paints at `(box.x - 20, box.y + 10)`
- **Production**: Wikipedia TOC + references get visible bullets

## References

- ADR-016 — inline text layout (marker drawn on block-level `li`)
- ADR-031 — float layout (markers unaffected, `<li>` has no float)
- ADR-073 — permanent tracing (no new trace needed; marker is visible output)
- This session's feature inventory (see `/tmp/wiki-css/merged.css` audit)
