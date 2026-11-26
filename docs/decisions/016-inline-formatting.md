# ADR-016: Inline Formatting and Display

**Status:** Accepted
**Date:** 2026-10-21
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-011 (layout), ADR-014 (UA stylesheet), ADR-015 (text wrapping)

## Context

As of v0.3.1, every element is laid out as a block. `<p>Hello <em>world</em>!</p>` renders as three vertically stacked blocks:

```
   Hello
   world
   !
```

Firefox renders it as one flowing line:

```
   Hello world!
```

with `world` in italics. The difference is **inline formatting** — the concept that `<span>`, `<em>`, `<a>`, `<strong>` and their content flow inline with surrounding text rather than stacking vertically.

Inline formatting is not a small feature. It requires:

1. **A `display` concept** — distinguishing block-level from inline-level elements.
2. **An inline formatting context** — a block container's inline children flow together and wrap as a unit.
3. **Cross-element line breaking** — a word that starts inside a `<em>` can end on the next line, and a `<span>` can split across lines.
4. **Per-fragment styling** — a `LineFragment` derived from an `<em>` needs its own font/color, distinct from surrounding text.

ADR-015 already introduced `LayoutText::lines` (a vector of `LineFragment`). Extending that to cover inline content is the natural next step.

Separately, ADR-015 shipped a **known behavior** documented in `current-state.md`: the first-strong RTL heuristic diverges from Firefox for mixed-direction paragraphs. The welcome page's fourth line (`هذا نص عربي. Rendered via Pango.`) renders right-aligned in HoneyBee but left-aligned in Firefox. The real fix is **`dir` attribute support**, which HTML provides as the author's escape hatch. That belongs in this ADR because inline formatting is where direction gets resolved for a real paragraph — per formatting context, not per text node.

This ADR decides both.

## Decision

Three coordinated changes:

1. **`display` becomes a cascade property** with values `block` and `inline` in v1 (`none`, `flex`, `grid`, `table` deferred).
2. **Inline formatting context** for block containers with inline children. The container's inline children flow together, wrap as a unit, and produce `LineFragment`s that carry per-fragment styling.
3. **`dir` attribute support** — `dir="ltr"`, `dir="rtl"`, `dir="auto"` on any element. `auto` applies the first-strong heuristic; explicit values override; missing values inherit from the parent, default LTR at `<html>`. This resolves the ADR-015 heuristic divergence.

### Scope of v1

**`display` property**:

- `display: block` — element participates in block flow (current behavior)
- `display: inline` — element participates in inline formatting context
- `display: none` — **deferred** (separate ADR; would retire the `layout.cpp` skip list)
- `display: flex` / `grid` / `table` / `contents` — **deferred**

Read from `ComputedStyle::get("display")` in `layout.cpp`. Default when missing: `block` (safer than HTML's inline default — an unknown element shouldn't silently become inline and break the layout).

