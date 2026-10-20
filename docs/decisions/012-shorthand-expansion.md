# ADR-012: Shorthand Expansion

**Status:** Accepted
**Date:** 2026-10-20
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-009 (stylesheet parser), ADR-010 (cascade), ADR-011 (layout)

## Context

As of PR #110, the browser renders HTML pages end-to-end. The pipeline from HTML bytes to rendered pixels works: parse, cascade, layout, paint.

But a silent failure lurks. Today:

```css
p { margin: 10px 20px }
```

produces a single declaration `{ property="margin", value="10px 20px", important=false }`. The cascade stores it. Layout then calls `parse_px("10px 20px")` in `fill_sides`, which returns `0.0f` because it rejects multi-value input (see ADR-011's `style_values.cpp`). **The margin silently vanishes.**

The same happens with `margin-top: 10px` written directly: `fill_sides` reads `margin` from computed style, not `margin-top`, so the longhand is also ignored.

Every contributor who writes normal CSS hits this and sees a browser that "doesn't do margins." This ADR fixes that.

## Decision

We make standard box-model CSS actually render, via two coordinated changes:

1. **Parse-time expansion of shorthands.** `Stylesheet::parse` expands `margin`, `padding`, and `border-width` in 1-, 2-, 3-, or 4-value form into four longhand declarations each.
2. **Longhand consumption in layout.** `fill_sides` reads the four longhand properties (`margin-top`, `margin-right`, `margin-bottom`, `margin-left`, etc.) individually, instead of one shorthand property applied to all sides.

The cascade and paint layers are untouched.

### Scope of v1

**Shorthands expanded:**

| Property | Longhands produced |
|---|---|
| `margin` | `margin-top`, `margin-right`, `margin-bottom`, `margin-left` |
| `padding` | `padding-top`, `padding-right`, `padding-bottom`, `padding-left` |
| `border-width` | `border-top-width`, `border-right-width`, `border-bottom-width`, `border-left-width` |

**Value forms expanded:** 1, 2, 3, or 4 space-separated values, per CSS 2.1 §8.1.

**Longhands consumed by layout:**

- `margin-top`, `margin-right`, `margin-bottom`, `margin-left`
- `padding-top`, `padding-right`, `padding-bottom`, `padding-left`
- `border-top-width`, `border-right-width`, `border-bottom-width`, `border-left-width`

### Expansion rules

The 4-value order is clockwise from the top:

```
              top
               |
       left ---+--- right
               |
             bottom

   margin: T R B L

      +---------------------------------+
      |              T                  |
      |        +-------------+          |
      |        |             |          |
      |    L   |   content   |   R      |
      |        |             |          |
      |        +-------------+          |
      |              B                  |
      +---------------------------------+
```

The 1/2/3/4-value expansion:

```
   Input      top    right   bottom   left

     1         1       1       1       1
     2         1       2       1       2      (top = bottom, right = left)
     3         1       2       3       2      (right mirrors to left)
     4         1       2       3       4      (all four distinct)
```

That is the whole algorithm: roughly 30 lines of parsing plus the table.

### Deferred to future ADRs

- `border` shorthand itself (combines `width` + `style` + `color`)
- `border-color` shorthand (would require `LayoutBox::border_color` to become `Color[4]`; a layout-box change)
- `background` shorthand (color + image + repeat + position)
- `font` shorthand (family + size + weight + style + line-height)
- `overflow`, `list-style`, `transition`, `animation`
- `calc()`, `var()`, and CSS-wide keywords (`inherit`, `initial`, `unset`)
- `auto` values for `margin` (requires container-aware centering)
- Named border widths (`thin`, `medium`, `thick`)

## Rationale

### 1. Expand at parse time, not cascade time

The alternative is to expand during cascade: `cascade()` sees `margin: 10px 20px` and writes four longhand `CascadeValue`s. That would be more faithful to CSS (the spec expands shorthands at computed-value time), but it costs more:

- `ComputedStyle` is `unordered_map<property, CascadeValue>`. Expanding at cascade time means every shorthand declaration spawns four entries, each carrying its own `(important, specificity, source_order)` — which is exactly what cascade-time expansion would have to synthesize anyway.
- The cascade would need to know which properties are shorthands and which longhands they cover. Avoidable coupling.
- Test surface doubles: expansion tests would live in `test_stylesheet.cpp` (parse-side) and `test_cascade.cpp` (cascade-side), or in neither cleanly.

Parse-time expansion puts the logic in one place — `Stylesheet::parse` — with one test file. The cascade and layout layers see only longhand properties.

### 2. Layout must consume longhands to complete the fix

Expanding `margin: 10px 20px` into four longhands is pointless if layout still reads `style->get("margin")`. Today's `fill_sides` reads the short-form property and applies one value to all four sides. After this ADR, it reads `margin-top/right/bottom/left` individually.

This also fixes a second bug: `p { margin-top: 10px }` written directly works after this ADR, because `fill_sides` now reads the longhand name.

### 3. Scope discipline

The three shorthands in scope are the ones layout currently consumes. `background`, `font`, and `overflow` shorthands would expand into declarations that no consumer reads — better to add them when the consumer arrives. `border-color` is deferred because its longhand array requires a `LayoutBox` struct change.

## Alternatives Considered

### Expand at cascade time

Rejected. Couples cascade to shorthand knowledge, doubles test surface, buys nothing at our scale. See rationale 1.

### Expand inside `parse_px`

Rejected. `parse_px` takes a single property value. Teaching it to expand `10px 20px` into four sides pushes shorthand knowledge into `layout.cpp`, where it doesn't belong.

### Don't expand — require longhand CSS only

Rejected. Every real stylesheet uses `margin: 10px 20px`. Refusing to expand means most pages fail silently on their most common shorthand.

### Expand all CSS 2.1 shorthands in one ADR

Rejected. `font` involves the `/` line-height separator. `background` involves URL resolution. `border` involves a style keyword system. Each is a distinct can of worms. Ship the three layout consumes; each additional shorthand is a future ADR.

## Consequences

### Files changed

- `src/renderer/css/stylesheet.cpp` — add `expand_shorthand` helper in the anonymous namespace; `add_declaration` calls it
- `src/renderer/layout/layout.cpp` — `fill_sides` reads `base-top`/`base-right`/`base-bottom`/`base-left` instead of `base`
- `tests/test_stylesheet.cpp` — expansion tests (8–10 new cases)
- `tests/test_layout.cpp` — longhand consumption tests (3–4 new cases)

No new files. No CMake changes. `Declaration`, `Rule`, `Stylesheet`, `ComputedStyle`, `LayoutBox`, and the paint module are all unchanged.

### Behavior examples

| Input CSS | Resulting declarations |
|---|---|
| `p { margin: 10px }` | four `margin-*: 10px` |
| `p { margin: 10px 20px }` | top/bottom `10px`, right/left `20px` |
| `p { margin: 10px 20px 30px }` | top `10px`, right `20px`, bottom `30px`, left `20px` |
| `p { margin: 10px 20px 30px 40px }` | top `10px`, right `20px`, bottom `30px`, left `40px` |
| `p { margin: 10px 20px 30px 40px 50px }` | (skipped — too many values) |
| `p { margin: 10px 20px !important }` | four longhands, each `important=true` |
| `p { margin-top: 10px }` | unchanged (already longhand) |

Visible layout after this ADR:

| CSS | Layout result |
|---|---|
| `p { margin: 10px }` | box offset 10px from parent on all sides |
| `p { margin: 10px 20px }` | 10px top/bottom, 20px left/right |
| `p { padding: 5px }` | content inset 5px from box border on all sides |
| `p { border-width: 1px 2px 3px 4px }` | four distinct border strip widths |

### What layout must not do

`fill_sides` must not fall back to reading the shorthand name. Once `margin: 10px 20px` is expanded at parse time, `ComputedStyle` has no `margin` entry. Layout reads the longhands. If a page somehow has a `margin` longhand reference that ADR-012 forgot to expand (i.e., a shorthand we don't handle), that's a symptom of a missing case, not a signal to add a fallback. The `expand_shorthand` table is the source of truth for which shorthands layout understands.

## Open Questions

### `border-color`

Expanding `border-color: red blue` into `border-top-color: red; border-right-color: blue; border-bottom-color: red; border-left-color: blue` is trivial at the parser level. But the paint stage draws a single `border_color` per box, not four. That requires `LayoutBox::border_color` to become `Color[4]`, and `paint_box` to draw each strip with its own color. Deferred to a follow-up ADR.

### `background` shorthand

`background: red` is one value; `background: url(x.png) no-repeat` is many. Requires URL loading and image drawing. Deferred.

### `font` shorthand

`font: italic bold 16px/1.5 serif` combines five properties. Requires parsing the `/` separator and typed values. Deferred.

### `auto` margins

`margin: 0 auto` centers a block. Requires layout to know its container width at the moment it lays out the child. Deferred.

### Named border widths

`border-width: thin` is 1px, `medium` 3px, `thick` 5px in browsers. Requires a `border-width` value parser beyond `parse_px`. Deferred.

## Record of Changes

- **2026-10-20:** Initial decision.
