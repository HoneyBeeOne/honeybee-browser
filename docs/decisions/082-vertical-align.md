# ADR-082: CSS vertical-align and baseline-aware inline layout

- **Status:** Accepted
- **Date:** 2026-11-20
- **Related ADRs:** ADR-015, ADR-016, ADR-029, ADR-081

## Context

ADR-016 established the initial inline formatting model. ADR-081 added
atomic inline-block participation in inline flow. The current implementation
still positions all fragments on a line using the same top coordinate and
uses fragment heights to determine line height.

This is insufficient for CSS vertical alignment. Text runs with different
font metrics, inline elements with different font sizes, and inline-block
elements need to share a common baseline. Their vertical positions must be
resolved relative to that baseline.

The current text measurement interface returns only width and height:

```cpp
using TextMeasurer = std::function<std::pair<float, float>(
    std::string_view text,
    std::string_view font_family,
    float font_size)>;
```

It does not expose the baseline or font metrics needed for baseline-aware
layout.

Nested inline elements also need to retain their own computed styles.
The current flattening implementation applies font family and font size
uniformly through an inline run, which prevents correct handling of
mixed-size inline descendants.

## Decision

Introduce baseline-aware inline layout and implement the CSS
`vertical-align` property for inline-level elements.

The implementation will be staged. It must preserve existing inline flow,
whitespace handling, line breaking, RTL behavior, and inline-block behavior.

### 1. Text and font metrics

Text measurement and font metrics are related but distinct concepts.

Text measurement is specific to a text run and must provide:

- Text width.
- Text height.
- Baseline position measured from the top of the measured text.

Font metrics describe a font configuration and must provide:

- Ascent.
- Descent.
- An x-height estimate.
- A normal line-height estimate.

Use separate structures:

```cpp
struct TextMetrics {
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
};

struct FontMetrics {
    float ascent = 0.0f;
    float descent = 0.0f;
    float x_height = 0.0f;
    float normal_line_height = 0.0f;
};
```

The text measurement callback should return `TextMetrics`. A separate
font-metrics provider should return `FontMetrics` for a font family and
size. The exact callback signatures may be refined during implementation.

The Pango backend should obtain the layout baseline using
`pango_layout_get_baseline()` and convert Pango units using `PANGO_SCALE`.

Font ascent, descent, and recommended line height should come from Pango
font metrics.

Pango does not directly expose a CSS x-height value through its basic font
metrics structure. Estimate it using the glyph metrics for a lowercase
`x`, with a documented fallback when unavailable.

Cache font metrics by the relevant font configuration (at least family
and size) so that they are not repeatedly recomputed for every word.

### 2. Line-height resolution and inheritance

The layout engine must distinguish the three CSS line-height forms:

- `normal`
- A unitless number
- A length or percentage

These forms have different inheritance behavior.

A unitless number is inherited as a multiplier and is applied using the
descendant's font size. A length or percentage is resolved on the element
where it is declared and inherited as an absolute computed length.

For example, with a parent font size of 20px:

```css
.parent {
    font-size: 20px;
    line-height: 1.5;
}

.child {
    font-size: 10px;
}
```

The child uses a 15px line height.

By contrast:

```css
.parent {
    font-size: 20px;
    line-height: 150%;
}

.child {
    font-size: 10px;
}
```

The percentage resolves to 30px on the parent and that computed length
is inherited by the child.

The current cascade inheritance implementation copies raw declaration
strings. The implementation must therefore preserve enough information
to distinguish an inherited unitless number from an inherited computed
length. An explicit inherited-value marker or an equivalent typed
computed-value representation should be introduced.

For `normal`, use the font backend's recommended line-height metric as
the initial approximation. The implementation should document that
`normal` can be user-agent and font dependent.

The used line height determines the leading around the font's content
area. Distribute the leading equally above and below the baseline,
allowing negative leading where appropriate.

### 3. Preserving inline style boundaries

The current inline formatting pipeline uses `StyledWord` objects and
`StyledLine` results. The first implementation step should preserve and
extend this pipeline rather than replace the entire line-breaking engine.

The representation must retain the computed style context of each inline
element, including:

- The active inline element and its computed style.
- Font family and font size for each text run.
- Color and other currently supported text properties.
- The `vertical-align` value associated with each inline element.
- Atomic inline-block boxes.

Nested inline elements must be allowed to introduce their own font family,
font size, line height, and vertical alignment without losing the style
context of their ancestors.

A token stream with explicit inline-boundary events is one possible
implementation:

```cpp
enum class InlineItemKind {
    BeginInline,
    EndInline,
    TextRun,
    AtomicInlineBox
};
```

This representation is a design option, not a requirement to replace
`StyledWord` immediately. The implementation should choose the smallest
change that preserves the necessary inline ancestry and style information.

Line breaking must continue across inline-element boundaries. When a line
break occurs inside a nested inline element, the active inline ancestors
must continue correctly on the next line.

Whitespace collapsing, existing line-breaking behavior, and RTL handling
must be preserved.

### 4. vertical-align values

Support the following values for inline-level elements:

- `baseline`
- `sub`
- `super`
- `middle`
- `text-top`
- `text-bottom`
- `top`
- `bottom`
- Length values
- Percentage values

The initial value is `baseline`.

`vertical-align` is not inherited by default. An explicit CSS `inherit`
value must still be handled according to the cascade.

For length values, positive values raise the element and negative values
lower it.

For percentage values, the percentage is resolved against the element's
computed line height.

The alignment rules are:

