# ADR-054: CSS transform: rotate()

**Status:** Implemented
**Date:** 2026-11-01
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-013 (length units), ADR-034 (image paint), ADR-039 (box model), ADR-044 (border-radius paint), ADR-053 (clamp machinery)

## Context

Wikipedia's only `transform` declaration reaching cascade is
on the search magnifier icon. Measured 2026-11-01 via
`HONEYBEE_LOG=all` diagnostic:

```
TRANSFORM-APPLIED: tag=span class=cdx-text-input__icon
  cdx-text-input__start-icon prop=transform val=translateY(-50%)
ICON-MEASURE: x=427.50 y=7.00 w=20.00 h=20.00
```

**Rotate is not used in production.** But rotate primitives
are architecturally useful (icon rotation, chart markers,
future menu chevrons) and verifiable via a synthetic
fixture. This ADR ships rotate only, defers translate.

### Why translateY is deferred

The magnifier at y=7 (container top) needs **both**
`top: 50%` **and** `translateY(-50%)` to center at y=18
(in a 42px container). `top: 50%` requires
`parent_content_height` in `LengthContext` -- the same
blocker as ADR-053's `%` max-height deferral.

`translateY(-50%)` alone shifts the icon to y=-3 (above
the search box). That is a **regression**, not an
improvement. Deferred until `parent_content_height`
threading (separate ADR).

### What rotate requires (four layers)

- **No angle parsing exists.** `parse_length` handles
  px/em/rem/%/vw/vh/vmin/vmax -- **not** deg/rad/turn/grad.
- **`transform` not in whitelist** -- parser drops it before
  cascade.
- **No LayoutBox field** -- even if parsed, nowhere to store.
- **No paint rotate wrapper** -- `paint.cpp` uses cairo but
  has zero `cairo_rotate` calls today.

## Decision

Ship `transform: rotate(<angle>)` end-to-end across four
layers:

1. **Whitelist** `transform` and `transform-origin` in
   `supported_properties()` (stylesheet.cpp)
2. **New `parse_angle(sv)`** in style_values -- handles
   `deg`/`rad`/`turn`/`grad`; default unit is deg per CSS
3. **New `float transform_rotate_deg = 0.0f`** in LayoutBox
4. **`layout_element`** extracts `rotate(<angle>)` from the
   transform string, stores angle. Other functions
   (`translate`/`scale`/`matrix`) skipped in v1.
5. **`paint_box`** wraps the whole body in
   `cairo_save` -> `cairo_translate(cx, cy)` ->
   `cairo_rotate(angle)` -> `cairo_translate(-cx, -cy)` ->
   body -> `cairo_restore`, where `(cx, cy)` is the box
   center.

## Design decisions

### Why wrap the entire paint_box body

CSS `transform` affects the element's **rendered pixels**:
background, border, box-shadow, content, children --
everything. Wrapping only `paint_children` would rotate
content but leave the background axis-aligned. Wrapping
only the background would miss the children.

The whole body from `if (!box.visible) return;` through the
`if (box.clip_children) cairo_restore(cr);` at the end goes
inside one `cairo_save`/`cairo_restore` pair, with the
rotate translate sandwich inserted between them.

**Exception**: the box-shadow `cairo_save`/`cairo_restore`
at line ~195 already has its own pair for clipping. Nested
save/restore is valid in cairo -- they push/pop independent
stack frames. No conflict.

### Why deg default

CSS spec: `rotate(45)` is degrees (unitless angle = deg).
Handle `deg`, `rad`, `turn`, `grad` for completeness. Use
`M_PI / 180.0f` for deg -> radians conversion at paint
(0.0174533 per degree).

### Why parse_angle separate from parse_length

`parse_length` returns pixels (float). Angles have no
pixel dimension. `parse_length("45deg")` returns 0 (unknown
unit). Separate function keeps semantics clean.

Alternative (rejected): teach `parse_length` to return
deg when unit is deg. Conflates units with pixels in callers
that expect pixel values (width, height, margin).

### Why rotate-only extraction (not full transform parser)

CSS `transform` can contain multiple functions:
`rotate(45deg) translateX(10px) scale(2)`. Proper
composition requires 2D affine matrix stacking. Out of
scope.

v1: extract the **first** `rotate(<angle>)` substring,
ignore everything else. For `translateY(-50%)` (the actual
Wikipedia case), no rotate() exists, so field stays 0,
paint applies no transform -- **zero delta**.

### transform-origin v1

Default `50% 50%` (box center). The pivot is
`(box.x + box.width/2, box.y + box.height/2)`. Custom
origins deferred.

