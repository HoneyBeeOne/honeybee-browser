# ADR-036: Attribute Selectors + `clear` Property

**Status:** Accepted
**Date:** 2026-10-27
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-031 (float v1), ADR-034 (`<img>` paint), ADR-035 (image layout integration)

## Context

ADR-035's Implementation section recorded an honest finding:
below-fold thumbnails have real heights in the tree
(`<img>` 250x250 inside a container), but the **containers
don't float right**. Tree dump post-ADR-035:

    FOUND IMG as Box x=324 y=1297 w=926 h=0  (JPEG, fails to decode)
    FOUND IMG as Box x=346 y=6481 w=250 h=250  (HTML5 logo — left-aligned!)
    FOUND IMG as Box x=346 y=14924 w=250 h=177  (categories diagram)

The `x=346` means the `<img>` sits at the content-left of
some container, not at the right edge of a right-float. That's
a **cascade coverage gap**, not a float layout bug.

### Measurement (2026-10-27, Wikipedia CSS + HTML)

**Wikipedia's thumbnail rule** (13 matching selectors, all
identical shape):

```css
figure[typeof~='mw:File/Thumb'],
figure[typeof~='mw:File/Frame'] {
    display: table;
    text-align: center;
    border-collapse: collapse;
    line-height: 0;
    margin: 0.5em 0 1.3em 1.4em;
    clear: right;
    float: right;
}
```

Two things prevent it from matching:

