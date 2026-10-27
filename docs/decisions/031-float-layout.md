# ADR-031: Float Layout (v1: block-level float + block narrowing) + Stray Closing-Tag Fix

**Status:** Accepted
**Date:** 2026-10-26
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout), ADR-016 (inline formatting), ADR-028 (grid), ADR-029 (flex), ADR-030 (feature-class flip table)

## Context

ADR-030's visual verification confirmed the article intro at y=145
in the first viewport of Wikipedia — deeply above the fold. Two
honest deferrals were documented in ADR-030's Implementation
section:

1. **Infobox renders as inline list.** The HTML article's
   infobox (`.infobox` table with `float:right; clear:right;
   width:22em; margin-left:1em`) is currently laid out as a
   stacked block list of its `.html` / `.htm` / `text/html` /
   `WHATWG` / `W3C` rows. In real Wikipedia, it appears as a
   compact 22em box at the top-right of the article content, with
   the article intro text flowing to its left.

2. **Stray closing-tag fragment** visible in the top-right of
   the viewport. A text node containing `</html>` (or similar)
   is being rendered as literal content. Present in ADR-029's
   baseline too; not introduced by any recent change.

### Measurement (Wikipedia CSS, 2026-10-26)

**Float distribution across Wikipedia's CSS**:

| Value | Count |
|---|---|
| `float: right` | 14 |
| `float: right !important` | 1 |
| `float: left` | 10 |
| `float: none` | 7 |
| `float: none !important` | 3 |
| **Total float declarations** | **35** |

**The specific infobox rules**:

```css
.infobox {
    margin-left: 1em;
    float: right;
    clear: right;
    width: 22em;   /* 352px at 16px root */
}

/* Not applicable: we are not the minerva skin. The rules below
   would override to float:none !important; they don't match
   because <body> lacks .skin--responsive.skin-minerva. */
body.skin--responsive.skin-minerva .mw-parser-output .infobox {
    float: none !important; ...
}
```

**First-viewport relevance**: the infobox is the ONLY floated
element in the first viewport of the HTML article. Other floats
(thumbnails, sidebars) appear further down or in other articles.

**Current state**: `float` is completely unimplemented in our
codebase (grep confirms zero hits for `"float"` / `Float::` /
`float_dir` in the CSS parser or layout engine). Elements with
`float: right` participate in block flow as if `float: none`.

### The scope split

Three CSS features are legitimately deferred from ADR-030:

- **`position`** (47 rules, 22 relative / 16 absolute / 3 fixed /
  2 sticky / 4 `!important`). No visible first-viewport driver —
  ADR-030's measurement confirmed this.
- **`:checked`** (14 rules). All drive dropdown open/close state
  and radio widget styling; none affect the first viewport without
  user interaction (language dropdown already hidden by ADR-029
  workaround; user menu, page-tools dropdown are already
  feature-flip-hidden).
- **`:not()`** and **`clip`**. Needed for the proper skip-link
  visually-hidden pattern (which ADR-030 worked around with
  `!important`).

None of these are urgent. **`float` is** — the infobox is a
directly visible bug.

**Decision: ADR-031 = float v1 + stray-`</html>` fix.** ADR-032+
covers `position` + `:checked` + `:not` + `clip` + `z-index` +
`float` text-wrap (v2).

## Decision

Two changes, both small and independently verifiable.

### 1. Float layout v1: block-level float + block narrowing

**Scope (v1)**:

- `float: left | right | none` parsed from `ComputedStyle`
  (already passes through the cascade as a declaration; we read
  it via `style->get("float")`).
- A floated child is laid out at its natural (or explicit)
  width, anchored at the left or right edge of its block parent's
  content box.
- The float is drawn with its own `margin` and `padding`
  applied (existing block layout handles those).
- The float's vertical extent is remembered by the parent
  layout loop.
- **Subsequent block children**: if a child's `y_before_layout`
  falls within the float's vertical extent (`y < float_bottom`),
  its content width is narrowed by `float_width + float_margin`.
  If the child starts below `float_bottom`, full width.
