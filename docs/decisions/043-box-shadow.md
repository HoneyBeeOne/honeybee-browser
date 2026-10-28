# ADR-043: Box Shadow

**Status:** Accepted
**Date:** 2026-10-28
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-016 (inline formatting), ADR-032 (visibility/opacity culling), ADR-033 (position), ADR-034 (background-image), ADR-039 (overflow: hidden)

## Context

`box-shadow` is missing from our CSS engine. `parse_color`
(ADR-016) and `fill_rect` (paint) provide the primitives needed
to render simple shadows. This ADR adds them.

### Measurement (Wikipedia CSS, 2026-10-28)

**19 rules use `box-shadow`.** Breakdown:

| Type | Count | Renders statically? |
|---|---|---|
| `inset 0 0 0 1px <color>` focus rings (`:focus`) | **13** | ❌ Needs `:focus` state (ADR-018 is paint-only, no re-layout) |
| `inset 2.75em 0 0` code-line highlights | 2 | ❌ Inside `<pre>` blocks (second-viewport) |
| `box-shadow: none` no-ops | 2 | N/A |
| `.vector-dropdown-content { 0 2px 6px -1px rgba(...) }` | 1 | ❌ `visibility: hidden` (never painted closed) |
| **`.vector-page-toolbar-container { 0 1px #c8ccd1 }`** | **1** | ✅ **First-viewport, no blur, no inset** |

