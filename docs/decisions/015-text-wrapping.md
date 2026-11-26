# ADR-015: Text Wrapping and Whitespace Collapsing

**Status:** Accepted
**Date:** 2026-10-20
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout), ADR-013 (length units), ADR-014 (UA stylesheet)

## Context

As of v0.2.1, every element is a block, and every text node is laid out as a single line that extends as far right as the text requires. If a text node's content is wider than its container, the text overflows past the container's right edge (and past the viewport, in a page with `overflow: visible` semantics we do not yet honor).

A short HTML file like this renders with the paragraph clipped at the window edge:

```html
<p>
  This is a long paragraph that in a real browser would wrap across
  multiple lines because the content exceeds the container width.
</p>
```

Firefox renders it in three lines within the paragraph's box. HoneyBee renders it as one line that extends beyond the viewport. That is a fundamental gap: text does not wrap, and that alone makes many real HTML files unreadable in HoneyBee.

Separately, the current text layout does not collapse whitespace. HTML's default whitespace behavior (`white-space: normal`) collapses runs of spaces, tabs, and newlines into a single space. HoneyBee currently renders them verbatim, so a paragraph with source-level indentation shows irregular spacing.

This ADR decides how to add text wrapping and whitespace collapsing, in a way that does not regress the rendering of Arabic text.

## Decision

Three coordinated changes in the layout pipeline:

1. Whitespace collapsing in text nodes before measurement.
2. Line breaking at word boundaries using the existing `TextMeasurer` seam.
3. Direction-aware line layout so RTL text wraps correctly.

### Scope of v1

**Whitespace collapsing** per CSS `white-space: normal`:

- Runs of spaces, tabs, newlines, carriage returns, form feeds collapse to a single space.
- Leading and trailing whitespace of the text node is removed.
- After line breaking, the trailing whitespace of each line (before a break) is removed.
- `&nbsp;` (U+00A0) is preserved as a non-collapsing, non-breaking space.

**Line breaking** at word boundaries:

- Words are delimited by collapsed whitespace.
- Greedy line-filling: measure each word; append to the current line if it fits, else start a new line.
- Words longer than the container are placed on their own line and overflow (no character-level breaking; `overflow-wrap` is deferred).

**Direction**:

- `direction: rtl` if the first strong character of the text (skipping whitespace and punctuation) is in the Arabic, Hebrew, Syriac, Thaana, NKo, or Adlam Unicode blocks.
- `direction: ltr` otherwise.
- Under RTL, words are laid out from the container's right edge moving left. Each word is still shaped correctly by Pango (Arabic shaping is Pango's job, independent of our line-level ordering).
- Under LTR, words flow left to right from the container's left edge.

**Only single-direction per text node.** A text node whose content mixes scripts (e.g., `Hello عربي world` in one node) uses the direction of its first strong character and renders in that direction. Mixed LTR/RTL reordering within one text node is deferred.

### Deferred

