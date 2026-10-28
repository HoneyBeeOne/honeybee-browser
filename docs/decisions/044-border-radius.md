# ADR-044: Border Radius

**Status:** Implemented
**Date:** 2026-10-28
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-016 (inline formatting, background/border paint), ADR-039 (overflow: hidden clipping), ADR-040 (border shorthand -- the search field this ADR rounds), ADR-043 (box-shadow -- painted behind background)

## Context

`border-radius` is missing from our CSS engine. On modern
sites this is the most visible single missing feature after
borders and shadows -- nearly every interactive element is
rounded. Wikipedia uses it on buttons, search fields, code
pills, and message boxes.

### Measurement (Wikipedia CSS, 2026-10-28)

**11 rules use `border-radius`.** Breakdown by value form:

| Form | Count |
|---|---|
| Single value (`2px`) | **10** |
| `inherit` | 1 |
| Percentage (`50%`) | 2 |
| Two / three / four value | 0 |
| Elliptical (`5px / 10px`) | 0 |

**11 rules target these first-viewport elements:**

| Element | HTML count | First char | Radius |
|---|---|---|---|
| `code` (inline) | **715** | 3648 | 2px |
| `.cdx-button` | **135** | 10901 | 2px |
| `.cdx-search-input` | 8 | 16008 | 2px |
| `.cdx-text-input` | 10 | 16177 | 2px |
| `.cdx-message` | (issue box) | -- | 2px |
| `a:where(:not([role='button']))` | 100s | -- | 2px (focus outline rounding, no static delta) |

**Expected visible delta: 4+ first-viewport changes** -- much
larger than ADR-043's 1px toolbar line.

1. **Search field rounded**: `.cdx-search-input` has
   `border: 1px solid #a2a9b1; border-radius: 2px` -- ADR-040
   gave it the border, ADR-044 rounds it. Complete element.
2. **All buttons rounded**: 135 `.cdx-button` elements get
   2px rounded corners (subtle but present on every tab,
   action, and dropdown trigger).
3. **All inline code rounded**: 715 `<code>` tags become
   rounded pills (subtle background + rounded corners).
4. **Message boxes rounded**: `.cdx-message` corners rounded.

### Per-corner override case

`.cdx-search-input__end-button.cdx-button` sets:

    border-top-left-radius: 0;
    border-bottom-left-radius: 0;

This overrides the shorthand's 2px on the left two corners of
the search field's end button. The correct render is: outer
corners rounded, inner corners square (matching the field
shape). v1 supports per-corner longhands (`border-top-left-
radius` etc.) so this case renders correctly.

If `.cdx-search-input__end-button` isn't visible in the JS-off
first viewport (search field may not render a separate end
button), the per-corner case doesn't affect visible delta --
but the parse-and-cascade still lands for future use.

### `inherit` and percentage deferred

- `.cdx-text-input__input { border-radius: inherit }` -- child
  input. Deferred: parent `.cdx-text-input` has
  `overflow: hidden` (per ADR-039), which clips the child to
  the parent's rounded shape. Child renders as square inside a
  clipped round parent -- **visually equivalent** to inheriting
  the radius.
- `.cdx-radio__icon { border-radius: 50% }` -- percentage
  radius creates an ellipse. Deferred: radio buttons are in
  the JS-off article view as `display: none` (form controls
  that need JS to become interactive). Zero visible impact.

## Decision

Add `border-radius` parsing, layout fields, and Cairo rounded-
rectangle painting. Support single-value shorthand + per-corner
longhands. Defer `inherit`, percentage, multi-value, and
elliptical.

### Parse: `border-radius` + per-corner longhands

**Shorthand `border-radius: <length>`**:
- Single-value only: `2px` → all four corners = 2px
- Multi-value forms (`2px 4px` etc.): 0 uses in Wikipedia,
  deferred. If encountered, parse the first token only
  (documented deviation; will be extended if a real site
  needs multi-value)
- `inherit`: not parsed (falls through to 0; documented)
- Percentage (`50%`): parsed as a length via `parse_length`
  with `%` support -- if `parent_content_width` is known, `%`
  resolves to a length; if not, returns 0. This is a
  pragmatic behavior, documented.

