# ADR-066: Intrinsic width for auto-width floats

**Status:** Accepted
**Date:** 2026-11-03
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-030 (float layout), ADR-036 (clear), ADR-061 (layout perf diagnostic), ADR-063 (float whitelist), ADR-064 (scroll fix), ADR-065 (diagnostic)

## Context

ADR-065 diagnostic (FLOAT-L / FLOAT-R / NARROW fprintfs added to
`layout_block_children`) on Wikipedia HTML article revealed two
float behavior categories:

| Float | Probe width | `narrow_w` | Verdict |
|---|---|---|---|
| `table.infobox` (has `width: 22em`) | 358.4 | 617.6 | OK readable |
| `figure.mw-halign-right` (no width) | 953.6 | 22.4 | BROKEN |
| `figure.mw-halign-left` (no width) | 953.6 | 22.4 | BROKEN |
| `div.navbar` (no width, text only) | 921.0 | 8.0 | BROKEN |

**The primary visible failure**: Wikipedia's `figure.mw-halign-*`
elements (thumbnails with captions, plus the Tim Berners-Lee photo
in the History section) produce following paragraphs 22.4px wide.
1-3 words per line. Screenshots after ADR-063 confirmed this.

### Root cause

Wikipedia CSS for figures:

```css
figure[typeof~='mw:File/Thumb'] {
    display: table;
    float: right;
    ...
}
```

**`display: table` has no layout engine in HoneyBee** (ADR-061
diagnostic confirmed zero "table"/"tbody"/"tr"/"td" handling in
`layout.cpp`). So `display: table` falls back to Block. Block
elements fill parent content width.

The float probe (ADR-030) calls `layout_element` with
`parent_content_width = content_w = 976`. The figure fills that,
so `probe->width = 953.6`. The follow-on paragraph gets
`narrow_w = 976 - 953.6 = 22.4`.

Real browsers implement **shrink-to-fit** for auto-width floats:
the float sizes to its content, not the parent.

### Why this is not ADR-065 (bounded floor)

Two responses existed:

- **ADR-065 bounded floor**: clamp `narrow_w` to a minimum of
  250px. Pragmatic 3-line band-aid. Text becomes readable, but
  the (still-full-width) figure overlaps the text visually.
- **ADR-066 (this ADR)**: compute intrinsic width from the
  float's content and re-layout at that width. Correct fix
  for the dominant figure-with-img case.

ADR-066 chosen as the real fix. Avoids the bounded floor's
visual overlap.

## Decision

**When a float has no explicit CSS `width` and its probe
contains an `<img>` child with valid intrinsic dimensions,
re-layout the probe at the img's intrinsic width.**

