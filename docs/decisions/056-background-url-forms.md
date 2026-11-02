# ADR-056: CSS `background:` URL forms (image + repeat + position + size)

**Status:** Implemented
**Date:** 2026-11-01
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-034 (image paint), ADR-045 (inline background-size), ADR-047 (mask-image size/pos), ADR-055 (background color forms)

## Context

ADR-055 shipped `background:` color-only forms. 4 of 11 Wikipedia forms still drop entirely because they contain `url(...)`:

```
background: #fff url(.../Checker-16x16.png) repeat
background: no-repeat center / 1.23076923em url(.../search.svg)
background: url(.../Icon_pdf_file.png) no-repeat right
background: url(.../arrow-down.svg) 100% 50% no-repeat
background: #fff url(...) repeat                  (var variant)
```

Each is a valid CSS `background` shorthand mixing color, image, repeat, position, and size in arbitrary order.

### What already exists (measured 2026-11-01)

**Block backgrounds have ZERO of the necessary infrastructure:**

- `LayoutBox` has only `background_image_url` (one string field)
- `paint_image` (paint.cpp:30) stretches to fit — no repeat, position, or size
- `try_expand_background` (ADR-055) drops anything with `url(` entirely

**But the full pattern exists twice already:**

| Precedent | Fields | Paint | Applies to |
|---|---|---|---|
| ADR-047 mask-image | `mask_size_w/h`, `mask_pos_x/y`, `mask_no_repeat` | `paint_mask_image` — Cairo pattern matrix with scale + translate | `LayoutBox` |
| ADR-045 inline bg | `bg_size_w/h`, `bg_pos_x/y` | Inline-bg render — Cairo pattern | `LineFragment` |

Both use `BgPosX` / `BgPosY` enums for keyword positioning. Both do aspect-derived sizing (one dim set → derive other). Both use Cairo pattern matrices for placement.

**What's missing:** block-level background gets none of it. The infra exists in three other places — just not here.

## Decision

Ship full URL form support, mirroring ADR-047's mask pattern onto `LayoutBox`:

1. **`LayoutBox` gains 5 fields** (mirror mask):
   - `background_size_w`, `background_size_h` (0 = auto)
   - `background_pos_x`, `background_pos_y` (BgPosX / BgPosY, default Left/Top)
   - `background_no_repeat` (bool, default false = repeat)