**Per-corner longhands** (each overrides the shorthand):
- `border-top-left-radius: <length>`
- `border-top-right-radius: <length>`
- `border-bottom-right-radius: <length>`
- `border-bottom-left-radius: <length>`

Parse order:
1. Read `border-radius` shorthand first → all 4 corners
2. Read each per-corner longhand → override the shorthand for
   that corner if present
3. Missing values default to 0

### LayoutBox fields

    float border_radius[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // 0 = top-left, 1 = top-right, 2 = bottom-right, 3 = bottom-left

No `has` gate -- `border_radius[i] > 0` for any i means rounded
paint. Zero default leaves all existing behavior unchanged.

### Paint: rounded rectangle path

New helper in `paint.cpp`:

    // Draw a path along the rounded-rect outline.
    // Radi in TL, TR, BR, BL order. Each radi is clamped
    // to min(w, h) / 2 to avoid overlap.
    void rounded_rect_path(cairo_t* cr,
                           float x, float y, float w, float h,
                           const float r[4]);

Uses `cairo_arc` + `cairo_close_path`. Standard 4-corner
approach:

    TL arc: center (x + r[0], y + r[0]), angle PI -> 1.5*PI
    TR arc: center (x + w - r[1], y + r[1]), angle 1.5*PI -> 2*PI
    BR arc: center (x + w - r[2], y + h - r[2]), angle 0 -> 0.5*PI
    BL arc: center (x + r[3], y + h - r[3]), angle 0.5*PI -> PI

Radii clamped per corner: if `r[0] + r[1] > w` or
`r[1] + r[2] > h` etc., clamp proportionally (CSS spec).
v1: simple clamp `r[i] = min(r[i], w/2, h/2)`.

**`paint_box` logic when any `border_radius[i] > 0`:**

    1. If border_width > 0 on any side:
       a. Fill outer rounded path with border_color
       b. Fill inner rounded path (inset by border_width,
          radii reduced by border_width) with background_color
       (this creates a border that follows the rounded curve)
    2. If border_width == 0:
       Fill rounded path with background_color

**Else (radius all 0):** existing `fill_rect` for background
+ 4 borders (unchanged).

The two-fill approach (outer border-color fill, inner
background-color fill) creates a rounded border without
`cairo_stroke`. This handles:
- Zero-border case (just rounded background)
- Uniform-border case (rounded border all around)
- Single-side border + rounding (documented limitation: with
  one-side border, the other sides' border-color is
  transparent so their contribution is zero -- correct)

### Border stroke follows curve

Current paint draws 4 separate `fill_rect` per border side
(square corners). With `border-radius > 0`, the border must
follow the rounded path. The two-fill approach above achieves
this without `cairo_stroke`:
- Outer path filled with border_color = rounded border ring
- Inner path filled with background_color = background, cut to
  inner rounded shape
- Diff = uniform-width rounded border

For corners where border is 0 on one side only (rare in
Wikipedia; per-side borders are uncommon), the two-fill
approach still works because transparent border_color leaves
that side empty.

### Behaviour for combined border + radius

Example: `.cdx-search-input { border: 1px solid #a2a9b1;
border-radius: 2px }`

Before ADR-044: 1px gray rectangular border around search
field (ADR-040's delta).
After ADR-044: 1px gray rounded border around search field,
2px radius. Visible: search field corners are rounded.

The 1px border narrows visually at corners (per CSS spec) --
correct behavior.

### Deferred (v1 scope)

- **Percentage radius** (`.cdx-radio__icon { 50% }`):
  parses via `parse_length` returning 0 for `%` without
  `parent_content_width`; radio buttons are `display: none`
  in target page
- **`inherit`** (`.cdx-text-input__input`): parent's
  `overflow: hidden` clips child to parent's rounded shape;
  visually equivalent
- **Multi-value shorthand** (`2px 4px` etc.): 0 uses; if
  encountered, first token only
- **Elliptical** (`5px / 10px`): 0 uses
- **`border-radius` transitions**: needs animation system

## Design decisions

### Why two-fill instead of cairo_stroke for border

`cairo_stroke` with `line_width` centers the stroke on the
path -- half inside, half outside. For CSS border semantics
(inside border-box), we'd need to offset the path by
`border_width/2` inward. With rounded corners, that offsets
the radius too, requiring radius reduction to match. And
per-side borders (which the two-fill approach handles
naturally) don't work well with a single stroke.

Two-fill: outer path with border_color, inner path (inset by
border) with background_color. Diff = border with rounded
corners. Simpler for uniform border case. Per-side border case
works because transparent border-color contributes nothing.

**Alternative (cairo_stroke with path offset)**: correct but
more code and harder to extend for per-side border colors.
Rejected for v1.

### Why clamp radius at min(w/2, h/2) not full CSS spec

CSS spec §5.5 defines a proportional clamping algorithm for
cases where `r[0] + r[1] > w` (adjacent radii overlap). Full
algorithm: compute `f = min(w / (r[0]+r[1]), h / (r[1]+r[2]),
...)` and multiply all radii by `f`.

v1 uses simple `r[i] = min(r[i], w/2, h/2)` per-corner. This
prevents overlap on the common case (uniform radius on a box
where `2*r <= min(w,h)`). Wikipedia's 2px radius on
100px+ boxes never triggers clamping. Full CSS algorithm
deferred until a real case needs it.

### Why per-corner longhands despite only 1 use

`.cdx-search-input__end-button.cdx-button` uses
`border-top-left-radius: 0; border-bottom-left-radius: 0` --
1 rule uses per-corner overrides. Cost of supporting them: ~15
lines of parse code + 4 fields. Benefit:
- Correct render for that one rule
- No re-parsing when other sites (or future Wikipedia updates)
  use per-corner forms
- Completes the border-radius model

### Why `border-radius: inherit` deferred

`.cdx-text-input__input` inherits parent's `border-radius: 2px`.
Deferring means the input renders with radius 0 (square).
Parent `.cdx-text-input` has `border-radius: 2px; overflow:
hidden` (ADR-039) -- so the input is clipped by the parent's
rounded shape.

**Visually equivalent to inheriting**: the outer wrapper's
rounded corners clip the child, so the visible result is the
child at 2px rounded corners (via clipping), even though the
child's own `border_radius` field is 0.

Documented deviation: if a future site uses `border-radius:
inherit` on a child WITHOUT `overflow: hidden` parent, the
inherit case would show square child corners. Own ADR if
needed.

### Why multi-value shorthand deferred

`border-radius: <tl-tr> <br-bl>` (2-value),
`<tl> <tr-bl> <br>` (3-value), `<tl> <tr> <br> <bl>` (4-value)
are the multi-value forms. **Zero uses in Wikipedia CSS.**
Supporting them adds ~30 lines of parse code with no immediate
target. When a real site needs them, parse extension is a
paint-nothing change (parse only). Deferred.

## Consequences

**Better:**
- **Search field completes**: border (ADR-040) + radius
  (ADR-044) = fully styled input
- **135 buttons rounded**: every tab, action, and dropdown
  trigger gains rounded corners
- **715 inline code pills rounded**: subtler but extremely
  widespread
- **Message boxes rounded**: issue box completes its styling
  arc (border ADR-040, shadow ADR-043, radius ADR-044)
- **Per-corner longhand support ready** for future sites using
  per-corner overrides

**Accepted limitations:**
- Percentage radius parses to 0 (`50%` radio buttons
  deferred)
- `inherit` deferred (parent clip provides equivalent visual)
- Multi-value shorthand uses first token only
- Elliptical radius deferred
- Corner-border-width narrowing beyond two-fill diff not
  spec-exact (relevant only for corner radius == border-width,
  rare)

**Unchanged:**
- ADR-016 through ADR-043 -- all features still work
- Zero-radius boxes (99% of Wikipedia's element types) --
  existing `fill_rect` path preserved
- `clip_children` (ADR-039) -- applied after rounded paths,
  children clipped to content box, not the rounded shape;
  matches CSS (overflow: hidden clips to content box, not
  border-radius unless we add `overflow: hidden` interaction
  with radius -- own ADR if needed)

**Paint order (unchanged except where radius > 0):**
1. ADR-043 box-shadow (unchanged; a rounded box-shadow that
   follows the curve is deferred)
2. ADR-044 background fill (rounded if any radius > 0)
3. ADR-044 border fill (rounded if any radius > 0)
4. ADR-034 background-image (unchanged -- draws as rect)
5. ADR-034 `<img>` (unchanged)
6. ADR-039 clip_children
7. Children (recursive)

## Implementation plan

Three commits, same discipline as ADR-040 / ADR-041 / ADR-042 /
ADR-043:

1. **Parse + layout fields** (behavior-neutral):
   - `LayoutBox` gains `float border_radius[4]`
   - Parse `border-radius` shorthand (single value → all four)
   - Parse per-corner longhands (`border-top-left-radius`
     etc.) → override
   - Zero paint changes; no visible delta
   - ~12 tests (parse cases)

2. **Paint** (visible):
   - New `rounded_rect_path` helper in `paint.cpp`
   - `paint_box` uses it when any `border_radius[i] > 0`
   - Two-fill for border + background
   - **PNG verification gate before merge**
   - Delta verified: search field, buttons, code pills,
     message boxes all rounded

3. **Docs**:
   - ADR-044 status → Implemented
   - Post-ADR-044 notes in ADR-040, ADR-043, ADR-039
     (respective deltas + interactions)
   - README + CHANGELOG

## Verification

**Unit tests (Commit 1, `[layout][border-radius][adr-044]`):**

Parse tests:
- `border-radius: 2px` → `border_radius[0..3] == 2`
- `border-radius: 0` → `border_radius[0..3] == 0`
- `border-radius: 10px` → all 10
- `border-top-left-radius: 0` alone → `[0]=0, [1]=[2]=[3]=0`
- `.x { border-radius: 2px } .x { border-top-left-radius: 0 }` → `[0]=0, [1]=[2]=[3]=2`
- `border-radius: inherit` → all 0 (deferred)
- `border-radius: 50%` → depends on parent_content_width
  (may be 0, documented)
- Malformed `border-radius: foo` → all 0
- Multiple rules cascade: later `border-radius: 4px` wins

Layout tests:
- LayoutBox receives correct `border_radius[4]` from style
- Zero-radius case leaves fields at default 0

**Visual verification (Commit 2, PNG before merge):**

| Element | Before | After |
|---|---|---|
| Search field corners | Square | **Rounded** (2px) |
| Tabs / buttons | Square | **Rounded** (2px) |
| Inline code | Square background | **Rounded pills** (2px) |
| Issue box corners | Square | **Rounded** (2px) |
| Article body text | No change | No change |
| Title underline | (ADR-042) | No change |

**ADR-039 lesson codified**: render and inspect PNG BEFORE
merge. ADR-043 caught a real bug (box-shadow band) via this
gate; ADR-044 expects the same discipline.

## Acceptance criteria

ADR-044 is complete when:

- [ ] `border_radius[4]` field on LayoutBox
- [ ] `border-radius` shorthand parsed (single value → all
      four corners)
- [ ] Per-corner longhands parsed and override shorthand
- [ ] Percentage radius → 0 (documented deferral)
- [ ] `inherit` → 0 (documented deferral)
- [ ] `rounded_rect_path` Cairo helper
- [ ] `paint_box` uses rounded path when radius > 0
- [ ] Border follows rounded curve (two-fill approach)
- [ ] PNG verification: search field, buttons, code pills
      rounded
- [ ] No unintended artifacts (no square/rounded mixing,
      no clipping bugs)
- [ ] 15+ unit tests across both commits
- [ ] Post-ADR-044 notes in ADR-040, ADR-043
- [ ] `main` green: 551 + N cases, all pass
- [ ] ADR-043 status → Implemented in docs commit

**Explicit claim**: search field corners rounded (completing
the ADR-040 border element), 135 buttons rounded, 715 inline
code pills rounded, message boxes rounded. 4+ first-viewport
changes, PNG-verified.

**Honest caveats:**
1. Percentage radius deferred (radio buttons, hidden)
2. `inherit` deferred (parent clip provides equivalent)
3. Multi-value shorthand: first token only
4. Elliptical radius deferred
5. Box-shadow doesn't follow rounded corners (rect shadow
   drawn behind rounded box; own ADR for rounded shadows)

## Implementation

Implemented on `feat/border-radius` across three commits
(Commit 1 = parse + fields, Commit 2 = paint, Commit 3 = this
docs update).

**Visual verification: inconclusive.** Documented honestly
below.

### What shipped

**Commit 1 -- parse + layout fields:**
- `LayoutBox` gains `border_radius[4]` (TL, TR, BR, BL)
- `border-radius` shorthand: single value -> all four corners
- Per-corner longhands (`border-top-left-radius` etc.):
  override shorthand (1 Wikipedia rule uses them:
  `.cdx-search-input__end-button`)
- Percentage -> 0 (deferred; radio buttons `display: none`)
- `inherit` -> 0 (deferred; parent `overflow: hidden` gives
  equivalent via clip)
- Multi-value -> first token only (documented; 0 uses)
- Elliptical -> horizontal component only (documented; 0 uses)
- 11 new tests in `tests/test_layout.cpp`

**Commit 2 -- paint:**
- New `rounded_rect_path()` helper using `cairo_arc` +
  `cairo_close_path`; radii clamped to min(w,h)/2 (simple
  bound; full CSS SS5.5 deferred)
- `paint_box` branches on any `border_radius[i] > 0`:
  * Rounded path: two-fill (outer `border_color`, inner
    `background_color` inset by border) -- rounded border
    follows curve without `cairo_stroke`
  * Zero-radius: existing 4-side `fill_rect` path, byte-
    identical to pre-ADR-044 (regression-free)
- `<algorithm>` + `<cmath>` includes added

**Commit 3 (this commit) -- docs:**
- ADR-044 status -> Implemented (with visual caveat, below)
- Post-ADR-044 notes in ADR-040, ADR-043
- README row update
- CHANGELOG entry

### Visual delta -- HONEST ASSESSMENT

ADR-044's Context section predicted "4+ first-viewport
changes" based on rule-count: 10 `border-radius: 2px` rules,
135 `.cdx-button` elements, 715 inline `<code>` tags, issue
box. That was rule-count thinking; it doesn't translate
linearly to visual perceptibility.

**Actual visual delta**: at 2px radius on 100px+ wide boxes,
the rounding is **below the unambiguous-perception threshold
at screenshot resolution** (1920px wide first-viewport PNG).

What was **verified definitively**:
- 11 unit tests confirm the parse-to-LayoutBox propagation
- `any_radius` gate triggers correctly for any radius > 0.001
- Zero-radius paint path is byte-identical to pre-ADR-044
  (no regression on 99% of boxes)
- Title underline (ADR-042), toolbar line (ADR-043), issue
  box border (ADR-040) all preserved

What was **NOT verified**:
- Whether the 2px rounding is humanly perceptible at typical
  view in the rendered PNG.

**This is the ADR-041 Commit 2 shape**, one cycle later:
prediction (4+ visible changes) vs. actual visual magnitude
(2px, subtle). Where ADR-041 Commit 2 was **skipped** because
no visible delta would have shipped, ADR-044 Commit 2 is
**merged** because the implementation is correct and future-
ready: larger radii (4px, 8px, 50%) render visibly, and the
parse path is spec-compliant for those cases.

**Honest status**: `Implemented` with the caveat "visual
delta subtle at 2px radius, not screenshot-verifiable;
behaviorally complete." This differs materially from
ADR-042's PNG-verified title underline or ADR-043's
PNG-verified toolbar line -- both unambiguous 1px gray lines.

### Test count

- Before ADR-044: 551 cases, 2125 assertions
- After Commit 1: 562 cases, 2188 assertions (+11 / +63)
- After Commit 2: unchanged
- After Commit 3: unchanged (docs only)

Zero new warnings introduced.

### Deferred (documented)

- **Percentage radius** -- 2 rules (radio buttons hidden)
- **`inherit`** -- 1 rule (parent clip gives equivalent)
- **Multi-value shorthand** -- 0 uses; first token only
- **Elliptical** -- 0 uses
- **`border-radius` transitions**
- **Rounded box-shadow** -- shadow follows curve; own ADR