### The `<img>` limitation

`layout_element` returns early for `<img>` with valid
dimensions (line ~1721). Transform parsing goes after
that return, so `<img>` elements **don't get transform
applied** in v1. Non-img elements (span, div, p) work
correctly. The search magnifier is a `<span>`, so it
would get transform if we implemented translateY.

Documented limitation. Fix path: move parse before the
img early-return, or add it to the img block separately.
Separate ADR, not this one.

## Deferred

- **`transform: translate()` / `translateX()` / `translateY()`**
  -- blocked on `parent_content_height` (per Context)
- **`transform: scale()`** -- different paint operation
- **`transform: matrix()`** -- full affine; not urgent
- **Multiple transform functions** (`rotate(...) scale(...)`)
  -- v1 extracts rotate() only, ignores rest
- **`transform-origin` custom values** -- v1 always center
- **`transform` on `<img>`** -- early-return path limitation

## Consequences

**Better:**
- Rotate primitives work: any non-img box with
  `transform: rotate(Ndeg)` paints rotated by N degrees
  around its center
- **Zero regression to `translateY(-50%)` on the search
  magnifier** -- rotate parser finds no `rotate(` in the
  string, leaves field 0, paint applies nothing. Before/
  after screenshots identical.
- `parse_angle` reusable for future rotate/filter/
  gradient-angle consumers
- Test count 647 -> 651 (+4 tests)

**Accepted limitations:**
- **Zero production PNG delta.** No Wikipedia element uses
  rotate. Verified via **synthetic fixture only** -- a new
  "synthetic-fixture verified" category, structurally the
  same as "scroll-PNG verified" but using a `data:` URL
  instead of a scroll position.
- **Combined transform functions** limited to first rotate()
- **Custom transform-origin** deferred
- **`<img>` transform** deferred

**Unchanged:**
- All ADR-005 through ADR-053 behavior
- `translateY(-50%)` on the search magnifier remains
  unpainted (no regression, no improvement)
- Alpha-blending of `<img>` and content

**Possible regression risks (verify in Commit 1):**
- Whitelisting `transform` means `translateY(-50%)` now
  reaches cascade, but layout's rotate-only parser leaves
  it at 0 -- paint draws unchanged. **Verify via before/
  after screenshots** (should be identical)
- Angle parser mishandling `none` -> treated as no
  transform (no rotate), correct
- Multi-rotate-elements in one document -- each box has
  its own pivot (box center), correct

## Implementation plan

Two commits:

1. **parse_angle + LayoutBox field + layout + paint wrapper + tests**:
   - style_values.hpp: `float parse_angle(std::string_view s);`
   - style_values.cpp: implementation (deg/rad/turn/grad)
   - layout_box.hpp: `float transform_rotate_deg = 0.0f;`
   - stylesheet.cpp: whitelist `transform`, `transform-origin`
   - layout.cpp: extract rotate() from transform string
   - paint.cpp: wrap paint_box body with save/rotate/restore
   - 4 tests

2. **Docs**: ADR-054 -> Implemented, README row + CHANGELOG

## Verification

**Unit tests (4 new):**
- `parse_angle` handles deg/rad/turn/unitless-deg
- `layout: transform:rotate(45deg)` -> LayoutBox field = 45
- `layout: no transform` -> LayoutBox field = 0
- `layout: translateY(-50%)` -> LayoutBox field = 0
  (rotate parser skips it, no regression)

**Synthetic fixture verification:**
```
data:text/html,<html><body style="margin:20px;padding:0">
<div style="width:100px;height:100px;background:red;
            transform:rotate(45deg)"></div></body></html>
```
- Before: red square at (20,20,120,120)
- After: red diamond, center at (70,70), corners at
  (20,70)/(70,20)/(120,70)/(70,120)
- User visually confirms (HoneyBee screenshot)

**Diagnostic fprintf (temporary, removed before commit):**
`ROTATE-PAINT: cx=70.00 cy=70.00 angle=45.00deg` at paint_box
-- 3-layer verification (parse check, layout check, paint
check)

**Wikipedia regression check:** zero-delta PNG on HTML
article (the magnifier's translateY is parsed but not
painted -- unchanged before/after)

## Acceptance criteria

ADR-054 is complete when:

- [ ] `parse_angle` handles deg/rad/turn/grad/unitless
- [ ] `transform` and `transform-origin` in whitelist
- [ ] `LayoutBox::transform_rotate_deg` field
- [ ] `layout_element` extracts rotate() from transform string
- [ ] `paint_box` wraps body with save/rotate/restore
- [ ] 4 new tests pass (647 -> 651)
- [ ] Synthetic fixture: square rotates to diamond (visual)
- [ ] Wikipedia HTML article: zero PNG delta (magnifier
      translateY parsed but not painted)
