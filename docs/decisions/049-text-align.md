# ADR-049: Text Align & Spacing (Measured, Deferred)

**Status:** Deferred
**Date:** 2026-10-29
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-016 (inline formatting), ADR-015 (line breaking), ADR-029 through ADR-048 (measurement-first discipline)

## Context

Text typography properties -- `text-align`, `letter-spacing`,
`word-spacing` -- are missing from our CSS engine. This ADR
records the measurement done before writing any implementation,
and the decision the measurement produced.

### Measurement (Wikipedia CSS, 2026-10-29)

**`letter-spacing`: 0 rules. `word-spacing`: 0 rules.** Neither
property appears in the target page's CSS at all. There is
nothing to implement for either.

**`text-align`: 26 rules.** Full breakdown by value:

| Value | Uses | Renderable delta? |
|---|---|---|
| `left` | 9 | No -- same as default |
| `start` | 2 | No -- left in LTR (all target text is LTR) |
| `center` | 9 | Only in second-viewport or empty elements |
| `right` | 5 | Only in second-viewport |
| `justify` | 1 | **`@media print` only** |

**The single `text-align: justify` rule** (context examined,
position 163907 in the CSS):

```css
/* inside @media print { ... } */
p {
    font-size: 12pt;
    line-height: 16pt;
    margin-top: 5px;
    text-align: justify;
}
p::before {
    content: '';
    display: block;
    width: 120pt;
    overflow: hidden;
    page-break-after: avoid;
}
```

Three independent print signals confirm this is print-only:
- `pt` units (12pt/16pt body text -- screen uses `px`/`rem`)
- `page-break-after: avoid` (print pagination)
- `120pt` width (print layout dimension)

**The 14 `center` / `right` uses** target:
- `.wikitable th` -- table headers (second-viewport)
- `figure figcaption` -- image captions (second-viewport)
- `.infobox` variants -- infobox titles/headers/images
  (second-viewport)
- `#footer-icons li` -- footer (bottom of page)
- `#siteNotice` -- usually empty
- `.center` utility class -- unused in current HTML

**Zero first-viewport targets. Zero scroll-verifiable targets
reachable before the first article table or figure.**

### Why this is Deferred rather than Implemented

Implementing `text-align` and `letter-spacing`/`word-spacing`
would produce:

- **`letter-spacing` / `word-spacing`**: no code to write; no
  CSS uses them
- **`text-align: left` / `start`**: no behavior change (default)
- **`text-align: justify`**: `@media print` only; our engine
  does not render print output; zero delta
- **`text-align: center` / `right`**: second-viewport targets

Implementing would add ~150-250 lines (line-fragment text-align
support in `break_into_lines` and `break_styled_words_into_lines`,
plus paint-side alignment), plus a `TextMeasurer` signature
change to thread `letter_spacing` / `word_spacing` through
Pango, **for zero visible consumer**.

This is materially different from ADR-047 and ADR-048, which
shipped correct infrastructure awaiting a *specific, verified*
future consumer (ADR-029 header layout, `.mw-header` element
appearing in HTML). ADR-049 has no identified future consumer:
the properties are simply absent or print-gated in the target
page.

## Decision

**Defer implementation until a target page has a
first-viewport-visible or scroll-visible `text-align` /
`letter-spacing` / `word-spacing` usage that our engine cannot
currently render.**

This ADR serves as a permanent record so a future maintainer
considering `text-align` sees the measurement, not just absence.

### What would reactivate ADR-049

Any of:

1. **A target page uses `letter-spacing` or `word-spacing`** on
   elements visible in the first viewport. (Currently 0 uses in
   Wikipedia CSS.)
2. **Wikipedia changes `p { text-align: justify }` to top-level
   or `@media screen`**. Print-styling for body text moved to
   screen would create first-viewport delta on every article.
3. **A future ADR renders print output** (`@media print` gated
   rules reaching cascade). At that point `p { justify }`
   becomes renderable if the print path exists.
4. **A specific rendered element needs `text-align: center|right`**
   -- currently all targets are second-viewport; if a
   first-viewport target appears, implement scoped to that.

## Implementation sketch (for future reference)

If reactivated, the implementation shape (estimated 150-250
lines) would be:

### Parse layer (small)

`text-align`, `letter-spacing`, `word-spacing` already parse
into `ComputedStyle` via the cascade (any property parses -- no
special handling needed). No parse changes.

### Line-fragment layer (medium)

`LineFragment` gains:
```cpp
enum class TextAlign : std::uint8_t { Left, Center, Right, Justify };
TextAlign text_align = TextAlign::Left;
```

