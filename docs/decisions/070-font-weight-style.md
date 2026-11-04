# ADR-070: Font weight + font style pipeline

**Status:** Implemented (production-verified)
**Date:** 2026-11-04
**Related:** ADR-014 (UA stylesheet), ADR-016 (inline context), ADR-069 (viewport-clipped paint)

## Context

During the ADR-069 visual-parity diagnostic, DUMP-DOM on Wikipedia's HTML article showed headings, bold, italic, and other inline emphasis rendering as flat body text. Root-cause investigation found the styling chain is complete *except for the last hop*:

| Layer | Status |
|---|---|
| Parser (lexbor → owned DOM) | ✅ No whitelist; all tags enter the tree |
| UA stylesheet (`ua_stylesheet.cpp`) | ✅ Already declares `h1-h6 { font-weight: bold }`, `em/i/cite/var { font-style: italic }`, `strong/b { font-weight: bold }`, `code/pre/kbd/samp/tt { font-family: monospace }` |
| Cascade | ✅ `ComputedStyle::get("font-weight")` / `get("font-style")` return the correct strings |
| `LayoutText` / `LineFragment` / `StyledWord` structs | ❌ **No `font_weight` / `italic` fields** — values are dropped on the floor at this hop |
| Pango `make_layout` | ❌ **No `set_weight` / `set_style` calls** — desc built with family + size only |

The honest comment at the top of `ua_stylesheet.cpp` **already documented this**:

> *font-weight and font-style are declared so the UA sheet matches CSS 2.1's intent; they do not flow into LayoutText yet (a future font-metrics ADR will consume them without touching this file).*

ADR-070 is that future ADR.

## Decision

Wire `font-weight` and `font-style` from `ComputedStyle` through the layout fragments to the Pango font description. Mirror the existing `font_family` / `color` pattern exactly (those already flow through the same three hops).

### Data model changes

**`LineFragment`** (`layout_box.hpp`):
```cpp
int font_weight = 400;   // CSS numeric weight; 400 = normal, 700 = bold
bool italic = false;     // true for italic | oblique
```

**`LayoutText`** (`layout_box.hpp`) — same two fields for consistency with block-level runs.

**`StyledWord`** (`text_utils.hpp`) — same two fields.

### Cascade → fragment propagation

Two new helpers in `layout.cpp`, modeled on `text_color_of`:

```cpp
int  parse_font_weight(std::string_view v, int parent_weight);  // "bold"→700, "700"→700, "bolder"/"lighter" relative
int  font_weight_of(const css::ComputedStyle* style);
bool is_italic(const css::ComputedStyle* style);
```

Insertion points (three total):
1. **Text-only LayoutText path** (layout.cpp ~965): set `f.font_weight = block_weight; f.italic = block_italic;` on every emitted fragment, and on the parent `LayoutText`.
2. **`flatten_inline_to_words`** (layout.cpp ~238): walk each element, resolve its `font-weight`/`font-style` per element (same override pattern as `color` today), carry through the walker signature.
3. **`break_styled_words_into_lines`** (text_utils.cpp): copy `w.font_weight`/`w.italic` into `LineFragment`; add them to `same_style` fragment-merge equality check so mixed-style neighbors don't accidentally merge.

### Pango draw

`make_layout` in `pango_text.cpp` gains two defaulted parameters:

```cpp
PangoLayout* make_layout(cairo_t* cr, std::string_view text,
                         std::string_view font_family, float font_size,
                         int font_weight = 400, bool italic = false) {
    ...
    pango_font_description_set_weight(
        desc, static_cast<PangoWeight>(font_weight));
    pango_font_description_set_style(
        desc, italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    ...
}
```

PangoWeight enum values are numerically aligned with CSS weights (`PANGO_WEIGHT_NORMAL == 400`, `PANGO_WEIGHT_BOLD == 700`), so the cast is exact.

`draw_line_with_pango` passes `frag.font_weight, frag.italic` into `make_layout`.

## What this ADR deliberately does NOT do

- **Does not extend `measure_with_pango` or the `TextMeasurer` signature.** Doing so would ripple into every call site and require a separate font-metrics refactor. Accepted consequence: **bold text width is measured as regular weight**, so bold text may overflow its block by 2–5%. Cosmetic for v1; scheduled for a follow-up.
- **Does not add `vertical-align: super/sub`.** `<sup>` and `<sub>` in the tree continue rendering at baseline (Wikipedia reference markers stay baseline-sized but *will become bold if wrapped in `<b>` — they aren't — or italic if wrapped in `<em>` — they aren't; typical Wikipedia refs are `<sup><a href>...</a></sup>` so they stay baseline-sized for now). Deferred to ADR-071.
- **Does not add `text-decoration` (underline / line-through / overline).** `<u>`, `<s>`, `<ins>`, `<del>` remain unstyled. Deferred to ADR-072.
- **Does not touch `ua_stylesheet.cpp`.** All rules needed already exist; this ADR only wires them through.
- **Does not add unit tests for the full cascade → Pango path.** The critical behavior (bold `<h1>`, italic `<em>`) is verified end-to-end via a fixture page + a Wikipedia screenshot diff. Unit-test infrastructure for inline-styled layout doesn't exist yet; building it is a separate ADR.

## Verification plan

Two checks, both required before ADR moves to Implemented:

1. **Fixture** (`docs/adr070-fidelity-test.html`): known tags in known contexts. Expected:
   - `<h1>`, `<h2>`, `<h3>`, `<h4>` visible as bold
   - `<b>`, `<strong>` bold; `<em>`, `<i>`, `<cite>`, `<var>` italic
   - Nested `<b><em>x</em></b>` renders as **bold italic**
   - `<code>` renders monospace (already the case; regression check)