- `baseline`: align the element's baseline with its parent's baseline.
- `sub` and `super`: shift the element down or up by a font-relative
  amount. Exact browser-specific offsets may initially be approximated.
- `middle`: align the midpoint of the element with the parent baseline
  plus half the parent's x-height.
- `text-top` and `text-bottom`: align with the parent element's text
  top or bottom.
- `top` and `bottom`: align the aligned subtree with the line box's
  top or bottom. These require line-level resolution and should be
  implemented after baseline alignment is working.
- Length and percentage values: shift the element relative to the
  parent's baseline.

The implementation must use a consistent coordinate convention.
In the renderer's usual downward-positive coordinate system, raising
an element decreases its Y coordinate.

### 5. Inline-block baseline

An inline-block participates in the surrounding line as an atomic inline
box.

Its baseline should follow CSS 2.1 behavior:

- If it has in-flow line boxes and its overflow is visible, use the
  baseline of its last in-flow line box.
- Otherwise, use its bottom margin edge.

The inline-block layout result must expose the selected baseline relative
to the position used by its parent inline formatting context.

Margins must remain part of the atomic box's contribution to line layout.

### 6. Baseline-aware line layout

The current approach assigns the same Y coordinate to every fragment in
a line. Replace this with a baseline-aware process.

Each line box must begin with a baseline strut derived from the
block container's font metrics and used line height. This strut ensures
that the line box retains the block container's normal line-height
contribution even when its contents use smaller fonts or have different
vertical alignment.

Inline participants then contribute their own ascent and descent
relative to the shared baseline. Their contributions may expand the
line box beyond the initial strut.

For non-replaced inline elements, the inline box's vertical contribution
must account for its own used line height and half-leading. Inline-block
and other atomic inline-level boxes contribute according to their
dimensions and baseline.

For each line:

1. Determine the baseline requirements of its inline-level participants.
2. Resolve baseline-relative shifts for text runs and inline-level boxes.
3. Determine the minimum ascent above the baseline and descent below it.
4. Account for each participant's used line height and leading.
5. Compute the line box dimensions.
6. Position each participant relative to the resulting baseline.
7. Resolve `top` and `bottom` alignment after the initial line box
   dimensions are known, repeating the necessary line-box calculation
   where required.

The line box must be large enough to contain all aligned participants.
Vertical alignment must not be implemented by simply changing fragment
heights while leaving their positions unchanged.

### 7. Staged implementation

Implementation is divided into two major phases.

#### Phase A: Baseline-aware inline layout

1. Extend text measurement with baseline metrics.
2. Introduce a font-metrics provider and caching.
3. Introduce typed line-height resolution and correct inheritance behavior.
4. Preserve nested inline style boundaries in the existing inline pipeline.
5. Implement baseline alignment for text runs with different font sizes.
6. Expose and use inline-block baselines.
7. Add regression tests for line height, whitespace, wrapping, and RTL.

At the end of this phase, inline content must be laid out using a shared
baseline even when its participants have different font sizes.

#### Phase B: CSS vertical-align

1. Implement the default `baseline` behavior.
2. Implement length and percentage offsets.
3. Implement `sub` and `super`.
4. Implement `middle`, `text-top`, and `text-bottom`.
5. Implement `top` and `bottom` using line-level resolution.
6. Add rendering tests for representative alignment combinations.

Each stage should have focused tests before proceeding to the next.
The baseline model must remain independently testable without relying
on non-default `vertical-align` values.

## Scope

This ADR covers horizontal writing mode and inline-level formatting.

The following are deferred:

- Table-cell vertical alignment.
- Vertical writing modes.
- Full OpenType baseline-table support.
- Advanced inline decoration and fragmentation behavior.
- Browser-specific typographic refinements beyond the initial documented
  approximations.

## Testing and acceptance criteria

Tests must cover at least:

1. Baseline alignment of text with different font sizes.
2. Inline elements with different font sizes.
3. Nested inline elements with independent styles.
4. `vertical-align: baseline`.
5. Positive and negative length offsets.
6. Percentage offsets based on the element's line height.
7. `vertical-align: sub` and `super`.
8. `vertical-align: middle` using the parent's x-height.
9. `vertical-align: text-top` and `text-bottom`.
10. `vertical-align: top` and `bottom`.
11. Inline-block baseline from the last in-flow line box.
12. Inline-block fallback baseline when it has no qualifying line box.
13. Inline-block margins and baseline placement.
14. Line-height `normal`.
15. Unitless line-height inheritance.
16. Length line-height inheritance.
17. Percentage line-height inheritance.
18. Mixed font sizes and line heights on the same line.
19. Multiple lines containing vertically aligned inline content.
20. Whitespace preservation and line wrapping.
21. RTL inline formatting regression tests.
22. Existing inline and inline-block layout tests.

Pixel-based rendering tests should be added for representative cases so
that baseline and vertical-position regressions are observable.

## References

- CSS 2.1, Section 10.8, Line height calculations:
  https://www.w3.org/TR/CSS2/visudet.html#line-height
- CSS 2.1, Section 10.8.1, Leading and half-leading:
  https://www.w3.org/TR/CSS2/visudet.html#leading
- CSS 2.1, Section 10.8.1, The `vertical-align` property:
  https://www.w3.org/TR/CSS2/visudet.html#propdef-vertical-align
- Pango Layout API:
  https://docs.gtk.org/Pango/method.Layout.get_baseline.html
- Pango FontMetrics API:
  https://docs.gtk.org/Pango/class.FontMetrics.html
- Pango Font glyph extents API:
  https://docs.gtk.org/Pango/method.Font.get_glyph_extents.html
