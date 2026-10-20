# ADR-011: The First Paint

**Status:** Accepted
**Date:** 2026-10-20
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-001 (technology stack), ADR-006 (return to ADR-001), ADR-007 (DOM), ADR-010 (cascade)

## Context

We have completed the three pillars of the CSS pipeline:

- **ADR-007**: an owned DOM tree
- **ADR-008/009**: selector and stylesheet parsing
- **ADR-010**: the cascade, which produces a `ComputedStyle` per element

For the first time, `h1 { color: red }` is now a fact about a concrete `dom::Element`. But nothing is drawn yet. The window still shows raw text via `make_preview`.

This ADR decides the first rendering pipeline: how to turn a `ComputedStyleMap` into pixels.

### Diagram conventions

All diagrams in this ADR are ASCII, in fenced code blocks. They render identically on GitHub, on GitLab, in `cat` output, and in `git diff`. Mermaid was considered for the pipeline flow but rejected: it renders only on web UIs, shows as raw text in plain markdown views, and requires a renderer to be meaningful. For spatial/structural concepts (box model, stacking, worked example), ASCII is also clearer than any flowchart DSL. Future ADRs should follow this convention.

## Decision

We implement a **two-stage pipeline**: **layout** then **paint**.

### Pipeline

```
HTML bytes             CSS text
    |                     |
    v                     v
[html::parse]       [Stylesheet::parse]
 (ADR-007)             (ADR-009)
    |                     |
    v                     v
dom::Document         Stylesheet
    |                     |
    +----------+----------+
               |
               v
           [cascade]
           (ADR-010)
               |
               v
       ComputedStyleMap
               |
               v
           [layout]
          (this ADR)
               |
               v
           LayoutTree
               |
               v
           [paint]
          (this ADR)
               |
               v
          Cairo pixels
```

### Stage 1: Layout

Produce a **layout tree** parallel to the DOM, with a `LayoutBox` per element and a `LayoutText` per text node.

```cpp
struct LayoutBox {
    float x = 0, y = 0;              // absolute pixels, viewport origin
    float width = 0, height = 0;     // border-box dimensions
    float margin[4] = {0,0,0,0};     // top, right, bottom, left
    float border[4] = {0,0,0,0};
    float padding[4] = {0,0,0,0};
    const dom::Element* element = nullptr;
    std::vector<std::unique_ptr<LayoutNode>> children;
};

struct LayoutText {
    float x = 0, y = 0;
    float width = 0, height = 0;
    std::string text;
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();
};

using LayoutNode = std::variant<LayoutBox, LayoutText>;
```

**The box model.** Every `LayoutBox` has three nested rings around its content:

```
   outer edge of margin box
   |
   v
   +--------------------------------------+
   |            margin (transparent)      |
   |   +-------------------------------+  |
   |   |          border (colored)     |  |
   |   |   +-----------------------+   |  |
   |   |   |   padding (background)|   |  |
   |   |   |   +---------------+   |   |  |
   |   |   |   |    content    |   |   |  |
   |   |   |   |               |   |   |  |
   |   |   |   +---------------+   |   |  |
   |   |   +-----------------------+   |  |
   |   +-------------------------------+  |
   +--------------------------------------+

   content-width  = width of the content box
   border-width   = content-width + padding-left + padding-right
                                + border-left  + border-right
   margin-width   = border-width + margin-left + margin-right
```

In v1, `LayoutBox::width` and `LayoutBox::height` describe the **border-box** (content + padding + border). Margin is stored separately and applied by the *parent* when stacking.

**Block flow stacking.** Vertical position is computed by accumulating heights and margins:

```
   parent content area (top edge at y = 0)

      +------------------------------------+
      |                                    |
      |  (mA = margin-top of A)            |
      |  +------------------------------+  |  <- y_A
      |  |  A border-box (height hA)    |  |
      |  +------------------------------+  |
      |  (mA' = margin-bottom of A)        |
      |  (mB  = margin-top of B)           |
      |  +------------------------------+  |  <- y_B
      |  |  B border-box (height hB)    |  |
      |  +------------------------------+  |
      |  (mB' = margin-bottom of B)        |
      +------------------------------------+

   y_A = mA
   y_B = y_A + hA + mA' + mB

   Note: v1 does NOT collapse adjacent vertical margins.
         Real CSS would merge A's bottom margin with B's top
         margin into max(mA', mB). That is deferred (see below).
```