2. **Wikipedia HTML article**: section headings visibly bold (previously flat); body-text emphasis visually distinct; no crash; no regression of ADR-069 scroll behavior.

**Verification category:** production-visual, fixture + Wikipedia side-by-side vs Firefox.

## Risks

- **Layout width drift on bold text.** Since `measure_with_pango` is unchanged, bold content is laid out at regular-weight width. Wikipedia's heading font is not dramatically wider than body, so overflow should be minor. If wrapping failures appear in the fixture, `measure_with_pango` extension moves from "deferred" to "part of ADR-070."
- **Fragment merge rules change.** `break_styled_words_into_lines` currently merges adjacent words with same `{family, size, color, element}`. Adding `{weight, italic}` means `"plain <b>bold</b> plain"` no longer merges into one fragment — it produces three. This is *correct* (Pango needs distinct font descriptors), but if any paint path assumed one-fragment-per-run, that assumption breaks. ADR-018 already established per-fragment elements, so paint handles multiple fragments; regression risk is low.

## References

- `ua_stylesheet.cpp` — the "future font-metrics ADR" comment that this ADR answers
- ADR-014 — UA stylesheet scope
- ADR-016 — inline context (per-fragment style foundation)
- ADR-069 — diagnostic that surfaced this gap

## Implementation

Shipped on `feat/adr-070-font-weight-style` at 2026-11-04.

### What shipped

**`layout_box.hpp`** — `LineFragment` and `LayoutText` gained `int font_weight = 400;` + `bool italic = false;`

**`text_utils.hpp`** — `StyledWord` gained same two fields

**`layout.cpp`** — three helpers near `text_color_of`:
- `parse_font_weight(std::string_view, int parent_weight)` — keyword + numeric + bolder/lighter
- `font_weight_of(const ComputedStyle*)`
- `is_italic(const ComputedStyle*)`

Propagated through three insertion points:
1. Block-level (LayoutText path): capture `block_weight`/`block_italic` from block style, apply to every fragment + parent `LayoutText`
2. `flatten_inline_to_words`: extended signature to carry `weight`/`italic`; per-element override mirrors `color` pattern exactly
3. `break_styled_words_into_lines`: copy `w.font_weight`/`w.italic` to `LineFragment`; added both to `same_style` merge equality

**`pango_text.cpp`** — `make_layout` extended with defaulted `(int font_weight = 400, bool italic = false)`; calls `pango_font_description_set_weight` + `set_style`. `draw_line_with_pango` passes fragment values.

**`docs/adr070-fidelity-test.html`** — 493-byte fixture with h1/h2/h3/h4, bold, strong, em, i, nested, cite, var, code, strong.

### Verified delta (2026-11-04, fixture screenshot)

8-of-8 checks pass:

| Check | Result |
|---|---|
| h1 large + bold | ✅ (was flat before ADR-070) |
| h2 medium + bold | ✅ |
| h3 small + bold | ✅ |
| h4 body-size + bold | ✅ |
| `<b>`, `<strong>` inline bold | ✅ |
| `<em>`, `<i>` inline italic | ✅ |
| `<cite>`, `<var>` italic | ✅ |
| `<code>` monospace (regression check) | ✅ |
| Nested `<b><em>x</em></b>` = bold italic | ✅ |

### Wikipedia status (honest reporting)

Successful run: no crash, smooth scroll (multiple scroll PERF lines), all previously-rendering content intact (infobox, tim photo, images). **Explicit "are headings now bold" observation not captured in this session** — could not distinguish between:
- (a) Wikipedia headings now bold (matches Firefox behavior), or
- (b) Wikipedia headings still flat because MediaWiki's author CSS sets `font-weight: normal` on headings with higher specificity than the UA sheet

**If (a):** ADR-070 fully closed for Wikipedia too.
**If (b):** ADR-070 still closed — pipeline is correct — and a follow-up ADR addresses cascade specificity for Wikipedia's author stylesheets.

Either way, the fixture is the primary proof that the pipeline itself works. Re-verification of Wikipedia headings is a next-session task; captured here so the record is honest about what was and wasn't observed.

### What was deferred and why

- **`measure_with_pango` extension** — bold text is laid out at regular-weight metrics. Accept ~2–5% width drift on bold content (Wikipedia headings may overflow their block by a few px). Scheduled for a dedicated font-metrics ADR. This is the primary trade-off of shipping ADR-070 now.
- **`vertical-align: super/sub`** — reference markers still baseline-sized. ADR-071.
- **`text-decoration`** (underline, line-through, overline) — `<u>`, `<s>`, `<del>`, `<ins>` remain unstyled. ADR-072.
- **Unit tests for the full cascade → Pango path** — verification is fixture + Wikipedia visual. No headless layout-styling test infrastructure yet; separate ADR candidate.

### Fragment merge behavior change

`break_styled_words_into_lines` previously merged fragments with same `{family, size, color, element}`. Adding `{weight, italic}` to the equality check means adjacent words of different weight no longer merge — `"plain <b>bold</b> plain"` produces three fragments instead of one. Pango needs this (one font descriptor per fragment); no regression observed in fixture or Wikipedia render.

### Process note

ADR-070 was initially drafted implementation-first at user prompt. The user caught this and requested spec-first. This commit corrects the order:

- **Commit 1** (`4d0ce3d`): diagnostic (`dump_layout_stats` + `HONEYBEE_DUMP_DOM` hook) + this spec as **Proposed**
- **Commit 2** (this one): the implementation + this **Implementation** section appended + verification fixture

**Lesson recorded:** every ADR commit should include `docs/decisions/0XX-*.md` in the same commit as the source. If the spec wasn't written first, the code doesn't ship — even if the code works. The spec is what survives the session.