- [ ] README row + CHANGELOG
- [ ] ADR-054 status -> Implemented

## Implementation

Implemented on `feat/transform-rotate-impl` across two commits
(Commit 1 = four-layer implementation + fixture, Commit 2 =
this docs update).

### What shipped

**`style_values.hpp` / `.cpp`:**
- New `float parse_angle(std::string_view s)`
- Handles `deg` / `rad` / `turn` / `grad`; unitless = degrees
- Returns 0 for empty / malformed / unknown units

**`stylesheet.cpp`:**
- `"transform"` and `"transform-origin"` added to
  `supported_properties()`

**`layout_box.hpp`:**
- New `float transform_rotate_deg = 0.0f`

**`layout.cpp`:**
- Extract first `rotate(<angle>)` from the transform string
  during `layout_element`; other functions (translateY,
  scale, matrix) skipped in v1

**`paint.cpp`:**
- Wrap `paint_box` body in `cairo_save` / `translate(cx,cy)`
  / `rotate(rad)` / `translate(-cx,-cy)` / `cairo_restore`,
  where `(cx, cy)` is the box center
- 0 degrees = no-op (`did_rotate` stays false, zero cairo
  state change)

**`test_layout.cpp`:**
- 4 new tests: `parse_angle` units, `rotate(45deg)` layout,
  no-transform layout, `translateY(-50%)` regression safety

**`docs/adr054-rotate-test.html`:**
- Permanent reproducibility fixture with three stacked
  boxes: control square, `rotate(45deg)` diamond,
  `rotate(30deg)` tilted rectangle

### Verified delta (synthetic fixture)

**Screenshot at 2026-11-01 confirmed**:

```
Control (no transform):     upright orange square
rotate(45deg):              orange DIAMOND, corners at
                            rotated positions
rotate(30deg) rectangle:    tilted blue rectangle
```

All three render correctly. First cycle to establish the
"synthetic-fixture verified" category with a concrete visual
result.

**Diagnostic logs during the verification run** confirmed
each layer:

```
ADR-054-LAYOUT-INPUT: tag=div class="box rotated-45"
  transform=rotate(45deg)
ADR-054-LAYOUT: tag=div angle=45.00
ADR-054-PAINT: angle=45.00 cx=72.00 cy=280.00 rad=0.7854
```

### A real finding during fixture setup

The first fixture used CSS shorthand:

```css
background: #d35400;
border: 2px solid #333;
```

**Both silently failed** -- only the longhand forms render.
The fixture was rewritten with:

```css
background-color: #d35400;
border-width: 2px;
border-style: solid;
border-color: #333;
```

This is a **pre-existing parser gap** (shorthand
`background` and `border` are not decomposed into
longhands). It is **out of ADR-054's scope** -- rotate is
not the cause and the fix belongs in a dedicated shorthand
ADR. Documented here so future contributors using the
fixture know to expect it.

### Honest delta framing

**Synthetic fixture: visible** -- 3 boxes render with
correct rotation. The diamond is unmistakable; the tilted
rectangle confirms non-45-degree angles.

**Wikipedia: zero production PNG delta** -- the only
transform reaching cascade is `translateY(-50%)` on the
search magnifier (from ADR-054's Context measurement). The
rotate parser skips it (no `rotate(` substring), leaves
`transform_rotate_deg = 0`, paint applies no transform.
Before/after screenshots on the HTML article are
**identical** -- the primary safety property.

### Test count

- 647 -> 651 cases (+4 new)
- 2471 -> 2490 assertions (+19 new)
- Zero pre-existing tests affected

### New verification category

**"synthetic-fixture verified"** -- structurally the same
rigor as "scroll-PNG verified": a committed `.html` fixture,
a `python3 -m http.server` local serve, and a visual
confirmation of the render. Uses `file://` + local server
because the project's fetch layer doesn't implement
`file://` yet (URL validator accepts it; `net/client.cpp`
returns status 0 -- separate latent inconsistency, future
ADR territory).

### Deferred (documented)

- `translate()` / `translateX()` / `translateY()` --
  blocked on `parent_content_height` threading
- `scale()` / `matrix()` -- different paint operations
- `transform-origin` custom values -- v1 always 50% 50%
- `transform` on `<img>` -- early-return path limitation
- **Background / border shorthand parsing** -- the fixture
  bypass revealed this gap; separate ADR needed