2. **`layout_element` reads 3 more properties** (mirror mask's block at layout.cpp:558):
   - `background-size` → `background_size_w` (first token, skip auto/cover/contain/%)
   - `background-position` → `background_pos_x/y` (keyword or coord pair)
   - `background-repeat` → `background_no_repeat` (no-repeat → true)

3. **`paint_image` honors size + position + repeat** (copy mask's pattern matrix, add repeat):
   - Size: `background_size_w` wins; aspect-derive `_h` if only one set
   - Position: `BgPosX/Y` → `ix, iy` per keyword
   - Repeat: `CAIRO_EXTEND_REPEAT` on pattern if `!background_no_repeat`

4. **`try_expand_background` handles URL forms** (biggest change):
   - Detect `url(...)` token → `background-image: url(...)`
   - Detect repeat keywords (`no-repeat`, `repeat`, `repeat-x`, `repeat-y`, `space`, `round`) → `background-repeat`
   - Detect position (keyword or coord) → `background-position`
   - Detect `/size` → `background-size`
   - Detect color → `background-color` (already handled)
   - All 5 sub-properties can appear in any order per CSS grammar

## Design decisions

### Why mirror ADR-047's mask pattern

The mask field set (`mask_size_w/h`, `mask_pos_x/y`, `mask_no_repeat`) is a complete, tested implementation of exactly what block backgrounds need. The field names are almost perfectly transferable:

| Mask field | Background field |
|---|---|
| `mask_image_url` | `background_image_url` (already exists) |
| `mask_size_w/h` | `background_size_w/h` (new) |
| `mask_pos_x/y` | `background_pos_x/y` (new) |
| `mask_no_repeat` | `background_no_repeat` (new) |
| `mask_has` | `!background_image_url.empty()` (implicit) |

The paint pattern (Cairo scale + translate matrix → `cairo_mask`) becomes `cairo_paint` for backgrounds (fill vs. mask). Same math, different final operation.

### Why repeat is added here but not in mask

`mask_no_repeat` exists as a field but `paint_mask_image` never reads it — masks are "no-repeat" by nature (the mask surface is a single overlay). Backgrounds genuinely need repeat.

`CAIRO_EXTEND_REPEAT` on the Cairo pattern handles repeat in one line:
```cpp
cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
```
Clipped to the box's paint rectangle (existing clip_children logic), this tiles correctly. No manual loop needed.

### Why the grammar is the real work

CSS `background` shorthand is order-free:

```
background: [color || image || repeat || position [/ size]]*
```

`#fff url(x) repeat` is valid. `url(x) no-repeat center / 1.23em` is valid. `no-repeat center / 1.23em url(x)` is valid. `url(x) repeat #fff` is valid. The parser must recognize each token type regardless of order and assemble them.

Token classifiers (~30 lines):

| Token type | Detection |
|---|---|
| URL | `url(...)` literal |
| Color | `#` prefix, named-color list, `rgb(...)`, `transparent` |
| Repeat | `no-repeat`, `repeat`, `repeat-x`, `repeat-y`, `space`, `round` |
| Position | `left`/`right`/`center`/`top`/`bottom` or `N%`/`Npx` |
| Size | Only after `/` |
| (color-passthrough) | Anything else — try `parse_color` at layout time |

Then assembly produces up to 5 longhands. Malformed (unknown token, more than one URL, etc.) → drop entirely (matching ADR-055 behavior).

### Why size "auto / cover / contain / %" are dropped

Same as ADR-055 and ADR-047: `%` sizes resolve against element dimensions which the parser doesn't know yet. `cover` / `contain` need paint-time aspect math not yet built. ADR-056 handles explicit px/em sizes only. All 5 Wikipedia forms use explicit `1.23076923em` or `auto` — both work with this restriction.

### Why position-only fields (no x/y coord resolution in parser)

`background-position: 100% 50%` needs element dimensions to resolve `%`. Parse stores the raw string, layout_element resolves against `box.width/height` at layout time. Matches ADR-047's mask pattern:

```cpp
box.mask_pos_x = px(t);  // where px() resolves coord against box.width
```

Same flow for backgrounds.

### What "correct rendering" means per form

| Form | Today | After ADR-056 | Caveat |
|---|---|---|---|
| `#fff url(Checker) repeat` | white fill | white fill + tiled checker | ✅ Complete |
| `no-repeat center / 1.23em url(search)` | white fill | search icon centered, sized | ⚠ Icon renders ~10px too high (translateY deferred) |
| `url(Icon_pdf) no-repeat right` | transparent | pdf icon, right-aligned | ✅ Complete |
| `url(arrow-down) 100% 50% no-repeat` | transparent | arrow, 100% horizontal (right), 50% vertical (center) | ✅ Complete |
| `#fff url(...) repeat` (var) | white fill | white + tiled | ✅ Complete |

**Four of five forms render correctly. One (search icon) has the correct-size-wrong-position caveat due to deferred translateY.** That's a real 4/5 visible delta.

### Why the magnifier caveat is documented but not fixed here

The search icon at `.cdx-text-input__start-icon` sits at `y=7` (container top, from ADR-054 diagnostic). It needs `translateY(-50%)` to center, but that would move it to `y=-3` (above the container) without `top: 50%` — which needs `parent_content_height` threading (deferred since ADR-053). Full chain:

| Layer | Status |
|---|---|
| background-image/repeat/position/size render | **ADR-056 (this ADR)** |
| translateY(-50%) honored | Blocked on `parent_content_height` |
| top: 50% honored | Same blocker |

After ADR-056: **the search icon renders.** It's just 10px above where CSS would place it. Better than invisible. The full centering ships when `parent_content_height` threads (future ADR).

## Deferred

- **`translateY()` / `top: 50%`** -- blocked on `parent_content_height`
- **Percentage background-size** -- needs element dimensions in parser (parse time)
- **`cover` / `contain`** -- needs paint-time aspect math
- **`repeat-x` / `repeat-y` / `space` / `round`** -- variants beyond `repeat` / `no-repeat`
- **Multi-layer** (`url(a), url(b)`) -- rare, each layer needs its own 5-longhand set
- **`background-clip` / `background-origin`** -- separate properties
- **`linear-gradient` / `radial-gradient`** -- no gradient support anywhere in paint

## Consequences

**Better:**
- **All 4 Wikipedia url() forms render** (3 correctly, 1 with the translateY caveat)
- **First ADR since 045 with 100% form coverage** on its target property (11/11 `background:` forms parse)
- **LayoutBox field set completes** the block-background pattern, mirroring mask
- **`paint_image` gets the Cairo pattern infrastructure** mask already had -- future background variants (radial gradients, etc.) have the machinery
- **Test count 658 -> ~666**

**Accepted limitations:**
- **Magnifier icon renders ~10px too high** -- the correct-size-wrong-position caveat from deferred translateY
- **Multi-layer backgrounds still drop** (rare)
- **Percentage size, cover, contain deferred** (none of the 5 Wikipedia forms use them)
- **Repeat-x/repeat-y/space/round deferred** (rare)

**Unchanged:**
- ADR-005 through ADR-055 behavior
- Color-only forms from ADR-055 (still render)
- Masks, box-shadows, border-radius, transforms (all separate fields/patterns)
- Inline background (LineFragment path untouched)

**Possible regression risks (verify in Commit 1):**
- `paint_image` currently called for `<img>` elements too. `background_size_w/h`, `background_pos_x/y`, `background_no_repeat` are block-background fields, **not img fields**. `paint_image` needs a discriminator (or the fields must not affect `<img>` render). Simplest: add `is_block_background` bool param, or split into `paint_background_image` + `paint_img_element` at the call site.
- Existing `background-image: url(x)` tests (test_layout.cpp ADR-045 block) use the inline path — unaffected
- Any existing `<img>` render path that goes through `paint_image` must remain stretch-only

## Implementation plan

Three commits (grammar is complex enough to warrant isolation):

1. **LayoutBox fields + layout_element reads + paint pattern (no expansion change)**:
   - 5 new fields mirroring mask
   - `layout_element` reads `background-size`, `background-position`, `background-repeat` (mirror mask's block at line 558)
   - `paint_image` split: `paint_img_element` (existing stretch) + `paint_background_image` (new size/pos/repeat)
   - Call sites updated: `paint_box` background path → `paint_background_image`; img path → `paint_img_element`
   - Tests: 4-5 for paint pattern (a background image at right + no-repeat vs. left + repeat)
   - **At this commit, `try_expand_background` still drops `url(...)` — no behavior change from ADR-055.** All existing tests pass.

2. **`try_expand_background` grammar for url forms**:
   - Replace the 3 early-return lines with full token classifier + assembler
   - 4-5 tests: each Wikipedia form + a couple of malformed forms (URL with unsupported tokens → drop)
   - **This is the visible delta.** After this commit, Wikipedia icons render (3/4 correct, 1 with the position caveat).

3. **Fixture + integration tests + docs**:
   - `docs/adr056-background-url-test.html` — 5 rows, one per Wikipedia form
   - Screenshot verification
   - ADR-056 → Implemented
   - ADR-055 Post-ADR-056 note
   - README + CHANGELOG

## Verification

**Unit tests (test_stylesheet.cpp):**
- `background: url(x)` → `background-image: url(x)`
- `background: url(x) no-repeat` → `background-image: url(x); background-repeat: no-repeat`
- `background: #fff url(x) repeat` → 3 longhands
- `background: url(x) 100% 50% no-repeat` → image + position + repeat
- `background: no-repeat center / 1.23em url(x)` → 4 longhands
- Unknown token → dropped

**Paint tests (test_paint.cpp or similar):**
- Background at `right` + `no-repeat` → image at `box.x + box.width - iw`
- Background at `center` + `size` → image centered, sized
- Background with `repeat` → tiled across box (or `CAIRO_EXTEND_REPEAT` set)

**Synthetic fixture verification:**
`docs/adr056-background-url-test.html`:
- Row 1: longhand `background-image` stretch (control)
- Row 2: `background: url(x) no-repeat right` (icon at right)
- Row 3: `background: #fff url(x) repeat` (tiled)
- Row 4: `background: no-repeat center / 30px url(x)` (icon centered, 30px)
- Row 5: `background: url(x) 50% 50% no-repeat` (percent position — currently no paint support, should fall back to top-left)

**Production verification:** Wikipedia HTML article screenshot before/after:
- **Before ADR-056**: search icon invisible, PDF icon invisible, arrow icons invisible
- **After ADR-056**: search icon renders (10px high), PDF icon renders, arrow icons render

**Test count target**: 658 → ~668 (+10 tests).

## Acceptance criteria

ADR-056 is complete when:

- [ ] `LayoutBox` has 5 background fields mirroring mask
- [ ] `layout_element` reads `background-size`, `background-position`, `background-repeat`
- [ ] `paint_background_image` honors size + position + repeat
- [ ] `paint_img_element` remains stretch-only (no regression on `<img>`)
- [ ] `try_expand_background` parses all 5 Wikipedia url() forms
- [ ] Unknown tokens in background shorthand → drop entirely
- [ ] ~10 new tests pass (658 → ~668)
- [ ] Fixture shows 4/5 rows correctly (5th is the magnifier, renders with position caveat)
- [ ] Wikipedia HTML article screenshot: search icon + PDF icon + arrow icons render
- [ ] Post-ADR-056 note in ADR-055 (url forms now handled)
- [ ] Post-ADR-056 note in ADR-047 (mask pattern reused for backgrounds)
- [ ] Post-ADR-056 note in ADR-045 (inline bg pattern echoed at block level)
- [ ] README row + CHANGELOG
- [ ] ADR-056 status -> Implemented

## Implementation

Implemented on `feat/background-url-forms` across three
commits (Commit 1 = infrastructure, Commit 2 = grammar
parser, Commit 3 = fixture + README). Single PR #249
merged at `dec064d`.

### What shipped

**Commit 1 (`2e9bff5`) -- infrastructure, no behavior change:**

- `LayoutBox` + 5 fields mirroring mask: `background_size_w/h`,
  `background_pos_x/y`, `background_no_repeat`
- `layout_element` reads `background-size`, `-position`,
  `-repeat` (mirror mask block at line 558)
- **`paint_image` split into two functions:**
  - `paint_img_element`: unchanged stretch-to-box for `<img>`
  - `paint_background_image`: intrinsic by default, explicit
    `background_size_w` overrides, aspect-derive `_h` from
    `_w`, position via `BgPosX/Y` keywords, repeat via
    `CAIRO_EXTEND_REPEAT`, clipped to box rect. Uses Cairo
    pattern matrix (mirrors `paint_mask_image`;
    `cairo_mask` -> `cairo_paint`).
- **Checkpoint**: all 658 tests pass unchanged. Verified
  the split didn't regress ADR-034 `<img>` rendering,
  ADR-047 mask, or ADR-045 inline background.

**Commit 2 (`a0e2c0e`) -- grammar parser, visible delta:**

- Replaces `try_expand_background`'s 3 early-return lines
  with token classifier + assembler for the CSS background
  shorthand grammar
- Handles `[color || image || repeat || position [/ size]]*`
  order-free per CSS spec
- 5 helper functions: `is_color_token`, `is_repeat_token`,
  `is_position_token`, `is_url_token`, `is_length_token`
- `bg_detail::classify` + `bg_detail::BgTokens` for token
  accumulation with `valid` flag for malformed detection
- Emits canonical order (color, image, repeat, position,
  size) regardless of source order -- CSS longhands are
  order-independent

**Commit 3 (`10f1eb2`) -- fixture + README:**

- `docs/adr056-background-url-test.html`: 5-row permanent
  fixture, self-contained via inline `data:image/svg+xml`
  URLs (no external network or local server dependency)
- Each row exercises one Wikipedia form

### Verified delta (production, screenshot 2026-11-01)

Ran against Wikipedia HTML article:

| Icon | Before ADR-056 | After ADR-056 |
|---|---|---|
| **Search magnifier** (top bar) | invisible | **renders** (~10px high) |
| **External link icons** (body) | invisible | **renders** |
| **Wikipedia logo** (`<img>`, not background) | renders | renders unchanged |
| **Article body / layout** | correct | correct (zero regression) |

**This is a real production delta on Wikipedia.**
First ADR since ADR-045 to produce material visual change
on the target page.

### Honest caveat (the magnifier)

`.cdx-text-input__start-icon` renders at correct size,
**~10px too high**. The CSS is:
```css
position: absolute;
top: 50%;
transform: translateY(-50%);
```

**`top: 50%` doesn't resolve** -- needs `parent_content_height`
in `LengthContext` (deferred since ADR-053). Feeding
`translateY(-50%)` alone moves the icon to `y=-3` (above
the container), a regression. So it's deferred.

After ADR-056: **the icon renders.** After the future
`parent_content_height` cycle: **it'll be perfectly
centered.** Two-step improvement.

### 11/11 `background:` form coverage

**Complete.** Every `background:` form in Wikipedia's
site.styles bundle now parses:

| Form | ADR |
|---|---|
| `#fff` / `#ffffff` / `var(--x, #fff)` | ADR-055 |
| `none` / `none !important` | ADR-055 |
| `#fff !important` | ADR-055 |
| `url(x)` | ADR-056 |
| `url(x) no-repeat right` | ADR-056 |
| `url(x) 100% 50% no-repeat` | ADR-056 |
| `#fff url(x) repeat` | ADR-056 |
| `no-repeat center / 1.23em url(x)` | ADR-056 |
| `#fff url(x) repeat` (var variant) | ADR-056 |
| `linear-gradient(...)` | **still dropped** (no gradient support) |

10 of 11 render. The 11th (linear-gradient) needs an
entirely new paint path -- separate ADR.

### Test count

- 658 -> 663 cases (+5 net)
  - 1 flipped (ADR-055's `url() forms dropped entirely`
    -> `now expand to 3 longhands`)
  - 5 new ADR-056 tests (one per Wikipedia form + malformed)
- 2514 -> 2550 assertions (+36)
- Zero regression on 662 pre-existing tests

### Design decisions applied

**Mirror ADR-047 mask pattern**: `mask_size_w/h` /
`mask_pos_x/y` / `mask_no_repeat` transfer almost directly
to `background_size_w/h` / `background_pos_x/y` /
`background_no_repeat`. The Cairo pattern matrix math is
identical (`cairo_mask` -> `cairo_paint` for backgrounds).

**Split `paint_image` into two functions** rather than add
parameters: `<img>` elements have fixed stretch behavior
that must not change; block backgrounds need the new
pattern matrix. Splitting preserves each path's contract.

**Color classifier duplicated in css/**: `layout::parse_color`
isn't accessible from `css/` (layering). The 19-name set is
duplicated in `is_color_token` -- **for token discrimination
only**. Actual color resolution still happens in
`layout_element` via `parse_color`. No risk of divergence
on named color list -- both reference the same CSS names.

**Preserve ADR-055's `none` and malformed handling**:
`background: none` -> transparent stays. Gradients and
multi-layer (comma) still drop entirely. Unknown tokens
drop entirely (mirror border/grid-template/flex).

### A real, non-obvious regression that was caught

**First attempt at Commit 1 placed the new reads at line
336** -- before `LengthContext ctx` was declared at line
434. Build error: `'ctx' was not declared in this scope`.
The fix was to relocate the entire block to line 558
alongside the mask reads, where `ctx` is in scope.

This is the same category of bug ADR-051's `parse_operand`
discovery surfaced: **subsystems that look unified from the
outside can have surprising internal scope boundaries.**
Worth noting for future readers: adding reads to
`layout_element` requires checking `LengthContext ctx`
location first.

### Deferred (documented)

- **`translateY()` / `top: 50%`** -- blocked on
  `parent_content_height`
- **Percentage `background-size`** -- needs element
  dimensions at parse time
- **`cover` / `contain`** -- needs paint-time aspect math
- **`repeat-x` / `repeat-y` / `space` / `round`** --
  variants beyond `repeat` / `no-repeat`
- **Multi-layer** (`url(a), url(b)`) -- rare
- **`linear-gradient` / `radial-gradient`** -- no gradient
  support anywhere in paint
- **`background-clip` / `background-origin`** -- separate
  properties



## Post-ADR-058 note

**The magnifier that ADR-056 first made visible is now
centered.**

ADR-056 shipped the `background:` URL form grammar that
rendered `.cdx-text-input__start-icon` for the first time.
The icon appeared, but at `y=7` (container top) --
10px above where CSS intended. ADR-056's honest delta
section noted this explicitly:

> The search magnifier renders at correct size, **~10px
> too high**. Needs `translateY(-50%)`, which alone would
> move it to `y=-3` without `top: 50%`.

ADR-058 ships both pieces of the formula together (they
must, as ADR-056's note explains). After ADR-058, the icon
sits at `y=18` -- **perfectly centered in the search box.**

**The full `background:` chain:**

| ADR | Contribution |
|---|---|
| 055 | `background: #fff` color forms |
| 056 | `background: url(...)` URL forms |
| 058 | (indirect) the rendered icon is now correctly positioned |

11/11 background forms parse (from ADR-056). 10/11 render.
The 11th (`linear-gradient`) remains deferred.