**The layout algorithm for v1:**

1. **Block flow only.** Every element is treated as `display: block`. Block boxes stack top-to-bottom inside their parent's content area.
2. **Box model applied in order**: content area is what remains after subtracting margin, border, and padding from the parent's content width. Vertical stacking uses `previous.bottom + previous.margin_bottom + this.margin_top` for the y offset (no margin collapse).
3. **Width fills parent.** A block box's width = parent content width − (own margin-left + border-left + padding-left + padding-right + border-right + margin-right). No intrinsic width calculation in v1.
4. **Height is derived**, not intrinsic: `height = content_height + padding_top + padding_bottom + border_top + border_bottom`, where `content_height` is the sum of children's heights plus their gaps.
5. **Text nodes become `LayoutText`**. Measured via **Pango**, single line per text node in v1. If Pango says the line is wider than the parent's content width, it is allowed to overflow rather than wrap (wrapping is deferred).
6. **Box model values from computed style**: `margin`, `padding`, `border-width` are parsed as pixel values (`"10px"` → `10.0f`). Missing or non-pixel values default to `0`. `width` and `height` override the fill-parent behavior if present.

**Recursive structure.** The algorithm is a single pre-order recursion:

```
   layout_node(node, x, y, parent_content_width) -> LayoutNode
   |
   +-- if node is TextNode:
   |     (w, h) = measurer(text, font)
   |     return LayoutText(x, y, w, h, text)
   |
   +-- if node is Element:
   |     m  = parse_px(style, "margin")         (each side)
   |     b  = parse_px(style, "border-width")
   |     p  = parse_px(style, "padding")
   |     w  = parse_px(style, "width")   or fill parent
   |
   |     content_x = x + m.left + b.left + p.left
   |     content_y = y + m.top  + b.top  + p.top
   |     content_w = parent_content_width
   |                 - (m.left  + b.left  + p.left)
   |                 - (m.right + b.right + p.right)
   |                 (override by w if explicit)
   |
   |     cursor_y = content_y
   |     children = []
   |     for each child in node.children:
   |         child_layout = layout_node(child, content_x, cursor_y, content_w)
   |         cursor_y     = child_layout.bottom + child_layout.margin_bottom
   |         children.append(child_layout)
   |
   |     box.width  = content_w + p.left + p.right + b.left + b.right
   |     box.height = (cursor_y - content_y) + p.top + p.bottom
   |                                            + b.top + b.bottom
   |     return LayoutBox(x, y, box.width, box.height, children)
```

### Stage 2: Paint

Walk the layout tree in document order, and for each node emit draw commands to **Cairo**:

1. For each `LayoutBox`: fill its border-box background if `background-color` is set; then draw its border sides if `border-width` is non-zero; then recurse into children clipped to the padding area.
2. For each `LayoutText`: `cairo_move_to` to `(x, y + ascent)`, set the font via Pango's Cairo integration, then `cairo_show_text` (or `pango_cairo_show_layout`).

The output is a `Gtk::DrawingArea::set_draw_func` callback that:
1. Parses the HTML → `dom::Document` → `Stylesheet::parse` → `cascade()` → `layout()` → `LayoutBox` root.
2. On each draw call, walks the layout tree and draws via Cairo.

### Wiring into the existing window

`browser::window` gains:
- A `Gtk::DrawingArea` child that replaces the current `Label` preview.
- A `render_document(html)` method that runs the full pipeline and stores the resulting `LayoutBox` root.
- A draw handler that paints the layout tree.

## Rationale

### 1. Two stages, not one

Chromium, Firefox, and WebKit all separate layout from paint. Reasons:

- **Repaint without relayout**: scrolling or hover changes paint only; the layout tree is reused.
- **Testability**: layout is pure computation (`ComputedStyleMap → LayoutBox`), paint is I/O (Cairo calls). Layout is unit-testable; paint is not.
- **Incremental refinement**: adding flexbox later changes the layout algorithm only. Adding gradients changes the paint algorithm only.