**UA stylesheet additions** (ADR-014's file, extended):

```css
span, a, em, i, strong, b, code, kbd, samp, var, small, sub, sup,
abbr, cite, q, s, u, time, mark { display: inline }
```

That covers the common inline elements. Others default to `block` (which is what they already are — no regression).

**Inline formatting context**:

When a block container has any inline child (text node or `display: inline` element), those children are collected into a single inline formatting context:

```
   <div>
     <em>Hello</em> world <span>foo</span>
   </div>

becomes:

   LayoutBox (div, block)
     └── LayoutInlineContext
           ├── LayoutInline (em) > LayoutText ("Hello")
           ├── LayoutText (" world ")
           └── LayoutInline (span) > LayoutText ("foo")
```

The `LayoutInlineContext` collects all inline items (from any depth of inline nesting), flattens them into a word list, and runs line breaking across the whole sequence — not per text node. That's what allows "Hello world foo" to flow together and wrap correctly.

A block container can also contain **mixed block and inline children**:

```
   <div>
     <p>block paragraph</p>
     <em>inline text</em>
   </div>
```

Browsers handle this by treating the inline run as an anonymous block. For v1: **the inline run becomes a `LayoutInlineContext` (a block), and inline runs and block children stack vertically in source order.** That's what Firefox effectively does.

**`LineFragment` gains style fields**:

```cpp
struct LineFragment {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::string text;
    // new in ADR-016:
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();
};
```

Because a fragment can come from inside an `<em>` (with italic style, eventually) or a `<span style="color: red">` (with red color), each fragment carries its own style. Paint draws each fragment independently, using its own style.

`LayoutText` keeps `font_family`, `font_size`, `color` for its *default* style. Fragments that come from the text node itself inherit those; fragments from inline children use the child's style.

**`dir` attribute support**:

Read directly from `dom::Element` in layout:

```cpp
enum class Direction { Ltr, Rtl };

Direction resolve_direction(const dom::Element& el,
                            const dom::Element* parent,
                            std::string_view text);
```

Resolution order for an element:

1. `el.get_attribute("dir") == "ltr"` -> LTR
2. `el.get_attribute("dir") == "rtl"` -> RTL
3. `el.get_attribute("dir") == "auto"` -> first-strong heuristic on the element's own text content
4. Attribute missing -> inherit from parent element (default LTR at `<html>`)

Direction is resolved **per block container** and applied to the entire inline formatting context within. That's CSS-correct: `direction` inherits, so `<p dir="rtl">` renders all its inline content (including `<em>` children) RTL. Bidi reordering within a line is deferred.

The welcome page's `هذا نص عربي. Rendered via Pango.` line has no `dir` attribute, inherits from `<body>` (which has none), inherits from `<html>` (which has none). Default LTR. So the mixed paragraph renders LTR — matching Firefox. The ADR-015 divergence is resolved by making it explicit.

### Deferred

- `display: none` (retires the `<head>` skip list — separate ADR)
- `display: flex` / `grid` / `table` / `contents`
- Full HTML element coverage for `display: inline` (v1's UA stylesheet adds 19 elements; more in follow-up)
- **Baseline alignment** — v1 top-aligns fragments within a line, so a `<span style="font-size: 24px">` inside a 16px paragraph sits at the line's top, not on the text baseline. Defer to a baseline-alignment ADR when inline font-size overrides are tested in practice.
- `direction` CSS property (only `dir` attribute in v1)
- Bidi reordering within a line (still first-strong at context level; full UAX #9 is a future ADR)
- Inline boxes that span across block boundaries (block-inside-inline: fallback to block for v1 — see Open Questions)
- `vertical-align`
- `line-height` property
- Float, position, overflow

### Data model

New enum and node types in `layout_box.hpp`:

```cpp
enum class Display : std::uint8_t { Block, Inline, None };

struct LayoutInline : LayoutNode {
    const dom::Element* element = nullptr;
    std::vector<std::unique_ptr<LayoutNode>> children;
    // style for fragments derived from this element
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();
    Display display = Display::Inline;
    LayoutInline() : LayoutNode(LayoutKind::Inline) {}
};

struct LayoutInlineContext : LayoutNode {
    std::vector<std::unique_ptr<LayoutNode>> children;  // inline items
    std::vector<LineFragment> lines;                    // computed lines
    bool rtl = false;
    LayoutInlineContext() : LayoutNode(LayoutKind::InlineContext) {}
};
```

`LayoutKind` gains `Inline` and `InlineContext`.

`LayoutBox::children` can contain `LayoutBox` (block children) and `LayoutInlineContext` (anonymous blocks wrapping inline runs). The parent's block flow positions each child (block or inline-context) vertically in source order — same cursor_y accumulation as today.

### Algorithm

**Layout pass** (block container with children):

```
   for each child of block container:
     if child is a Text node OR child has display: inline:
       accumulate into current_inline_run
     else (block child):
       flush current_inline_run as LayoutInlineContext (if non-empty)
       layout child as LayoutBox
       advance cursor_y

   flush any trailing inline_run
```

**Inline formatting context construction**:

1. Flatten the `LayoutInlineContext`'s inline tree depth-first into a sequence of `(text, style)` items where `style = { font_family, font_size, color }`.
2. Split into words, preserving style per word.
3. Greedy line-pack like ADR-015, but each word measures with its own style.
4. Produce `LineFragment` for each word-group on a line, carrying that group's style.
5. x-positions: LTR at `content_x`, RTL at `content_x + content_w - line_width` (per line, not per fragment).
6. y-positions: cumulative line heights, same as ADR-015.

**Paint** (unchanged shape, updated data):

Paint already iterates `LayoutInlineContext::lines`, calling drawer per fragment. With style fields on fragments, `draw_line_with_pango` uses the fragment's own `font_family`/`font_size`/`color` instead of the parent `LayoutText`'s. `LayoutInline` is a structural node, not painted directly.

### Direction resolution and the ADR-015 divergence

The ADR-015 heuristic becomes one option among three:

- `dir="ltr"` — explicit, always LTR
- `dir="rtl"` — explicit, always RTL
- `dir="auto"` — heuristic (what ADR-015 did unconditionally)
- **missing** — inherit (the HTML spec default, which most pages rely on)

For the welcome page: no `dir` attribute on `<p>`, inherits LTR from `<html>`. Renders left-aligned, matching Firefox. **The ADR-015 divergence is resolved by making the heuristic opt-in.**

For `<html dir="rtl" lang="ar">` — the standard Arabic site pattern — everything inherits RTL, paragraphs render right-aligned. Correct.

For `<p dir="auto">هذا نص عربي. Rendered via Pango.</p>` — the heuristic fires, renders RTL. Opt-in.

That covers all three real-world cases without a heuristic-forced default.

## Rationale

### 1. Inline formatting is the biggest structural gap left

The `docs/current-state.md` gap list has one entry bigger than everything else: "Inline formatting (`<span>`, `<a>`, `<em>`) — layout is block-only." Real HTML uses inline elements constantly. A page with `<p>Check <a href="...">our docs</a> for more info.</p>` renders as three stacked blocks in HoneyBee. Fixing that is the difference between "renders pages" and "renders pages that look like pages."

### 2. `display` is the right abstraction

The alternative — hardcode "tag X is inline" in `layout.cpp` — would work but is architecturally wrong. It would:
- Deny authors `span { display: block }` (a common reset override)
- Prevent `display: none` from being added later as a property
- Force a code change for every new inline element

Making `display` a cascade property is the same pattern ADR-014 used for UA defaults — one architectural decision, extended incrementally via UA stylesheet updates.

### 3. Flattening inline content for line breaking is what browsers do

Chromium and WebKit both flatten inline content into a list of inline items when computing line boxes. The tree structure is preserved (for debugging, accessibility, DOM-level operations), but line breaking sees a flat sequence. That's necessary because a word can't respect inline element boundaries — a `<em>world</em>` and surrounding text flow together.

The alternative — keeping the inline tree intact and doing line breaking per text node — would fail on mixed inline content. `<p>Hello <em>world</em>!</p>` would break "Hello" and "world" as separate line-breaking units, and the space between them would be ambiguous.

### 4. `dir` attribute is the escape hatch for the ADR-015 divergence

ADR-015 documented that the first-strong heuristic diverges from Firefox. The fix isn't "remove the heuristic" (worse for Arabic-only pages) or "refine the heuristic" (still wrong for edge cases). The fix is what HTML actually provides: **`dir` attribute for explicit control**.

ADR-016 makes the heuristic opt-in via `dir="auto"`, defaults to inherit-LTR otherwise, and gives authors `<html dir="rtl">` for Arabic pages. That's the standard HTML pattern, and it resolves the divergence without special-casing any specific page.

## Alternatives Considered

### Hardcode inline/block in `layout.cpp`

**Rejected.** Denies author `display` overrides, requires a code change for every element, and closes the door on `display: none` later. Making `display` a cascade property is the architecturally correct choice.

### Keep line breaking per-text-node

**Rejected.** Breaks the "Hello world !" flow case (see rationale 3). Line breaking must cross inline element boundaries.

### Do full Unicode Bidi (UAX #9) in v1

**Rejected.** Full bidi is a separate concern, hundreds of lines, and none of the current test cases need it. v1's `dir` + first-strong-at-context-level covers the common cases. Deferred to a bidi ADR.

### Baseline alignment in v1

**Deferred.** v1 top-aligns fragments within a line. That's correct for mixed inline content with uniform font-size. It's visibly off only when a `<span style="font-size: 24px">` is inside a smaller-font paragraph. Since font-size overrides inside inline elements are rare in practice (and font-style/font-weight don't render at all yet), top-alignment is acceptable for v1. Follow-up ADR when we test real pages with mixed font sizes.

### Skip `dir` attribute, keep ADR-015's heuristic

**Rejected.** That's the state ADR-015 shipped and documented as divergent from Firefox. Fixing it now, while inline formatting is being restructured, is cheaper than doing it as a separate ADR later.

### `display: none` in v1

**Deferred.** Would retire the `<head>` skip list in `layout.cpp`, which is a nice cleanup but not core to inline formatting. Separate ADR.

## Consequences

### Files changed

- `src/renderer/layout/layout_box.hpp` — `Display` enum, `LayoutKind::Inline` + `LayoutKind::InlineContext`, `LayoutInline` and `LayoutInlineContext` node types, `LineFragment` gains style fields
- `src/renderer/layout/layout.cpp` — `display` reading, inline run collection, `LayoutInlineContext` construction, direction resolution from `dir` attribute, per-element inline structure
- `src/renderer/layout/text_utils.cpp` — `break_into_lines` gains per-word style, flattens inline tree before line breaking
- `src/renderer/paint/paint.cpp` — iterate `LayoutInlineContext::lines`, use fragment's own style
- `src/browser/pango_text.cpp` — `draw_line_with_pango` uses fragment's style fields
- `src/renderer/css/ua_stylesheet.cpp` — add `display: inline` for 19 inline elements
- `tests/test_layout.cpp` — inline formatting, mixed inline/block, direction resolution via `dir` attribute
- `tests/test_paint.cpp` — fragment-per-style painting
- `tests/test_ua_stylesheet.cpp` — inline default tests

No changes to `renderer::dom`, `renderer::css` (except UA stylesheet content), `renderer::html`.

### Behavior examples

| HTML | Before | After |
|---|---|---|
| `<p>Hello <em>world</em>!</p>` | 3 stacked blocks | 1 line: "Hello world!" |
| `<p>Hello <em>world</em>!</p>`, container 80px | 3 stacked blocks | 2 lines: "Hello", "world!" |
| `<p dir="ltr">هذا نص</p>` | right-aligned (heuristic) | left-aligned (explicit) |
| `<html dir="rtl"><p>هذا نص</p></html>` | right-aligned (heuristic) | right-aligned (inherited) |
| `<p dir="auto">هذا نص</p>` | right-aligned (heuristic) | right-aligned (heuristic) |
| `<span>inline</span>` inside `<p>` | separate block | inline with paragraph text |
| `<p><div>block</div>text</p>` | 2 stacked blocks | still 2 stacked blocks (inline run anonymous) |

### What layout changes

- Block flow algorithm unchanged (cursor_y accumulation)
- Box model unchanged
- **New**: inline runs collected per block container; `LayoutInlineContext` construction; direction resolution from `dir` attribute; per-word style in line breaking

### What paint changes

- Still iterates `LayoutInlineContext::lines`
- `draw_line_with_pango` uses fragment's `font_family`/`font_size`/`color` instead of parent `LayoutText`'s
- No structural change to `paint()` or `paint_box`

## Open Questions

### Block inside inline

`<span><div>block</div></span>` — a block element inside an inline element. Browsers handle this by "splitting" the inline around the block (the inline becomes two inline segments plus the block between them). That's complex.

v1: **fallback to block**. If an inline element contains a block element, the inline element is treated as block for layout purposes. This avoids a whole class of complexity and produces a reasonable result (span becomes div-like). Documented as a known limitation.

### Anonymous block for mixed content

`<div>text <p>block</p> more text</div>` — the two inline runs ("text " and " more text") become anonymous block containers that stack around the `<p>`. That's what ADR-016's "flush inline run as LayoutInlineContext" does. Nothing extra needed, but worth noting as the mechanism that handles this case.

### Empty inline elements

`<span></span>` — no content, no fragments, but a `LayoutInline` node exists in the tree. v1: allowed, produces no lines, zero-height context. Harmless.

### Inline element styles

v1 carries `font_family`, `font_size`, `color` on fragments. `font_weight`, `font_style`, `text_decoration` are declared in the UA stylesheet (ADR-014) but not rendered (paint doesn't support them). A future font-metrics ADR will consume them. When that lands, `<em>` will become visually italic and each fragment will know its weight/style.

### `dir` inheritance through block boundaries

`<html dir="rtl"><div><p>text</p></div></html>` — the `dir` inherits through every level. v1: `resolve_direction` walks up the parent chain by default (via `parent` argument), so inheritance works as expected even across block boundaries. Only `dir="auto"` stops inheritance (uses local heuristic instead).

### Performance

Flattening inline trees and per-word measurement is O(n) in word count, same order as ADR-015. No new performance concerns at our scale.

## Record of Changes

- **2026-10-21:** Initial decision.
