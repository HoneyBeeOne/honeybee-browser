# ADR-056: CSS `background:` URL forms (image + repeat + position + size)

**Status:** Accepted
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