Matches CSS shrink-to-fit for the dominant case
(figure-with-img, ~90% of Wikipedia's floating figures).

### Scope

Only the figure-with-img pattern. Text-only floats (navbar-mini)
and multi-img floats stay as-is. Both documented in Deferred.

### Implementation

Insertion after the initial probe in the float block
(`layout.cpp` ~line 1300, ADR-030's probe):

1. If `child_style->get("width")` is absent / `auto` / empty:
2. Walk probe subtree for a Box with `element->tag() == "img"`
   and `0 < width < probe->width - 1` (safety: not full-width)
3. If found (`img_w > 0`), re-layout probe with
   `parent_content_width = img_w`
4. Re-layout replaces the probe unique_ptr; subsequent
   accumulation uses new `probe->width` (~img width)

### Why re-layout, not just using effective_w for narrowing

Using `effective_w` only for narrowing would leave the figure
painting at full 976px, so text (narrowed to 756) would overlap
the figure's visible left region.

Re-layout makes the figure actually shrink to ~220px. Its
`figcaption` re-wraps at 220 (correct), its `<img>` fits
(correct), and the float shifts to the right edge as a proper
220px box.

## Design decisions

### Why walk for img instead of computing content extent

Content extent (max x+width over all leaves) would still see the
figcaption — which is a block that fills its parent. So extent
= content_w. Not useful.

The img is the dominant content in figures. Its width is the
intended float width.

### Why `img_w < probe->width - 1` as filter

Safety: if the img also filled content_w (broken image, or
image_dims returned invalid, or img itself has CSS width:100%),
don't use that value. Fall back to probe->width. Prevents
regression when img_w ≈ content_w.

### Why not full shrink-to-fit

Real CSS shrink-to-fit needs max-content and min-content widths.
Computing max-content requires a "lay out with unlimited width"
mode — not available in HoneyBee's architecture.

For figure-with-img, `img_w` IS the max-content (img doesn't
wrap). So the img walk approximates shrink-to-fit for the
dominant case.

### Why re-layout at `img_w` (content width)

`layout_element`'s `parent_content_width` parameter becomes the
figure's content width. Passing `img_w` makes the figure's
content area exactly `img_w`. Its img fits exactly. Figcaption
wraps at `img_w`. Correct.

## Deferred

- **Text-only float intrinsic width** (navbar-mini, side-box) —
  needs min-content walk or reflow-at-narrow-width, ~100 lines
- **Multi-img / mixed-content shrink-to-fit** — needs max-content
- **`display: table` layout engine** — larger ADR (ADR-067
  candidate)
- **`min-content` / `max-content` as CSS keywords** — separate
- **Figcaption exceeding img width** — falls back to img width,
  may overflow caption if longest word > img_w

## Consequences

**Better:**
- Wikipedia `figure.mw-halign-*` narrows at ~220px instead of
  976px, so following text gets 976-220 = 756px — **readable**
- Tim Berners-Lee photo in History section renders correctly
  (figure right-aligned at ~220px, text left ~756px wide)
- ADR-063's float layout remains enabled and now produces
  usable output
- Infobox case (`width: 22em`) unchanged — auto-width check
  excludes it

**Accepted limitations:**
- Text-only floats (navbar-mini) still produce ~8px `narrow_w`
  (only visible near footer, minor)
- Figcaption overflow if a caption word is wider than the img
- Re-layout cost: ~15 extra `layout_element` calls per Wikipedia
  render → measured via `PERF-LAYOUT` to confirm < 100ms

**Unchanged:**
- ADR-005 through ADR-065 behavior
- Infobox case (explicit width, no re-layout)
- Non-float block layout
- Clear jumps (ADR-036, unchanged)

**Possible regression risks (verify in Commit 1):**
- **Re-layout cost**: 15 extra layout passes on Wikipedia
- **Figcaption narrow wrap**: caption may be taller at 220px
  than at 976px → figure taller
- **Existing float test**: check 687/2627 still passes
- **Img width equality**: filter `img_w < probe->width - 1`
  prevents re-layout when img ≈ content_w

## Verification

**Fixture** (`docs/adr066-float-intrinsic-test.html`):
- Row 1: `div` with `float: right` + `img` (100×120), no
  explicit div width — text beside should be wide
- Row 2 (control): same with `width: 100px` on div — text
  beside should be wide (existing behavior, no re-layout)

**Unit test** (`test_layout.cpp`):
- Fake `image_dims` returns 20×20 for `a.png`
- `<div class='f'><img src='a.png'></div><p class='p'>text</p>`
- `.f { float: right }` — no width
- Assert `<p>` width > 700 (readable, not collapsed to ~0)

**Wikipedia screenshot**: History section (Tim Berners-Lee
photo) — text beside figure should be ~756px wide, not 22px.

**Perf**: `PERF-LAYOUT` before/after — should be within 100ms
variance on Wikipedia.

## Acceptance criteria

- [ ] Auto-width float with img child: probe re-laid-out at img width
- [ ] Following block children narrowed at new width
- [ ] Infobox (`width: 22em`) case: unchanged (no re-layout)
- [ ] Fixture row 1: readable text beside figure
- [ ] Unit test passes (~780px paragraph)
- [ ] Wikipedia screenshot: readable text beside Tim Berners-Lee photo
- [ ] 687 / 2627 tests pass (or expected delta documented)
- [ ] `PERF-LAYOUT` unchanged (< 100ms delta)
- [ ] README row + CHANGELOG
- [ ] ADR-066 status → Implemented