**Expected visible delta: exactly 1 line** — a 1px gray bar
1px below the `.vector-page-toolbar-container` ("Article | Talk
| Read | View source | View history" tab row).

### Honest scope note

**This is an infrastructure-first cycle.** ADR-041 Commit 1
shipped parse+cascade with 0 visible delta. ADR-043 ships
parse+layout+paint with 1 visible delta — one better, still
small. The value is:

1. Genuine infrastructure for future sites that use `box-shadow`
2. The 1px toolbar line IS a real visible change (measurable)
3. Unblocks future `border-radius + box-shadow` combined
   cycles (shadows on rounded boxes need both)

**Not claiming this is a "big delta" cycle.** It's a clean
small-delta cycle.

### What renders today

All 19 box-shadow rules currently produce nothing. The parser
drops the declaration (unknown property), layout has no field
for it, paint has no draw path.

### What will render after ADR-043

- `.vector-page-toolbar-container` → 1px gray line under the
  toolbar (visible in first viewport)
- Any future non-inset, non-blurred box-shadow rule on a static
  element → renders
- **Inset shadows** and **blurred shadows** → skipped (parse,
  no render) per deferral list below
- **Multi-shadow rules** (comma-separated) → skipped
- **`:focus` state rules** → skipped (state handling is
  paint-only, no re-layout in v1)

## Decision

Add `box-shadow` parsing, layout fields, and paint draw for
the non-inset, non-blurred case. All other forms parse
successfully (so they don't drop the whole declaration) but
render nothing — documented deferrals.

### Parse: `box-shadow` shorthand

Grammar (v1):

    box-shadow: [inset] <ox> <oy> [blur] [spread] <color>
    box-shadow: none

- `<ox>`, `<oy>`, `<blur>`, `<spread>`: lengths (via
  `parse_length`). `<ox>`, `<oy>` required. `<blur>`,
  `<spread>` optional, default `0`.
- `inset` keyword: recognized, stored, but not rendered
- `<color>`: hex / named / `rgb()` / `rgba()` / `currentColor`
  (via `parse_color`)
- `none`: clears all fields (matches CSS: `box-shadow: none`
  removes any inherited shadow)
- **Multi-shadow** (comma-separated list): v1 parses only the
  first shadow (deferred: full list); the rule's other shadows
  are lost silently. Documented.

Parse must handle the shorthand's ambiguous token order:
2-5 space-separated tokens before the color. Approach:

1. Split on whitespace
2. Detect `inset` keyword → set flag, remove token
3. Remaining tokens: first 2 = `<ox>`, `<oy>` (required)
4. Next tokens: 0-2 additional numeric lengths → `<blur>`,
   `<spread>` in order
5. Remaining token (exactly 1): `<color>`
6. On parse failure: drop the whole declaration (matches
   `border` / `flex` / `grid-template` behavior)

### LayoutBox fields

    float box_shadow_ox = 0.0f;
    float box_shadow_oy = 0.0f;
    float box_shadow_blur = 0.0f;
    float box_shadow_spread = 0.0f;
    Color box_shadow_color = Color::transparent();
    bool  box_shadow_inset = false;
    bool  box_shadow_has = false;

Set from ComputedStyle during layout_element's early property
pass (alongside `background-color`, `border-color`).

`box_shadow_has` gates paint. `box_shadow_inset` and
`box_shadow_blur > 0` are checked separately by paint; when
either is true, paint skips the shadow (documented v1
deferral).

### Paint: draw shadow behind box

`paint_box` draws the shadow rectangle **before** the background
fill:

    if (box.box_shadow_has
        && !box.box_shadow_inset
        && box.box_shadow_blur <= 0.0001f) {
        const float sx = box.x + box.box_shadow_ox - box.box_shadow_spread;
        const float sy = box.y + box.box_shadow_oy - box.box_shadow_spread;
        const float sw = box.width  + 2.0f * box.box_shadow_spread;
        const float sh = box.height + 2.0f * box.box_shadow_spread;
        fill_rect(cr, box.box_shadow_color, sx, sy, sw, sh);
    }

- No blur: hard-edged rect at offset (matches the toolbar
  case where blur=0)
- Spread expands the rect on all sides (negative shrinks)
- Drawn before background → background covers any overlap with
  the box
- Sibling boxes drawn later may cover the shadow (matches CSS:
  z-order decides)

`fill_rect` already handles alpha (`cairo_set_source_rgba`);
`rgba(0,0,0,0.2)` renders with 20% opacity correctly.

### Behaviour for `box-shadow: none`

Parse `none` → set `box_shadow_has = false`. Useful for
elements that override an ancestor's shadow (though CSS shadow
is non-inherited, so this only matters when a rule sets
`box-shadow: <values>` and a later rule sets
`box-shadow: none` — the cascade produces the final value,
which layout reads. `none` → clear fields.)

### Deferred (v1 scope)

- **`inset` render**: parsed, not painted. All 13
  Wikipedia inset rules are `:focus` state; needs state-driven
  re-layout (ADR-018's state is paint-only). Own ADR if
  needed.
- **`blur > 0` render**: needs Gaussian approximation (3-5
  translucent rings, or Cairo `cairo_paint_with_alpha` with
  clip masks). The dropdown rule uses blur=6 but is
  `visibility: hidden` — zero visible loss.
- **Multi-shadow** (comma-separated): v1 takes only the first.
  3 Wikipedia rules use 2-shadow lists, all `:focus` state.
- **`box-shadow` transitions**: needs animation system.
- **Percentages** in offsets/blur/spread: not in Wikipedia CSS.
- **`box-shadow` on inline elements**: not exercised.

## Design decisions

### Why parse-but-skip for inset and blur

Parsing succeeds → the whole declaration enters the cascade.
Layout sets fields. Paint makes the render decision. This
preserves the chain for future implementation: adding
inset render later is a paint-only change (all the parse +
layout already works).

**Alternative** (reject inset / blur rules at parse): simpler
v1, but requires re-parsing when they later become supported.
Parsing handles the shape; render defers the visual.

### Why 1px toolbar line is worth shipping

The alternative (deferring box-shadow entirely) loses:
1. The 1px line (small but real)
2. Infrastructure that future sites will exercise (many sites
   use simple drop-shadows on cards, modals, buttons —
   non-inset, sometimes no blur)
3. Foundation for `border-radius + box-shadow` combined cycle
   where the shadow follows the curve

The cost is ~150-200 lines + tests. Low risk, small real
delta, real infrastructure.

### Why paint draws shadow before background

CSS spec: box-shadow renders **behind** the element's own
background. Element's border/background covers the shadow
where they overlap. Sibling z-order still applies (later
siblings paint on top). Our paint_box naturally does
background-first, so drawing shadow before background matches
the spec's stacking.

### Why spread expands rect symmetrically

CSS spec: `spread` extends the shadow box outward on all four
sides by `<spread>`. Positive spread makes the shadow larger
than the box; negative shrinks it. Simple additive expansion:

    shadow_rect = (box.x + ox - spread,
                   box.y + oy - spread,
                   box.w + 2*spread,
                   box.h + 2*spread)

For the toolbar rule (spread=0), this is just `(ox, oy)` offset.

## Deferred

See "Deferred (v1 scope)" above. Summary:

- Inset render (13 rules, all :focus state)
- Blur > 0 render (1 rule, hidden element)
- Multi-shadow (3 rules, all :focus state)
- Transitions
- Percentages
- Inline-element shadows

## Consequences

**Better:**
- 1px toolbar line appears (PNG-verified delta)
- Box-shadow infrastructure ready for future sites
- Foundation for `border-radius + box-shadow` combined cycle
- Parse handles all forms (inset, blur, multi) so future
  render-only changes don't need re-parsing

**Accepted limitations:**
- Only 1 visible delta on target page (13 inset rules blocked
  by :focus state; 1 blurred rule blocked by visibility:hidden)
- Inset shadows render as nothing (deferral)
- Blurred shadows render as nothing (deferral)
- Multi-shadow only first shadow parsed

**Unchanged:**
- ADR-032 visibility/opacity culling preserved
- ADR-034 background-image (drawn after background fill, after
  shadow — matches z-order)
- ADR-039 overflow:hidden clipping (clips children + borders,
  shadow drawn before clipping — shadow extends outside the
  box so it isn't affected by clip_children)

**Possible regression risks (to verify in Commit 2):**
- Shadow drawn before background: if background has alpha
  (e.g. `background-color: rgba(0,0,0,0.5)`), the shadow
  blends through. Real CSS behaves the same way, so this is
  correct, but worth noting.
- Shadow drawn even when box is `visibility: hidden`? No —
  `visible` check short-circuits `paint_box` entirely, so
  neither shadow nor box renders. Correct.
- Shadow outside parent's `clip_children` bounds gets clipped
  by ADR-039's Cairo clip (correct per CSS: `overflow: hidden`
  clips box-shadow of children; own box-shadow is NOT clipped
  by own overflow). Wait — our paint draws the shadow before
  `clip_children` is applied, so the shadow is inside the
  pre-clip pass. Real CSS: shadow of a box is not clipped by
  its own overflow (it's outside the content box). So
  `box.clip_children` should not affect its own shadow. Our
  sequence: shadow draw → background → border → clip_children
  applied → children. Shadow escapes clip. Correct.

## Implementation plan

Three commits, same discipline as ADR-041 / ADR-042:

1. **Parse + layout fields** (behavior-neutral):
   - `LayoutBox` gains 7 box-shadow fields
   - Parse `box-shadow` shorthand in `layout_element`
   - `box_shadow_has` gates everything
   - `inset` / `blur` / multi-shadow parsed and stored (not
     rendered)
   - Zero paint changes: no visible delta yet
   - ~15 tests (parse cases)

2. **Paint** (visible):
   - `paint_box` draws shadow if `box_shadow_has` &&
     `!inset` && `blur <= 0`
   - PNG verify toolbar line appears
   - No unintended shadow artifacts (verify no other rule
     produces shadow in first viewport)
   - ~3 tests (paint-path behavior doesn't have a natural
     unit-test hook; verify via layout-side assertions)

3. **Docs**:
   - ADR-043 → Implemented
   - Post-ADR-043 note in ADR-034 (background-image + shadow
     draw order)
   - README + CHANGELOG

## Verification

**Unit tests (Commit 1):**

Parse tests (`[css][stylesheet][adr-043]`):
- `box-shadow: 0 1px #c8ccd1` → ox=0, oy=1, blur=0, spread=0,
  color=#c8ccd1, inset=false, has=true
- `box-shadow: 2px 3px 4px 5px red` → ox=2, oy=3, blur=4,
  spread=5, color=red
- `box-shadow: 0 0 5px red` → ox=0, oy=0, blur=5, spread=0
- `box-shadow: inset 0 0 0 1px #36c` → inset=true, ox=0, oy=0,
  blur=0, spread=1, color=#36c
- `box-shadow: 0 2px 6px -1px rgba(0,0,0,0.2)` → spread=-1,
  color has alpha=0.2
- `box-shadow: none` → has=false
- `box-shadow: 0 1px red, 0 2px blue` → first shadow parsed,
  second dropped (multi-shadow deferral)
- `box-shadow: 1px` → parse fails (needs at least ox + oy)
- `box-shadow: ` (empty) → parse fails
- `box-shadow: 0 1px solid` → parse fails (no color, `solid`
  not a length)

Layout tests (`[layout][box-shadow][adr-043]`):
- `<div class='p'>` with `box-shadow: 0 1px red` →
  LayoutBox has `box_shadow_has=true`, `box_shadow_ox=0`,
  `box_shadow_oy=1`, `box_shadow_color` matches red
- `box-shadow: none` rule → `box_shadow_has=false`
- No box-shadow rule → `box_shadow_has=false`
- `box-shadow: inset 0 0 0 1px red` → `box_shadow_inset=true`
  (parsed; render skipped)

**Visual verification (Commit 2, PNG before merge):**

| Check | Before | After |
|---|---|---|
| Toolbar "Article / Talk / Read" bottom | (verify — may already have border) | 1px gray line below |
| Issue box | No change | No change |
| Article body | No change | No change |
| No unintended shadows | — | ✅ |

**Critical pre-check in Commit 2**: does
`.vector-page-toolbar-container` already have a visible border?
If so, the shadow may be invisible (drawn 1px below an existing
line at same offset). If invisible → delta is 0, this cycle
ships infrastructure-only. Honest accounting needed either way.

## Acceptance criteria

ADR-043 is complete when:

- [ ] 7 box-shadow fields on LayoutBox
- [ ] `box-shadow` shorthand parsed (1-5 tokens + color,
      optional inset)
- [ ] `box-shadow: none` clears fields
- [ ] Multi-shadow → first only (deferred note in ADR)
- [ ] Malformed drops rule
- [ ] Paint draws non-inset, non-blurred shadows before
      background
- [ ] PNG verification: toolbar line appears (or honest
      "0 visible" accounting if already bordered)
- [ ] No unintended shadows in first viewport
- [ ] 15+ unit tests
- [ ] Post-ADR-043 note in ADR-034
- [ ] `main` green: 539 + N cases, all pass
- [ ] ADR-043 status → Implemented

**Honest claim**: 1px gray line under the toolbar, if not
already present via border. If already present, 0 visible
delta but full box-shadow infrastructure ready for future
sites and combined border-radius cycle.
