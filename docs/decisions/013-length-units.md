# ADR-013: Length Units and Auto Margins

**Status:** Accepted
**Date:** 2026-10-20
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-010 (cascade), ADR-011 (layout), ADR-012 (shorthand expansion)

## Context

As of PR #113, `margin: 10px 20px` renders. But anything that isn't `Npx` silently becomes 0. Testing example.com against our browser compared with Firefox surfaced the gap. Example.com's real CSS:

```css
div {
  width: 600px;
  margin: 5em auto;
  padding: 2em;
  background: #fdfdff;
  border-radius: 0.5em;
  box-shadow: 2px 3px 7px 2px rgba(0,0,0,0.02);
}
```

Three lines fail silently:

- `width: 600px` -> works (px)
- `margin: 5em auto` -> `5em` becomes 0, `auto` becomes 0
- `padding: 2em` -> becomes 0

Visible result: the div is 600px wide, hugging the top-left of the window. Firefox centers it horizontally, insets content 2em, spaces it 5em from above.

The remaining lines (`border-radius`, `box-shadow`) are unsupported properties -- a different scope, not this ADR.

This ADR closes the length-unit and `auto` gaps so that real stylesheets render as written.

## Decision

We add **length units** (`px`, `em`, `rem`, `%`) and **auto margins** for horizontal centering, resolved at layout time via a context-aware `parse_length`.

### Supported units (v1)

| Unit | Resolves against | Notes |
|---|---|---|
| `px` | (absolute) | already works |
| `em` | font-size (context-dependent) | new |
| `rem` | root element's computed font-size | new |
| `%` | parent content width | new; see quirk below |
| `auto` | context-dependent | new; see centering below |

### Deferred units

`vw`, `vh`, `vmin`, `vmax`, `ch`, `ex`, `pt`, `pc`, `in`, `cm`, `mm`, `q`, `calc()`, `min()`, `max()`, `clamp()`, `min-content`, `max-content`, `fit-content`.

### `em` resolution is context-dependent

This is the subtlety that makes `em` different from `rem`:

```
   font-size: 1.5em      ->  resolves against PARENT font-size
   other properties' em  ->  resolves against OWN font-size

   <html>  (no font-size set; browser default 16px)
      |
      +-- <body>  font-size: 1.5em
      |           -> 16 * 1.5 = 24px
      |           |
      |           +-- <p>  no font-size declaration
      |           |       -> inherits 24px (from <body>)
      |           |
      |           +-- <div>  font-size: 2em; padding: 1em
      |                   font-size: 24 * 2 = 48px
      |                   padding:  48 * 1 = 48px
```

The `1em` on `padding` uses the element's own 48px font-size, not the parent's. The `2em` on `font-size` uses the parent's 24px. Getting this right requires threading parent font-size through the layout recursion.

### `%` resolution uses parent width, not height

CSS 2.1 quirk, applies to all box properties:

```
   width: 50%         ->  parent_content_width * 0.5
   margin-left: 10%   ->  parent_content_width * 0.1
   margin-top: 10%    ->  parent_content_width * 0.1   (NOT parent height)
   padding-top: 10%   ->  parent_content_width * 0.1
```

Vertical percentages resolve against the parent's **width**, not its height. Following the spec (also simpler than trying to distinguish sides).

`height: %` is deferred: resolving height percentages correctly requires intrinsic-height calculation, which v1 layout does not do cleanly.

### `auto` for horizontal margins centers the element

```
   parent content width = 800
   child  width: 600px, margin-left: auto, margin-right: auto

   available = 800 - 600 - (padding-left + padding-right + border-left + border-right)
   each auto side = available / 2 = 100

   +-----+------------------+-----+
   | 100 |      600px       | 100 |
   +-----+------------------+-----+
         ^                  ^
         margin-left: auto   margin-right: auto
```

`auto` semantics:

- `margin-left` / `margin-right` when width is explicit -> center
- `width: auto` -> fill parent (already the layout default; unchanged)
- `margin-top` / `margin-bottom: auto` -> 0 (no vertical centering in v1)
- `padding: auto` -> invalid in CSS -> 0
- Any other `auto` -> 0

### Cascade change: font-size inheritance moves to layout

Today `font-size` is one of the seven inherited properties in `cascade.cpp`. Once `em` is supported, inheriting the raw `font-size` string is incorrect:

```
   <body>  font-size: 1.5em     (resolves to 24px against 16px default)
   <p>     no font-size         (inherits raw "1.5em" via cascade)
```

If layout then resolves `"1.5em"` against `<p>`'s parent (24px), it gets 36px -- wrong. It should be 24px.

CSS resolves `em` on `font-size` against the parent's *computed* font-size, so inheritance passes the computed pixels, not the raw string.

Fix: **`font-size` is removed from `kInheritedProperties`** in `cascade.cpp`. Layout threads `parent_font_size` through the recursion and computes each element's font-size from (a) its own declaration if present, or (b) the parent's computed value. This gives the correct answer without cascade needing to know about length units.

The other six inherited properties (`color`, `font-family`, `font-style`, `font-weight`, `line-height`, `visibility`) are unaffected -- none of them use `em` that resolves against font-size context.

## Rationale

### 1. Silent 0 is the biggest single bug

Every contributor writing real CSS hits one of the length-unit or `auto` patterns and sees nothing happen. Fixing these makes the browser behave the way the CSS they wrote implies.

### 2. `em` context matters more than people think

Most CSS authors don't realize `font-size: 1.5em` and `padding: 1.5em` use different reference sizes. Getting this wrong produces off-by-a-scalar bugs that are hard to diagnose. Passing an explicit `LengthContext` through layout forces both the implementation and the tests to be explicit about which context applies.