- **Inline content inside a narrowed block**: the whole block is
  narrowed uniformly. Lines that extend past `float_bottom` are
  still narrow (v1 approximation).
- **`clear`** property parsed but treated as `none` (no forced
  break; the `float_bottom` check already produces the sensible
  behavior for the infobox case).

**Not in scope (deferred to ADR-032 v2)**:

- Per-line text-wrap: inline content that narrows for lines
  overlapping the float, then widens after `float_bottom`. This
  is the "proper" CSS behavior and requires line-fragment-level
  awareness in `layout_inline_run`. ~200-250 extra lines.
- Float stacking: multiple floats per horizontal band. Not used
  in Wikipedia's first viewport.
- `float` inside flex/grid children: laid out as block (already
  the case for `float:none`).
- `clear: left | right | both` semantics beyond what falls out
  of the block-narrowing rule.

**Why this scope is honest**:

- The infobox case is *exactly* "block-level float at top of
  article content, following block content narrows". v1 covers it.
- Per-line wrap is a bigger change; its absence means an
  approximation is visible (paragraphs that end below the float
  stay narrow). That's documented, measured, and deferrable.
- Real browsers do the full per-line version. We're shipping an
  intermediate that gets the visible 80%.

**Measured expectation**: after v1, the infobox appears as a
22em box at the top-right of `.mw-parser-output`. The intro
paragraph narrows to ~1046px content width (1398 - 352 - 16
margin-left - 16 float margin-left default = ~1014 effective).
At y=float_bottom (infobox height ≈ 800-1000px depending on
render), subsequent blocks return to full 1398px width.

### 2. Stray `</html>` fix

**Root cause (hypothesis)**: a text node with literal `</html>`
or a similar closing-tag fragment is present somewhere in the
DOM tree. Most likely a lexbor parse quirk with Wikipedia's
malformed HTML (Wikipedia's output is not strictly
well-formed; it depends on browsers' error recovery).

**Fix approach (v1)**:

1. Commit 1's diagnostic pinpoints the exact text node (path in
   the DOM tree, content, byte size).
2. If it's a real text node from source: strip closing-tag-only
   text nodes during post-parse cleanup in `html/convert.cpp`.
   The cleanup skips text nodes whose entire (trimmed) content is
   `</tag>` or `<!-- ... -->`.
3. If it's a lexbor parse artifact: fix in the post-lexbor tree
   walk.
4. Only if both (2) and (3) don't apply: document as a known
   limitation and defer.

Commit 1 records which path applies. This is intentionally a
small in-Cycle fix (~30 lines) that shouldn't block the float
work.

## Design decisions

### Why block-narrowing (not full text-wrap) in v1

Three considerations:

1. **API fit**: `layout_block_children` already knows the content
   width for each child. Narrowing it before calling
   `layout_element` is a ~5-line addition per child. Per-line
   wrap would require a new `narrow_lines_after_y` parameter to
   `layout_inline_run` and handling inside
   `flatten_inline_to_words` — a larger change to the most
   performance-sensitive path (line layout runs O(lines ×
   fragments) on Wikipedia's 55k-line article).

2. **Visual quality**: for the infobox case, all article intro
   lines are within the float's vertical extent. Full narrowing
   is visually identical to proper per-line for the target case.

3. **Incremental honesty**: shipping v1 with a documented
   approximation lets the ADR's implementation section report
   measured actuals (as ADR-030 did for actuals-exceeding-predictions).
   If the visible quality is unacceptable, ADR-032's per-line
   work starts from working float infrastructure rather than a
   monolith.

### Why not defer float to ADR-032 as well

Because it's the directly visible bug in the article body, and
because it's genuinely 120-150 lines. The full ADR-032 chunk
(`position` + `:checked` + `:not` + `clip` + `z-index` + float v2)
is 400+ lines; splitting off the visible win is the honest
sequencing.

### Why `clear` is parsed but treated as none

The `.infobox` rule uses `clear: right`. In practice, at the top
of `.mw-parser-output`, there's nothing above the infobox to
clear. So `clear: right` is inert. Treating `clear` as `none`
produces identical output for the target case. Full `clear`
semantics (forcing the block below the float bottom regardless
of overlap) are a natural v2 addition when other pages need
them.

### Why not support `float` inside flex/grid children

`float` on a flex/grid item is treated as `float: none` per CSS
spec (floats don't float in flex/grid containers). Our current
behavior already does this (float ignored → block layout). No
change needed. Documented for clarity.

### The bundled `</html>` fix — why it's in ADR-031 Commit 1

The diagnostic is small; the fix depends on what it finds. If
it's a text-node strip in `html/convert.cpp`, that's ~30 lines.
If it's a lexbor quirk, could be smaller or larger. Bundling it
with the float work keeps the ADR-031 PR story coherent ("fix
the two honest deferrals from ADR-030's Implementation section")
and avoids a separate micro-PR that would interrupt the cycle.

If the diagnostic shows the fix is larger than expected (>100
lines), Commit 1 will ship float alone and the stray-`</html>`
diagnostic becomes its own micro-cycle. Documented in Commit 1's
message either way.

## Deferred (ADR-032+ candidates)

- **`position: absolute | fixed | relative | sticky`** — 47
  uses; no current visible driver; needed for future
  click-to-open dropdowns, infobox thumbnails, sticky headers.
- **`:checked`** — 14 uses; all dropdown/radio state; not in
  first viewport.
- **`:not()`** — general selector negation; needed for skip-link
  visually-hidden pattern.
- **`clip` / `clip-path`** — visual clipping.
- **`z-index`** — paint order for positioned elements.
- **Per-line float text-wrap** (this ADR's v2) — proper CSS
  behavior for the infobox and future thumbnails. Requires
  line-fragment-level float awareness.
- **Float stacking** — multiple floats per band.
- **`clear: left | right | both`** — forcing block below float.
- **`visibility`, `opacity`** — needed for `:checked` dropdowns
  once they're addressable.

## Consequences

**Better:**

- **Infobox renders as a proper 22em box at top-right** of the
  article content, instead of a stacked inline list. Largest
  remaining visual bug in the article body.
- **Article intro narrows** around the infobox, matching real
  Wikipedia's article-content layout. Text is where it should be
  — to the left of the infobox.
- **Stray `</html>` cleaned up** — top-right corner of viewport
  visually clean.
- **Float infrastructure exists** for future work (thumbnails,
  sidebars, pull quotes) — ADR-032's per-line wrap builds on
  working v1 rather than a monolith.

**Accepted limitations (v1 approximation):**

- **Whole-block narrowing** instead of per-line wrap. Paragraphs
  that start within the float zone stay narrow even for lines
  below `float_bottom`. Documented in Implementation section;
  deferred to ADR-032 v2.
- **No `clear` semantics beyond block-narrowing**. `clear: right`
  on the infobox is inert in our target case (nothing above it).
- **No float stacking**. Multiple floats in a band lay out as
  sequential blocks at their edge positions.
- **`float` inside flex/grid children** treated as block (CSS
  spec behavior).
- **`float: inline-start | inline-end`** (CSS logical values)
  not parsed — treated as `float: none`.

**Unchanged:**

- All prior ADRs (018-030).
- ADR-029's flex algorithm, feature-class flip, `#p-lang-btn`
  workaround.
- ADR-030's flip table, skip-link `!important` workaround.
- Article body layout, scroll, hover, navigation.

## Implementation plan

Two commits on `feat/float-layout`:

1. **Float parsing + block-narrowing layout + stray `</html>`
   fix** — parse `float` from ComputedStyle; in
   `layout_block_children`, detect floated children, lay them
   out at edge with natural width, remember vertical extent,
   narrow subsequent block children whose `y_before` is within
   it. Diagnostic-then-fix for the stray `</html>` (bundle or
   defer based on diagnostic result, documented in commit
   message).
2. **Docs** — ADR-031 status → Implemented, Post-Implementation
   note in ADR-030's Implementation section updated with new
   infobox y-coordinates, CHANGELOG, README row.

## Post-ADR-032 correction (2026-10-26)

ADR-031 shipped with a "known paint overlap" note in its commit
message. That claim was a **misreading** of the rendered PNG.

Diagnostic confirmation (via ADR-032's `find_leak` walk of the
layout tree for every box at `x<500, y<900`):

| Element | y | w | Content |
|---|---|---|---|
| `<h1>#firstHeading>` | 84 | 948 | Article title "HTML" |
| 4 maintenance banners | 259-466 | 948 | Ambox warnings |
| `<p>#mwEg>` | 497 | **590** | Intro paragraph 1 |
| `<p>#mwHw>` | 613 | **590** | Intro paragraph 2 |
| `<p>#mwJg>` | 729 | **590** | Intro paragraph 3 |

**No navbox, no leaked infobox content, no overlap.** The intro
paragraphs are correctly narrowed to w=590 beside the infobox
at x=914. Every box in the first viewport is legitimate article
content.

### What was actually seen

The HTML article's intro paragraphs legitimately contain the
strings `HTML5`, `XHTML`, `text/html`, `WHATWG` -- the article
is about those exact subjects. When the paragraphs wrap to 4-5
lines each at Pango's ~23px line height, the visual density
resembled a nav list. Combined with a stale PNG from before the
`.sidebar` hide (ADR-031 Commit 1's second fix), the "overlap"
appeared real.

The earlier `leak_locate` diagnostic (searching for the same
strings) already showed all infobox-related hits at
`y >= 4317`, way below the first viewport. That was the signal
that should have stopped the investigation.

### What this means

- **ADR-031's float v1 is correct.** Block-level float, block
  narrowing, `.sidebar` hide all work as intended. The layout
  tree matches the plan.
- **The "known bug" note in ADR-031 Commit 1's message was
  wrong.** It stays in the commit message (historical record,
  not rewritten) but this correction supersedes it.
- **The `.sidebar{display:none}` workaround** (ADR-031 Commit 1's
  second fix) is still useful: it removes the 1517px nav
  template that was crowding the article column, freeing the
  intro to land at y=497 instead of y=2014.
- **No further fix needed for ADR-031's "overlap."**

### The pattern

Same class of correction as ADR-028's grid misdiagnosis:

- A visual signal led to a hypothesis (overlap in first
  viewport)
- The hypothesis shipped with a caveat (draft PR, then merged
  with "known overlap" note)
- A diagnostic disproved it (find_leak showed only legitimate
  content)
- The record is corrected in place, not rewritten

The discipline holds: **the historical record shows both the
original claim and the correction, side by side.** A future
reader can see how the misreading happened and trust the
corrected version because the evidence is preserved.

## Post-ADR-033 note (2026-10-26)

ADR-033 shipped `position: relative | fixed`. Zero visible
delta on the article PNG -- byte-identical to pre-ADR-033.

**Relevance to this ADR's float v1**: none. The
`position: relative` declarations on the 22 rules that use them
are context-only (zero actual top/left/right/bottom offsets).
None interact with float layout. The one `position: absolute`
element in the first viewport (search icon) is treated as
`static` in ADR-033 v1.

Float v1 remains correct and unaffected. The subsequent ADR-034
(`position: absolute` with containing-block tracking) will
introduce the first real interaction between position and
layout flow -- absolute children can be positioned relative to
a `position: relative` ancestor, which is where float and
absolute could interact (e.g., absolute element inside a
narrowed float zone). That's future work.

## Post-ADR-034 note (2026-10-27)

ADR-034 shipped `<img>` paint with SVG decode. **No interaction
with float v1 in the first viewport**:

- The infobox is a `<table>` element, not `<img>`. Float layouts
  the table; image paint doesn't touch it.
- The one `<img>` inside the infobox (`.infobox-image`, official
  logo) resolves to `//upload.wikimedia.org/...` -- a
  protocol-relative URL. Fails to fetch per ADR-034's finding
  #2. Once `resolve_url` handles protocol-relative srcs, this
  image will render inside the infobox's float-narrowed cell
  without interaction with float code.

**When it interacts:** ADR-035 (layout-integrated `<img>`) will
make `<img>` reserve layout space, which means `<img>` inside a
float will correctly contribute to the float's height. That's
the first real float + image interaction. Currently zero.

## Post-ADR-035 note (2026-10-27)

ADR-035 integrated image intrinsic dimensions into layout.
**Interaction with float v1 (this ADR) surfaced a real
limitation**:

Wikipedia thumbnails are `<img>` inside `<figure class="thumb">`
which CSS sets to `float: right`. With ADR-035, the `<img>`
inside the figure has real dimensions (h=250 for the HTML5
logo). But the float container still doesn't contribute its
height to the parent's `cursor_y` — so article text flows
beside the thumbnail (narrowed), not below it.

**This is correct float v1 behavior** (block-level float,
no `clear`, no parent-height contribution). The visible
article-layout implication is that thumbnail images sit inside
the article's text flow, which reads oddly for large images.

ADR-036 (layout-integrated float) will resolve:
- Floated children contribute height to parent when the float
  is the only child of a block
- `clear: left | right | both` forces following content below
  the float
- This makes thumbnail images push article text down
  correctly

Until then, ADR-031's float v1 is unchanged; the ADR-035
integration simply makes float-contained `<img>` sizes
accurate, without changing flow.

## Verification

**Unit tests** (in `tests/test_layout.cpp`, new
`[layout][float][adr-031]` tag):

- `.f { float: right; width: 100px }` in 500px container:
  floated element at `x = 400` (content_x + content_w - 100)
- `.f { float: left; width: 100px }` in 500px container:
  floated element at `x = content_x`
- Block child starting below float bottom: full width
- Block child starting within float zone: narrowed width
- Two block children, second starting within float: both
  narrowed
- Block child with `y_before_layout >= float_bottom`: full
  width
- Two children, one floated one not: correct narrow/full
  sequence
- Stray text-node test: text content equal to `</html>` doesn't
  produce an inline run

**Visual verification on Wikipedia** (viewport 1774):

| Check | Before ADR-031 | After ADR-031 |
|---|---|---|
| `.infobox` x | 324 (inline in flow) | ~1042-1374 (right edge) |
| `.infobox` width | full content (1398) | ~352 (22em) |
| Infobox appearance | stacked list | proper box |
| Article intro `<p>` width | 1398 | ~1014 (narrowed) |
| Stray `</html>` in viewport | visible | hidden |
| `mw-page-container-inner` y | 36 | 36 (unchanged) |
| `h1#firstHeading` y | 84 | 84 (unchanged) |
| `vector-body` y | 145 | 145 (unchanged) |

## Acceptance criteria

ADR-031 is complete when:

- [ ] `float: left | right | none` parsed from ComputedStyle
- [ ] Floated children laid out at correct edge of block parent
- [ ] Following block children narrowed when overlapping float zone
- [ ] Following block children full-width when below float bottom
- [ ] Stray `</html>` text node diagnosed (path recorded) and
      fixed or documented
- [ ] 6-8 unit tests added and passing
- [ ] Measured visible delta on Wikipedia (Commit 1 message)
- [ ] ADR-030's Implementation section Post-ADR-031 update
      appended with new infobox + intro y/width measurements
- [ ] `main` is green: 399 + N cases, all pass, zero warnings
- [ ] ADR-031 status -> Implemented in Commit 2

**Explicit claim**: the Wikipedia HTML article's infobox renders
as a 22em box at the top-right of `.mw-parser-output`, with the
intro paragraph narrowing to ~1014px content width around it.
Stray `</html>` fragment gone. Article body position unchanged
from ADR-030 (intro at y=145).

**Honest caveat**: this is v1 with block-level narrowing, not
per-line text-wrap. Paragraphs that begin within the float zone
stay narrow even for lines below the float bottom. ADR-032's v2
will ship proper per-line wrap. The v1 approximation is
measured in Commit 1's message; if quality is unacceptable, v2
starts from working float infrastructure.

**Second honest caveat**: the `</html>` fix depends on the
diagnostic result. If the fix is >100 lines (unlikely but
possible if lexbor is producing the artifact and it needs
post-parse cleanup), the fix defers to its own micro-cycle and
ADR-031 ships float alone. Commit 1's message documents which
path applied.