`break_into_lines` and `break_styled_words_into_lines` gain
`text_align` parameter. After computing line fragments and
line width:

- **`left`**: no change (current behavior)
- **`center`**: shift each fragment by `(content_w - line.width) / 2`
- **`right`**: shift each fragment by `content_w - line.width`
- **`justify`**: for all lines except the last, distribute
  `(content_w - line.width)` across word boundaries (requires
  word-boundary tracking through the fragment splitter)

`justify` is the load-bearing complexity. Multi-word line
splitting needs to know which gaps are inter-word (justifiable)
vs intra-word (not). Current `LineFragment` merges adjacent
same-style words into one fragment -- would need either
per-word fragments (larger structure) or a per-fragment
"word boundaries" side list.

### Spacing layer (larger -- the reason this is deferred)

`letter-spacing` / `word-spacing` require:

1. `TextMeasurer` signature change:
   ```cpp
   using TextMeasurer = std::function<std::pair<float, float>(
       std::string_view text, std::string_view family,
       float font_size,
       float letter_spacing,   // NEW
       float word_spacing)>;   // NEW
   ```
2. **~10 call sites** updated: `layout_element`, `break_into_lines`,
   `break_styled_words_into_lines`, `measure_with_pango`,
   `render_pipeline.cpp`, all test helpers (`fixed_measurer`
   in `test_layout.cpp`, `test_pseudo_layout.cpp`, etc.)
3. `measure_with_pango` sets Pango attributes
   (`pango_attr_letter_spacing_new`,
   `pango_attr_word_spacing_new`) before measuring
4. `draw_line_with_pango` sets the same attributes before
   painting

Threading two new parameters through the entire text pipeline
is the bulk of the cost, and it is only justified when a
target page actually uses the properties.

### Paint layer (small after the above)

`draw_line_with_pango` applies the same attributes it measures
with. Zero new paint logic.

## Consequences

**Better (from deferring):**
- **No speculative code** -- `letter-spacing` / `word-spacing`
  are zero-use; `text-align` targets are print-gated or
  second-viewport
- **No `TextMeasurer` signature churn** (~10 call sites)
  without a consumer
- **The measurement is permanently recorded** -- a future
  maintainer finds this ADR, sees the 0/0/26 breakdown, and
  skips the same investigation

**Accepted cost:**
- The engine remains without `text-align` support, so if a
  future target page uses it, that page renders left-aligned
  incorrectly until implementation

**Unchanged:**
- All ADR-015 through ADR-048 behavior
- Text measurement pipeline
- Everything else

## The sixth verification category

ADR-049 establishes a sixth category in the session's decision
ledger:

| Category | Example | Outcome |
|---|---|---|
| PNG-verified (first-viewport) | ADR-042, ADR-043 | shipped with visible delta |
| PNG-verified (scroll) | ADR-045 | shipped with scroll delta |
| Behaviorally-complete, visually-subtle | ADR-044 | shipped |
| Behaviorally-complete, visually-zero | ADR-047, ADR-048 | shipped, consumer pending |
| Deferred with reason | ADR-041 Commit 2 | code deferred |
| **Measured, no target, deferred (this ADR)** | **ADR-049** | **no code written** |

The distinction from "Deferred with reason": ADR-041 Commit 2
had a *reason to defer a specific implementation step*
(needed absolute positioning). ADR-049 defers the *entire
feature* because measurement showed no target exists to
implement against. Recording that decision is the deliverable.

## Acceptance criteria (for a decision record)

ADR-049 is complete when:

- [x] Measurement recorded (text-align 26, letter-spacing 0,
      word-spacing 0)
- [x] The one `justify` rule identified as `@media print`
- [x] The 14 center/right targets enumerated as
      second-viewport or empty
- [x] Reactivation criteria defined (4 specific conditions)
- [x] Implementation sketch recorded (for future reference)
- [x] Sixth verification category named
- [ ] ADR index row added
- [ ] CHANGELOG entry

**Explicit claim**: `text-align`, `letter-spacing`, and
`word-spacing` produce zero first-viewport delta in the target
page. No implementation written. Decision recorded so the
measurement is not repeated.

**Honest caveats:**
1. **No code shipped** -- this is a decision record only
2. **`letter-spacing` / `word-spacing` are zero-use** in the
   target CSS
3. **`text-align: justify` is print-only** -- 4 independent
   print signals confirm
4. **All center/right targets are second-viewport or empty**
5. **Reactivation requires a real target** -- one of the 4
   listed conditions in "What would reactivate ADR-049"