1. **`[typeof~='mw:File/Thumb']`** is a word-token attribute
   selector. Our cascade likely doesn't support `[attr~=val]`
   syntax (verified in Commit 1's diagnostic).
2. Even if it matched, **`clear: right`** isn't parsed by our
   layout (confirmed: `grep '"clear"'` in `src/renderer/css/`
   and `src/renderer/layout/` returns empty).

**`clear` distribution in Wikipedia CSS** (20 total):

| Value | Count |
|---|---|
| `clear: both` | 7 |
| `clear: right` | 5 |
| `clear: none` | 4 |
| `clear: left` | 3 |
| `clear: right !important` | 1 |

Full scope worth supporting.

**Article body container** (`flow-root` / `overflow: hidden`):
no matching rules detected. Either absent or in `@media` we
don't parse. **Parent height still won't include float
heights** — documented v1 limitation.

**Honest timing correction to ADR-035**: measurement now
shows **6.3 sec** render (was 5.0-5.5 sec pre-ADR-035).
ADR-035's spec predicted 5.3-5.8 sec; actual is 6.3 sec —
**1 second added**, 2-3x the predicted cost. Real cost of 8
sync fetches at layout time on cold cache. Documented in
this ADR and corrected in a Post-ADR-035 note.

### What this fixes

After both cascade + layout fixes:

- `figure[typeof~='mw:File/Thumb']` matches → `float: right`
  applies → figure positions at right edge of article body
- `clear: right` on float → consecutive floats stack vertically
  instead of overlapping at right edge
- `clear: right` on following blocks (if any) → those blocks
  jump below the float
- Thumbnail `<img>` inside the float stays at 250×250 (from
  ADR-035) — the figure containers it correctly

### What this ADR does NOT fix

- **Article body height still doesn't include float heights** —
  needs `display: flow-root` or `overflow: hidden` on the
  article container (ADR-037 if needed)
- **Navbox overlap at x=324** (separate ADR-037/own-follow-up)
- **JPEG decode** (needs `libjpeg-turbo`, deferred since
  ADR-034)
- **First-viewport appearance** — thumbnails are below fold.
  This ADR's visible delta is **below-fold only**.

## Decision

Two coupled fixes shipped together.

### 1. Cascade: attribute selector support

Extend the selector parser and matcher to handle all seven
attribute selector forms:

| Form | Matches |
|---|---|
| `[attr]` | Element has `attr` at all |
| `[attr=val]` | `attr` equals `val` exactly |
| `[attr~=val]` | `attr` is space-separated list containing `val` |
| `[attr\|=val]` | `attr` is `val` or starts with `val-` |
| `[attr^=val]` | `attr` starts with `val` |
| `[attr$=val]` | `attr` ends with `val` |
| `[attr*=val]` | `attr` contains `val` |

**Selectors AST** currently represents compound selectors as
strings + kind. Attribute selectors need a structured
representation:

```cpp
struct AttributeSelector {
    enum class Op { Exists, Equals, Includes, DashMatch,
                    Prefix, Suffix, Substring };
    std::string name;
    std::string value;  // unused for Exists
    Op op;
};
```

Compound selector gains `std::vector<AttributeSelector>
attributes;`. Match logic checks all attributes against the
element's `get_attribute(name)`.

**Files touched**: `selector.hpp` (AST), `selector.cpp`
(parse + match).

### 2. Layout: `clear` property support

Read `clear` from `ComputedStyle::get("clear")` during
`layout_element`. Two behaviors:

**A. Clear on a float** — forces the float below all previous
same-direction (or both) floats. In `layout_block_children`'s
float branch:

```cpp
if (float_dir == "left" || float_dir == "right") {
    // ADR-036: clear on a float forces it below prior
    // same-direction (or both-direction) floats.
    std::string clear_dir = "none";
    if (child_style) {
        if (auto c = child_style->get("clear")) clear_dir = *c;
    }
    const bool must_jump =
        (clear_dir == "both") ||
        (clear_dir == "right" && float_dir == "right") ||
        (clear_dir == "left"  && float_dir == "left");
    if (must_jump) {
        cursor_y = std::max(cursor_y, float_bottom);
    }
    // ... existing float layout ...
}
```

This means: two consecutive `clear: right; float: right` figures
stack vertically (second one starts at `float_bottom` of the
first) instead of overlapping at the right edge.

**B. Clear on a non-float block** — forces the block below
all same-direction (or both) floats. In
`layout_block_children`'s regular block branch, before
deciding `narrow_x/narrow_w`:

```cpp
// ADR-036: clear on a non-float block forces it below
// any same-direction float. If clear_both, jump below both
// sides; if clear_right/left, jump below that side.
std::string clear_dir = "none";
if (child_style) {
    if (auto c = child_style->get("clear")) clear_dir = *c;
}
const bool must_clear_right =
    (clear_dir == "both" || clear_dir == "right");
const bool must_clear_left =
    (clear_dir == "both" || clear_dir == "left");
if (must_clear_right && float_right_w > 0) {
    cursor_y = std::max(cursor_y, float_bottom);
}
if (must_clear_left && float_left_w > 0) {
    cursor_y = std::max(cursor_y, float_bottom);
}
```

This means: an `<h2>` with `clear: both` (if Wikipedia has
such a rule) starts below any active float, not beside it.

### What ships together, and why

The two fixes are coupled: cascade alone would make thumbnails
float right, but consecutive floats would overlap. Layout
alone wouldn't help — nothing has `float: right` because the
selector doesn't match. Together they deliver the correct
visible layout.

ADR-035's `<img>` sizes reserve space inside the figure.
Figure at right edge (from float). Following text narrows
(from existing float logic). Consecutive figures stack (from
new clear handling). That's real Wikipedia.

## Design decisions

### Why `[attr~=val]` first, others as bundle

All seven attribute operators are ~30 lines each once the AST
exists. The AST is the expensive part (~40-60 lines). Once
it's in place, adding all seven is cheap and future-proofs
the cascade for rules like `[role='button']` (16 uses in
Wikipedia CSS), `[aria-hidden="true"]`, `[href]`.

Bundling all seven matches the arc's pattern (ADR-011 shipped
all block/inline layout at once; ADR-028 shipped all grid
features at once). No partial attribute-selector support.

### Why `clear` on both float and non-float

Real CSS `clear` applies to any block-level element. The
`figure[typeof~='mw:File/Thumb']` rule puts `clear: right` on
the **float itself** — a spec-standard pattern for stacking
floats vertically. The article's section headings may have
`clear: both` — a spec-standard pattern for jumping sections
below floats. Both are real, both fix real layouts.