### 3. `%` against width is a spec quirk worth encoding

Many "why is my margin-top wrong" Stack Overflow questions trace back to `%` on vertical box properties resolving against parent width. Following spec from day one avoids this trap.

### 4. `auto` is a targeted addition

Only horizontal margins benefit from `auto` in v1. Treating it as a special case in the margin computation is ~15 lines, not a general "auto value" system. When flexbox or grid arrive, they will need richer `auto` handling -- but that is their ADR, not this one.

### 5. Font-size inheritance belongs in layout

The alternative -- resolving `em`-based font-size at cascade time so inheritance carries a computed pixel value -- would require `cascade()` to know about length units, length contexts, and the DOM tree simultaneously. That couples the cascade to layout concerns. Keeping font-size computation in layout means the cascade stays a pure string-in, string-out operation, which has been its design since ADR-010.

## Alternatives Considered

### Support viewport units (`vw`, `vh`) in v1

**Rejected for v1.** Requires passing viewport dimensions into layout (already available as `viewport_width`, but not `viewport_height` -- and passing height in requires window-resize plumbing). Deferred to a future ADR, probably alongside responsive design.

### Defer `auto` margins

**Rejected.** `margin: 0 auto` is one of the most common CSS idioms. Without it, centering is impossible. The cost is small.

### Keep `font-size` in cascade inheritance, resolve `em` twice

**Rejected.** Would let `<p>` with no font-size declaration see `"1.5em"` in `ComputedStyle::get("font-size")` and resolve it against `<p>`'s parent font-size, which double-counts the multiplier. The bug would only appear with nested `em`-based font-size, which is exactly what real pages use.

### Resolve `em` against the element's own font-size for `font-size` too

**Rejected.** Circular: `font-size: 1.5em` would need to know its own font-size to compute its own font-size. The spec is right -- parent for `font-size`, own for everything else.

## Consequences

### Files changed

- `src/renderer/layout/style_values.hpp` / `.cpp` -- replace `parse_px` with `parse_length(std::string_view, const LengthContext&)`; new `LengthContext` struct; new `is_auto(std::string_view)` helper
- `src/renderer/layout/layout.hpp` / `.cpp` -- thread `parent_font_size` and `root_font_size` through `layout_element`; resolve all lengths via `parse_length`; implement `auto` horizontal centering
- `src/renderer/css/cascade.cpp` -- remove `font-size` from `kInheritedProperties`
- `tests/test_layout.cpp` -- new tests for every unit and every `auto` case; one existing test updated (font-size inheritance)
- `tests/test_cascade.cpp` -- one existing test updated (`font-size` no longer inherited)

No new files. No CMake changes. No changes to `dom`, `html`, `paint`, `selectors`, or `stylesheet`.

### `LengthContext` shape

```cpp
struct LengthContext {
    float font_size = 16.0f;            // own font-size, for em on non-font-size properties
    float parent_font_size = 16.0f;     // for em on font-size property itself
    float root_font_size = 16.0f;       // for rem
    float parent_content_width = 0.0f;  // for %
    bool on_font_size_property = false; // selector for which font-size to use for em
};
```

### Behavior examples

| CSS (in 800px parent, root font-size 16) | Before | After |
|---|---|---|
| `p { padding: 1em }` | padding 0 | padding 16 |
| `p { padding: 2em }` (with font-size 20) | padding 0 | padding 40 |
| `p { margin: 5em }` | margin 0 | margin 80 |
| `p { font-size: 1.5em }` | font-size 16 (fallback) | font-size 24 |
| `html { font-size: 20px }` `p { font-size: 2rem }` | 16 (fallback) | 40 |
| `p { width: 50% }` | 0 | 400 |
| `p { margin-top: 10% }` | 0 | 80 (parent width, not height) |
| `p { width: 600px; margin-left: auto; margin-right: auto }` | 600px, left-aligned | 600px, centered |
| `p { width: auto }` | fills parent | fills parent (unchanged) |

### Font-size computed in layout, not cascade

Layout tracks `parent_font_size` and `root_font_size` as recursion parameters:

```cpp
float font_size_of(const css::ComputedStyle* style,
                   float parent_font_size, float root_font_size);

LayoutBox layout_element(..., float parent_font_size, float root_font_size);
```

Root font-size is computed once at the top of `layout()` by resolving `<html>`'s font-size against 16 (browser default if no declaration).

### `auto` for width unchanged

`width: auto` already means "fill parent" in the current layout. This ADR does not change that. Only `margin-left: auto` and `margin-right: auto` gain new behavior.

## Open Questions

### Viewport units

`vw`, `vh`, `vmin`, `vmax` need `viewport_height` plumbed into layout, which needs window-resize events to trigger relayout. Deferred to a responsive-design ADR.

### `calc()` and functions

`calc(100% - 20px)` needs an expression parser. Deferred.

### Font-relative units (`ch`, `ex`)

`ch` is the width of "0" in the element's font, `ex` is the x-height. Both require Pango font metrics queries at layout time. Deferred until a font-metrics ADR lands.

### Absolute units (`pt`, `cm`, `mm`, `in`)

96px = 1in, 1pt = 1/72in, etc. Trivial to add but rarely used on screen. Deferred.

### Inherited lengths beyond font-size

`line-height: 1.5` (unitless) is inherited as a multiplier, not an absolute value. `line-height: 1.5em` inherits as absolute. This distinction matters when line-height support lands. Deferred to a line-height ADR.

## Record of Changes

- **2026-10-20:** Initial decision.