Collapsing them into one pass is a common beginner mistake. It works for a toy, then becomes unmaintainable.

### 2. Block flow only in v1

A correct block flow algorithm is roughly 100 lines. It is the foundation of every layout algorithm — even flexbox and grid use block flow internally for their children. Getting block flow right first, then adding inline, then flex, is the standard learning path (see "Let's build a browser engine!" by Matt Brubeck, and Servo's early history).

The alternative — "implement all of CSS layout in one go" — is what kills hobby browsers before they render a single pixel.

### 3. Pango for text measurement

Three options were considered:

- **Fixed-width estimate** (`chars * 8px`): trivial, wrong for Arabic, wrong for any non-Latin font, wrong for kerning.
- **Cairo text extents** (`cairo_text_extents`): has no Arabic shaping, so Arabic renders as isolated letterforms, not connected.
- **Pango via `pango_cairo_create_layout`**: correct Arabic shaping, correct complex scripts, correct ligatures. Already linked via GTK.

The project's mission is "Fast websites for Egypt." Arabic correctness is not optional. Pango is the correct choice.

### 4. Cairo for drawing

Already in the tech stack (ADR-001). Works directly with `Gtk::DrawingArea`. Pango integrates via `pango_cairo_show_layout`.

### 5. Deferring the hard parts

The following are explicitly deferred, so we do not sink weeks into them before the pipeline works end-to-end:

- **Inline formatting** (`display: inline`) — needed for `<span>`, `<a>`, `<em>`, and for text flow inside a paragraph
- **Text wrapping** — currently single-line per text node
- **Line breaking** — no `line-height` propagation across wrapped lines
- **Margin collapse** — adjacent vertical margins stack additively in v1; real CSS collapses them to `max()`
- **Shorthand expansion** — `margin: 10px 20px` stays as a raw string, interpreted as `0` in v1 (only single-value `Npx` shorthand is read)
- **Floats, flexbox, grid** — block flow only
- **`position: relative/absolute/fixed`** — all treated as `static`
- **`overflow` and clipping** — no scrolling, no overflow boxes
- **Images, tables, lists** — no special layout
- **Bidi and RTL** — Arabic text renders correctly via Pango, but RTL paragraph direction is not yet propagated from `dir="rtl"` or `direction: rtl`
- **UA stylesheet** — no default `h1 { font-size: 2em }`. A plain `<h1>` renders at 16px in v1. The UA stylesheet is a separate future ADR.
- **Computed value typing** — every style read is `"10px"` parsed by `std::stof`. A typed `Length` comes later.

## Alternatives Considered

### Skip layout, draw text directly

**Rejected.** Would produce pixels but prove nothing about CSS. Any nonzero margin, padding, or background would be impossible. The point is to build a browser, not a text viewer.

### One-stage render

**Rejected.** No repaint-only path, no testability, no incremental growth. See rationale 1.

### Use a third-party layout library

**Rejected.** No mature C++ layout library exists that we could vendor without bringing an enormous dependency tree. Layout is the interesting part to build.

### Full CSS layout (inline + block) in one ADR

**Rejected.** Inline layout (creating line boxes, breaking lines, bidi reordering, whitespace collapsing) is a book-length spec. Doing it in one ADR would delay the first pixel by months.

### Fixed-width text measurement

**Rejected.** Visibly wrong for Arabic, wrong for kerning, wrong for anything but monospace Latin. Pango is already linked.

## Consequences

- New module `renderer/layout/`:
  - `layout_box.hpp` — `LayoutBox`, `LayoutText`, `LayoutNode`
  - `layout.cpp` — `LayoutBox layout(const dom::Document&, const ComputedStyleMap&, TextMeasurer)`
  - `layout_box.cpp` — helper methods
- New module `renderer/paint/`:
  - `paint.hpp` — `void paint(Cairo::RefPtr<Cairo::Context>, const LayoutNode&)`
  - `paint.cpp`
- New types in `renderer/layout/`: `Color` (RGBA floats), `Length` (float px)
- `src/browser/window.cpp` — replace preview `Label` with `Gtk::DrawingArea`, wire the pipeline
- `tests/test_layout.cpp` — unit tests for block flow, box model, Pango-less layout (mock text measurement via a fixed-size callback)
- No new lexbor includes. `renderer::dom` and `renderer::css` untouched.

### Text measurement testing strategy

Layout is pure computation except for text measurement, which calls Pango. To make layout testable without GTK, layout takes a **measure callback**:

```cpp
using TextMeasurer =
    std::function<std::pair<float, float>(const LayoutText&)>;

LayoutBox layout(const dom::Document&, const ComputedStyleMap&,
                 TextMeasurer measurer);
```

- In `window.cpp`: a real measurer wrapping Pango.
- In tests: a fake measurer (`width = chars * 8`, `height = 16`) that produces deterministic results.

This is the standard seam for testing rendering pipelines without a display server.

### Behavior examples (v1)

The ADR's behavior table, visualized. Input:

```
   HTML:  <body><p>Hello</p><p>World</p></body>
   CSS:   p { margin: 10px; background: red; }
   Viewport: 800px wide
```

Resulting layout (sketch, not to scale):

```
     +-------------------------------------+  y=0   <- body border-box top
     |  (margin-top of p1 = 10px)          |
     |  +-------------------------------+  |  y=10  <- p1 border-box top
     |  |  p1 (red background)          |  |
     |  |  Hello                        |  |
     |  +-------------------------------+  |  y=26  <- p1 border-box bottom
     |  (p1 margin-bottom = 10px)          |
     |  (p2 margin-top    = 10px)          |
     |  +-------------------------------+  |  y=46  <- p2 border-box top
     |  |  p2 (red background)          |  |
     |  |  World                        |  |
     |  +-------------------------------+  |  y=62  <- p2 border-box bottom
     |  (p2 margin-bottom = 10px)          |
     +-------------------------------------+  y=72  <- body border-box bottom

   Key numbers:
     p1.y      = 10            (its own margin-top)
     p1.height = 16            (one line of text)
     p2.y      = 46            (p1.bottom 26 + 10 + 10)
     p2.height = 16
     body.h    = 72            (p2.bottom 62 + p2 margin-bottom 10)

   Note: v1 does NOT collapse adjacent margins.
         Real CSS would merge p1's 10px bottom margin with p2's
         10px top margin into a single 10px gap (y=36 instead of 46).
         That is deferred per ADR-011's scope.
```

Other behavior examples:

| HTML | CSS | Expected layout |
|---|---|---|
| `<p>Hello</p>` | (none) | one box, x=0, y=0, width=viewport_width, height=16 |
| `<div><p>A</p><p>B</p></div>` | (none) | div wraps two p's, each 16px tall, stacked |
| `<p style="margin: 10px">Hi</p>` | (none) | p at y=10, width = viewport − 20 |
| `<p style="width: 100px">Hi</p>` | (none) | p width = 100px + padding + border |
| `<p style="background: red">Hi</p>` | (none) | red rect behind text |

## Open Questions

### Viewport

Where does the root layout box get its width? In v1, the `Gtk::DrawingArea`'s `get_width()`. That ties layout to the window size, so a resize triggers a full relayout. That is fine at our scale; a follow-up ADR can add incremental relayout.

### CSS shorthand

`margin: 10px 20px` (two values) is not handled in v1. The layout reads single-value `"Npx"` only, so `margin: 10px` works but `margin: 10px 20px` defaults to `0`. Shorthand expansion is deferred to a future ADR (before or alongside flexbox).

### Colors

`color`, `background-color`, `border-color` accept CSS named colors (`red`, `navy`) in v1. Hex colors (`#fff`, `#ffffff`) are also supported. `rgb()` and `hsl()` are deferred.

### Fonts

Default font is the system sans (via Pango). `font-family`, `font-size`, `font-weight`, `font-style` are read from computed style if present. Emoji rendering via Pango works out of the box.

### Bidi and RTL

Pango handles Arabic shaping and RTL rendering correctly **within a single line**. Direction detection from `dir="rtl"` on the `<html>` element is deferred. v1 renders all text LTR-aligned but with correct Arabic letterforms.

## Record of Changes

- **2026-10-20:** Initial decision.