Implementing both is ~30 lines beyond the minimal float-only
version, and matches CSS spec.

### Why not `display: flow-root` in v1

The article body's container (`.mw-parser-output` or
`mw-content-ltr`) probably has `display: flow-root` or
`overflow: hidden` in modern Wikimedia CSS — but our
measurement didn't detect it (either absent or in @media we
don't parse).

Without flow-root support, article body height **won't
include float heights**. This means:
- Article can look "shorter than real Wikipedia" below a
  floated thumbnail
- Following sections may appear to overlap the float's bottom
  visually in extreme cases

For the current HTML article specifically: thumbnails are 8443
and 8443+ px apart, so no overlap in practice. But structurally
incomplete.

**Deferred to ADR-037** (`display: flow-root` or
`overflow: hidden` for float-containing parents). Real,
principled, its own cycle.

### Why not navbox hide in this ADR

The `navbox/sidebar` overlap at x=324 (visible in ADR-035's
PNG) is a **separate defect**. It's a `<table class="sidebar">`
or `<table class="navbox">` variant that ADR-031's
`.sidebar{display:none !important}` isn't matching — likely
a different class token.

Adding a broad `.navbox{display:none}` would probably help but
risks hiding legitimate article content (some navboxes are
intentional). Better as its own focused diagnostic.

## Deferred (ADR-037+ candidates)

- **`display: flow-root`** — article body container includes
  float heights; float bottom doesn't look "cut off" below
- **`overflow: hidden` auto-float-containment** — alternative
  to flow-root; same effect
- **Navbox/sidebar class token identification** — hide
  whatever's at x=324
- **`calc()` evaluation** — search icon 2×2 today
- **`max-width` property** — footer SVGs, width-constrained
  containers
- **JPEG decode** — needs `libjpeg-turbo`
- **`object-fit`** — non-intrinsic aspect
- **`[attr~=val]` with case-insensitive modifier** (`[attr~=val i]`)
  — CSS4 feature; not used by Wikipedia assets
- **`:not()` with attribute arguments** — e.g. `:not([attr])`
  — future selector work

## Consequences

**Better:**

- **Thumbnails float right, at correct position.** The HTML5
  logo and categories diagram sit at the right edge of article
  body, with text flowing beside them — matching real
  Wikipedia. Biggest remaining visible delta below the fold.
- **Consecutive floats stack correctly.** `clear: right` on
  float prevents overlap.
- **Following blocks with `clear` jump below.** Section
  headings and other clear-having blocks position correctly.
- **Cascade gains `[attr]`, `[attr=val]`, `[attr~=val]`, etc.**
  — enables 16+ future rule matches (`[role='button']`,
  `[aria-hidden]`, etc.).
- **Zero workarounds added.** Pure cascade+layout fix; no
  viewport_css additions, no paint-layer skips.

**Accepted limitations (honest, documented):**

- **Below-fold only.** Thumbnails don't appear in first
  viewport; PNG first-viewport unchanged.
- **Article body height excludes floats.** No `display:
  flow-root` yet. Article looks slightly shorter than real
  Wikipedia. ADR-037.
- **`navbox` overlap unchanged** — separate ADR.
- **JPEG unchanged** — separate ADR.
- **Timing correction**: ADR-035's sync fetch at layout cost
  ~1 sec (6.3 sec cold render, not 5.3-5.8 as predicted).
  Documented in ADR-035's Post-ADR-036 note.

**Unchanged:**

- ADR-035's image layout (wordmark, tagline, header fix).
- ADR-034's `<img>` paint pipeline (PNG/SVG decode, cache).
- ADR-031's float v1 (narrowing following blocks beside float).

## Implementation plan

Three commits on `feat/attr-selectors-clear`:

1. **Cascade: attribute selector AST + parser + match** (all
   seven forms). Add 12-15 tests.