- `display: inline` for `<span>`, `<em>`, `<a>` -- they still stack as blocks (ADR-016+)
- Multi-direction within one line (bidi reordering algorithm; Unicode UAX #9)
- `white-space: pre` / `pre-wrap` / `pre-line` (needs `<pre>` element and monospace handling)
- `line-height` property (uses Pango's natural line height per line)
- `text-align: center` / `right` / `justify` (left/right from direction only)
- Hyphenation, `word-break`, `overflow-wrap`, `hyphens`
- Character-level breaking for CJK
- `direction` CSS property or `dir="rtl"` attribute (heuristic only in v1)
- Orphan/widow control
- `text-overflow: ellipsis`

### Data model change

`LayoutText` becomes a multi-line node. It still represents one text node in the DOM, but internally holds a sequence of line fragments:

```cpp
struct LineFragment {
    float x = 0.0f;        // absolute x of the line's left edge
    float y = 0.0f;        // absolute y of the line's top
    float width = 0.0f;    // total pixel width of the line's content
    float height = 0.0f;   // line height (Pango's per-line pixel height)
    std::string text;      // the line's text, with collapsed whitespace
};

struct LayoutText : LayoutNode {
    std::string text;                          // full collapsed text
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();
    std::vector<LineFragment> lines;           // one entry per rendered line
    bool rtl = false;
    // x, y, width, height (from LayoutNode) become the bounding box
};
```

The `LayoutText`'s bounding `width` is the maximum line width. Its `height` is the sum of line heights. Its `x`, `y` are the top-left of the first line.

Paint iterates `lines` and draws each one via the Pango drawer.

### Line-breaking algorithm

Greedy, one pass. Given collapsed text, direction, font metrics, and max width:

```
   words = split_collapsed_whitespace(collapsed_text)
   current_line_text = ""
   current_line_width = 0.0
   lines = []

   for each word in words:
       word_width = measure(word, font)
       space_width = (current_line_text.empty() ? 0.0 : measure(" ", font))
       if current_line_width + space_width + word_width <= max_width:
           current_line_text += (current_line_text.empty() ? "" : " ") + word
           current_line_width += space_width + word_width
       else:
           if current_line_text is not empty:
               lines.push(LineFragment(text=current_line_text, width=current_line_width))
           current_line_text = word
           current_line_width = word_width

   if current_line_text is not empty:
       lines.push(LineFragment(text=current_line_text, width=current_line_width))
```

Then, for RTL, each `LineFragment.x` is set to `content_x + content_w - fragment.width` (right-aligned). For LTR, `fragment.x = content_x`. `fragment.y` advances by the previous line's height.

Line height comes from `measure(line_text, font)`. Its `y` component is the line's pixel height as Pango reports it, which includes proper ascender/descender spacing. No `line-height` property is applied on top of that.

### Direction detection (first-strong heuristic)

```
   rtl_strong(cp) =
       U+0600..U+06FF   -- Arabic
    || U+0750..U+077F   -- Arabic Supplement
    || U+08A0..U+08FF   -- Arabic Extended-A
    || U+FB50..U+FDFF   -- Arabic Presentation Forms-A
    || U+FE70..U+FEFF   -- Arabic Presentation Forms-B
    || U+0590..U+05FF   -- Hebrew
    || U+0700..U+074F   -- Syriac
    || U+0780..U+07BF   -- Thaana
    || U+07C0..U+07FF   -- NKo
    || U+1E900..U+1E95F -- Adlam
```

Direction is determined by the first strong character: RTL if that first strong character is in one of the ranges above, LTR if it is any other letter, LTR for text with only punctuation or digits.

## Rationale

### 1. Text wrapping is a fundamental gap

A browser that clips long paragraphs is not a browser. Text wrapping is not optional -- it is one of the most basic layout behaviors in HTML. If a stranger opens HoneyBee today and loads a documentation page, a blog post, or a Wikipedia article, the paragraph renders clipped. That is worse than a text dump -- it is a text dump that cannot be read.

### 2. Arabic regression must be prevented

If we add naive LTR wrapping without direction awareness, a long Arabic paragraph would wrap into lines where the words are in LTR order within each line, e.g. `ورحمة الله السلام عليكم` instead of `السلام عليكم ورحمة الله`. That is worse than the current clipped single-line rendering -- words would be misordered, changing the meaning. Since HoneyBee's mission is "Fast websites for Egypt," this regression cannot happen.

The direction heuristic is 30 lines of code (a codepoint range check) and it makes Arabic wrapping correct for single-direction paragraphs, which is the common case.

### 3. Reusing the existing TextMeasurer seam

Layout already has a `TextMeasurer` callback: production uses Pango, tests use a fake. Line breaking needs to measure words. It can use the same seam -- measure each word, pack into lines. No new dependencies, no new test bypass mechanism, and line-breaking unit tests are exactly as easy as layout unit tests are today.

The alternative -- call `pango_layout_set_width` and let Pango break lines -- would couple layout to Pango, break headless unit tests, and violate ADR-011's architecture (layout is pure computation, paint is I/O).

### 4. Whitespace collapsing is a prerequisite

Without whitespace collapsing, indented HTML source produces irregular spacing on screen. With it, source-level whitespace becomes semantically irrelevant, matching how HTML is written in practice. It is a small amount of code (60 lines of collapse logic) and it prevents every future page from looking "almost right."

## Alternatives Considered

### Delegate line breaking to Pango

**Rejected.** `pango_layout_set_width` + `pango_layout_get_line` would work, but it couples layout to Pango (breaking ADR-011's architecture), breaks headless tests (the `TextMeasurer` fake would need a fake line-breaker too), and makes line-breaking behavior dependent on Pango version rather than under our control. The greedy algorithm above is ~80 lines of code.

### No direction handling in v1, defer to ADR-016

**Rejected.** Naive LTR wrapping would regress Arabic rendering. That is not "deferred feature" -- it is "shipped bug." Better to ship direction-aware wrapping in v1 and defer the harder mixed-direction case.

### Full Unicode Bidi Algorithm in v1

**Rejected.** UAX #9 is ~50 pages of spec and thousands of lines of implementation. Full bidi is a separate ADR, probably years down the line. The first-strong heuristic handles the common cases (single-direction paragraphs) with 30 lines.

### Character-level breaking for CJK

**Deferred.** Chinese, Japanese, Korean have no spaces; line breaking happens between most characters with rules (kinsoku). Our tokenizer splits on whitespace only. A CJK paragraph would render as one long line in v1. That is a real limitation but affects a smaller portion of the project's target audience than wrapping English and Arabic correctly.

## Consequences

### Files changed

- `src/renderer/layout/layout_box.hpp` -- add `LineFragment`, extend `LayoutText` with `std::vector<LineFragment> lines` and `bool rtl`
- `src/renderer/layout/layout.cpp` -- collapse whitespace, detect direction, break into lines, populate `LayoutText::lines`
- `src/renderer/paint/paint.cpp` -- draw each `LineFragment` instead of the whole text once
- `src/browser/pango_text.cpp` -- `draw_with_pango` gains a `LineFragment`-aware variant
- `tests/test_layout.cpp` -- new tests for line breaking (English, Arabic), whitespace collapsing, direction detection
- `tests/test_paint.cpp` -- one test: multi-line text draws at the right y positions

No changes to `renderer::css`, `renderer::dom`, `renderer::html`.

### Behavior examples

| Input | Before | After |
|---|---|---|
| `<p>Short text</p>`, container 800px | one line at y=0, height 16 | one line, unchanged |
| `<p>` 200 chars, container 800px | one line, extends to ~1600px (clipped) | wraps to 2-3 lines, all within 800 |
| `<p>   Multiple\n  spaces  </p>` | renders literal whitespace | renders "Multiple spaces" |
| `<p>السلام عليكم ورحمة الله وبركاته</p>`, 400px container | one line, extends past 400 | wraps to 2 lines, words ordered RTL within each |
| `<p>a b c d e</p>` (measure "a" = 10px, " " = 4px), container 50px | one line, extends past 50 | lines: "a b c", "d e" |

### What layout doesn't change

Block flow, box model, `LayoutBox` structure, margin/padding/border application, auto margins -- all identical. ADR-015 only changes the internals of `LayoutText` and how paint consumes it.

### What paint doesn't change

The `paint()` entry point, `paint_box`, `paint_children` -- identical. The change is inside `draw_with_pango`: it now takes a `LineFragment` and its surrounding `LayoutText` context, and draws one line.

## Open Questions

### Mixed LTR/RTL within one text node

A text node like `Hello عربي world` renders in first-strong direction (LTR here), with words in source order. That is correct for the common case but wrong in general for bidi text. Deferred to a future bidi ADR.

### Word-only CJK paragraphs

Chinese, Japanese, Korean paragraphs have no spaces; v1's tokenizer treats them as one long word. Deferred to a CJK-specific ADR.

### Line height and line-height property

v1 uses Pango's per-line pixel height. Adding a `line-height` property is a small ADR, separate from line breaking.

### Overflowing long words

A single word longer than the container overflows. `overflow-wrap: break-word` and `hyphens: auto` are deferred.

### `dir="rtl"` attribute

v1 uses first-strong. A page with `dir="rtl"` explicitly set but LTR content renders LTR in v1. That is arguably wrong per HTML spec but acceptable for v1. Deferred to a `dir` attribute ADR.

## Record of Changes

- **2026-10-20:** Initial decision.