2. **Layout: `clear` on float + non-float** (jump-below logic
   for both float stacking and following block). Add 6-8
   tests. Verify against Wikipedia: thumbnail tree shows
   figure at right edge, following text narrowed.
3. **Docs** — ADR-036 status → Implemented, Post-ADR-036
   notes in ADR-031 (float interaction: `clear` now honored)
   and ADR-035 (timing correction + thumbnail propagation now
   delivered), CHANGELOG.

## Verification

**Unit tests** (cascade `[css][attr-sel]` tag):

- `[attr]` matches any element with the attribute
- `[attr=val]` exact match
- `[attr~=val]` word-token match
- `[attr|=val]` dash-separated match
- `[attr^=val]` prefix
- `[attr$=val]` suffix
- `[attr*=val]` substring
- `[attr=val]` non-match when attribute absent
- `[attr~=val]` non-match on partial word (e.g. `attr="abc"` with `~=ab` doesn't match)
- Case sensitivity (default: case-sensitive for value; CSS spec says optional `i` modifier — deferred)
- Compound: `figure[typeof~='mw:File/Thumb'].mw-halign-right` — both tag, attr, and class must match

**Layout tests** (`[layout][clear][adr-036]` tag):

- Float with `clear: right` after a `float: right` — jumps below
- Float with `clear: left` after a `float: right` — no jump
- Float with `clear: both` after a `float: right` — jumps below
- Non-float with `clear: right` after a `float: right` — jumps below
- Non-float with `clear: none` after a `float: right` — flows beside (narrowed)
- Two consecutive `clear: right; float: right` — stack vertically

**Visual verification** (Wikipedia HTML):

| Check | Before ADR-036 | After |
|---|---|---|
| HTML5 logo img x | 346 (left of content) | **~1422** (right edge) |
| Categories img x | 346 | **~1422** |
| Following `<p>` x | 324 (narrow) | 324 (narrow, correct) |
| Following `<p>` width | 948 | ~948 (unchanged) |
| Footer wikimedia img x | 67 (left) | **~1550** (right) |
| Footer mediawiki img x | 67 | **~1550** |
| First-viewport PNG | (wordmark/tagline rendering) | **unchanged** (thumbnails below fold) |
| Below-fold PNG | overlap | **correct** (thumbnails at right edge) |

## Acceptance criteria

ADR-036 is complete when:

- [ ] All seven attribute selector forms parsed + matched
- [ ] `figure[typeof~='mw:File/Thumb']` matches
- [ ] Thumbnail float positioned at right edge
- [ ] Consecutive floats stack (clear: right on float)
- [ ] Non-float blocks with clear jump below
- [ ] 18-23 tests added and passing
- [ ] Measured visible delta below fold (Commit 2 message)
- [ ] Post-ADR-036 notes in ADR-031, ADR-035
- [ ] `main` green: 425 + N cases, all pass, zero warnings
- [ ] CI green (PR + post-merge)
- [ ] ADR-036 status -> Implemented in Commit 3

**Explicit claim**: thumbnails float right, at correct
position; consecutive floats stack; following blocks with
`clear` jump below. Below-fold visible delta: thumbnails
positioned correctly at right edge, text flows beside them.

**Honest caveats**:

1. **First viewport unchanged** — thumbnails are below fold.
2. **Article body height excludes floats** — no `flow-root`
   (ADR-037).
3. **Timing correction recorded** — ADR-035 cost ~1 sec (6.3s
   cold, not 5.3-5.8s predicted); updated in Post-ADR-035.
4. **Navbox overlap unchanged** — separate defect.
5. **Root cause verified in Commit 1** — if attribute
   selectors ARE already supported, ADR-036 becomes
   clear-only (~40 lines). Measurement in Commit 1's
   diagnostic settles this before implementation.

**Structural value**: full attribute selector support
(`[attr]` through `[attr*=val]`) is broadly useful — 16+
rules in Wikipedia CSS use `[role=...]`, `[aria-*]`,
`[href]`. `clear` property completes the float model.
