# ADR-079: Line-by-line float wrap

**Status:** Accepted (implemented in PR #295; production History
  improvement requires ADR-074's Bug A fix, see Implementation status)
**Date:** 2026-11-16
**Implemented:** 2026-11-24
**Related:** ADR-031 (float block-narrowing), ADR-032 (deferred line-wrap), ADR-074 (float positioning), ADR-078 (media-scoped vars)

## Implementation status (2026-11-24)

Implemented in [PR #295](https://github.com/HoneyBeeOne/honeybee-browser/pull/295).
LTR now wraps line-by-line around floats; RTL retains block-narrowing as
a scoped fallback.

### Verified

- New tests `tests/test_float_wrap.cpp` (`[adr079]`): 6/6, 25 assertions.
- Full suite: **3017 assertions in 764 test cases, all passing.**
- ADR-066 float-intrinsic-width and ADR-031 placement invariants untouched.
  5 pre-existing tests retargeted to assert float width directly and
  expect the following block to be full-width.
- Production trace (`HONEYBEE_LOG=layout_detail:trace`) confirms
  `FLOAT_CTX_CHILD <p> ... L=0.0 R=358.4 mode=per-line` fires with the
  infobox width passed to the line breaker. The mechanism is live.

### Honest scope note: production History position is not improved

At default viewport (984px content width), `HONEYBEE_LOG` traces show
`<h2 id='History'>` at y=2281.5 (window 912px content width). Pre-ADR-079
baseline was y=2210. The ~71px difference matches narrower-window wrap
noise; **net effect of ADR-079 on History is ~0**, not the ~1700px
improvement this ADR originally predicted.

Root cause: **ADR-074 Bug A dominates.** The infobox is placed at y=798
(after the intro paragraphs, not alongside them), so:

1. The intro paragraphs (`YPROG cursor_y=152.9, 238.9, 531.9`) are
   laid out **before** the infobox — they receive no `FLOAT_CTX_CHILD`
   and no per-line narrowing (nothing to narrow against).
2. The paragraphs that *do* receive `FLOAT_CTX_CHILD` (start_y=1568+,
   `R=358.4 float_bottom=2389.7`) sit **entirely inside** the float
   zone. Every line is narrowed, so per-line wrapping produces the
   same result block-narrowing would.

ADR-074 estimated Bug B contributed ~565px to History's offset. Direct
measurement now shows ~0px for this article. **This ADR's estimated
benefit was based on the assumption that Wikipedia's intro paragraphs
were long enough to span the float boundary; in fact they are not.**

ADR-079 remains correct and necessary — once Bug A is fixed and the
infobox is placed alongside the intro (y≈267), those paragraphs will
span the float boundary and the per-line wrap will produce real benefit.
Until then, ADR-079 is a **prerequisite, not a fix**, for History's
position.

### Follow-up (blocking History parity)

- **ADR-074 Bug A**: infobox placement at default viewport width.
  Diagnosis candidates listed in ADR-074 §Scope split. This is where the
  History-position win lives.
- Optional observability nit: `FLOAT_CTX_CHILD start_y=-1.0 float_bottom=0.0
  L=0.0 R=0.0 mode=per-line` appears for children of an inactive-but-structurally-
  active `FloatContext`. Harmless (reserves=0 makes the callback a no-op),
  but could be filtered at the trace site for readability.

## Context

Wikipedia's `<h2>History` renders at `y=2210` at 984px content width (Firefox: `y≈500`). ADR-074 diagnosed two related bugs:

- **Bug A:** Infobox float placed 532px too low at narrow width (`y=799` vs `y=267` at wide)
- **Bug B:** Block-narrowing over-stretches intro paragraphs by ~565px

Both stem from the same root: **ADR-031's block-narrowing approximation.**

The current code (layout.cpp:1224 comment) acknowledges this honestly:

> *"Full per-line text-wrap is deferred to ADR-032; this is a block-narrowing approximation that covers the infobox case."*

### The approximation

```cpp
if (start_y < float_bottom) {
    narrow_x = content_x + float_left_w;
    narrow_w = content_w - float_left_w - float_right_w;
}
// Apply narrow_w to the ENTIRE block
```

If a paragraph **starts** within the float's vertical extent, the **entire paragraph** is narrowed. But in real browsers, only lines that vertically overlap the float are narrowed. Lines below the float use full width.

### Measured impact (Wikipedia HTML article at 984px content width)

- Infobox `float: right` height: ~738px
- Intro paragraphs before History: 3
- Each paragraph at block-narrowed width (~618px): ~350-400px tall
- Each paragraph at full width (~976px): ~200px tall
- **Error per paragraph: ~150-200px**
- **Total offset: 532px (Bug A) + ~565px (Bug B) ≈ 1700px**

This matches the observed History offset (2210 - 500 = 1710px).

### Diagnostic confirmation (2026-11-16 trace)

Permanent `FLOAT` and `NARROW` traces from ADR-073 showed:
- `FLOAT <table class='infobox'> dir=right y=799.1 h=738.4 bottom=1545.5` at default
- `NARROW <p> start_y=... float_bottom=1545.5 narrowed=1 w=976->617` for paragraphs starting in float range
- History placed at `y=2210.4` (block-narrowing math propagates)

Section 9 of the diagnostic showed History is painted at all correct scroll positions — **no culling bug, only layout**. Fix the wrap, fix the position.

## Decision

**Defer implementation to next session.** This is a Proposed ADR documenting the measured scope, not a fix.

### The fix

Replace block-narrowing with **per-line narrowing** for floats.

### Data model changes

**`FloatContext` struct** (new, layout_box.hpp):
```cpp
struct FloatContext {
    float left_w  = 0.0f;   // total width reserved on left
    float right_w = 0.0f;   // total width reserved on right
    float bottom  = 0.0f;   // vertical extent of floats
};
```

Carried through the inline layout chain alongside existing `content_x`, `content_w`, `cursor_y`.

**`line_available_width(y_top, y_bottom, content_w, float_ctx)`** — returns the width available for a line spanning `[y_top, y_bottom]`:
- If `y_top >= float_ctx.bottom` → full `content_w` (below float)
- If `y_bottom <= float_ctx.bottom` → `content_w - left_w - right_w` (fully inside float extent)
- Otherwise → transition case: line spans float bottom edge, use narrowed width but check if it fits

### Algorithm changes

**`flatten_inline_to_words`** (layout.cpp):
- Currently returns `StyledWord` list with no y awareness
- Must accept `float_ctx` + starting y
- Each word's y determines available width for its line

**`break_styled_words_into_lines`** (text_utils.cpp):
- Currently takes `max_width` (single value)
- Must take a `float_ctx` and evaluate per-line
- When line wraps to next y, re-evaluate available width for new y
- This is the core change — the current line-breaker is single-width

**`layout_block_children`** (layout.cpp):
- Remove the `if (start_y < float_bottom)` block-narrowing
- Instead pass `float_ctx` down through the inline path
- Block children still need to respect floats for their own y positioning (block elements can't overlap floats, per CSS spec — that part stays)

### What stays the same

- **Block-level float avoidance:** A block child that would start within float's y-extent still needs to jump below it OR be narrowed if it's a block that respects narrowing. That's ADR-031's contribution — keep it for block elements.
- **Float placement:** No change. Floats still placed at cursor position, removed from flow, `float_bottom` tracked.
- **`clear` handling:** No change. `clear: left/right/both` still jumps `cursor_y` past float bottom.

### What changes

- **Inline content inside blocks starting in float range:** Rather than narrowing the entire block, each line within the block uses its own available width. This is the line-wrap implementation.

### The interaction with existing code

**Positive:** Many of the ADR-074 trace points remain useful:
- `FLOAT` trace still shows float placement + bottom
- `NARROW` trace becomes a per-line trace (or removed — line-level trace added instead)
- New `LINE-WRAP` trace per line showing available width calculation

**Risk:** `break_styled_words_into_lines` is used by both the LayoutText path (single-style text runs) and the InlineContext path (mixed-style inline runs). Both need the float-aware line breaker. Tests exist for both paths (test_layout.cpp), so regressions should be caught.

## What this fixes

- **History y-coordinate:** from `y≈2210` → `y≈500` (Firefox-equivalent). Confirmed by the 1700px error calculation.
- **Infobox y-coordinate:** from `y≈799` → `y≈267` (matching MAX viewport). Bug A auto-resolved because it's the same root cause (paragraphs before infobox are also block-narrowed and over-tall).
- **Overall Wikipedia vertical rhythm:** all content below the infobox moves up by the fix amount. Everything becomes vertically closer to Firefox.

## What this does NOT fix

- **Mobile menu at desktop width** — ADR-076-D (`:where()` / `:has()` selectors). Separate ADR.
- **`@media (min-width:...)` custom properties** — ADR-078 deferred. Only relevant if any var fails in specific ranges; not blocking the History position.
- **`<h2>` display:inline routing correctness** — already fixed in ADR-071, works. Heading IS at correct y after this fix.
- **Page-level font metrics** — `measure_with_pango` still uses regular metrics. If bold text is wider than measured, lines may overflow. Separate deferred ADR from ADR-070.

## Realistic scope

| Task | Estimate |
|---|---|
| `FloatContext` struct + `line_available_width` helper | 30 lines |
| `break_styled_words_into_lines` float-aware (both Text and InlineContext paths) | 100-150 lines |
| `flatten_inline_to_words` pass-through of `float_ctx` | 20 lines |
| `layout_block_children` remove block-narrow, pass context | 30 lines |
| Existing test updates (a few may break) | 50 lines |
| New tests for line-wrap behavior | 50-100 lines |
| **Total** | **~300-400 lines, 1-2 sessions** |

**Risk:** Medium. This touches the most complex part of the layout engine (ADR-016 inline context + ADR-031 float interaction). Existing 707 tests provide good coverage, but new edge cases will surface — multi-float, nested, RTL, etc.

## Verification

1. **Unit tests:** new cases for
   - Single float, text wraps around it (2 lines narrow + 3 lines full)
   - Text fully above float (all full width)
   - Text fully below float (all full width)
   - Text starting above float, ending below (transition line)
   - Two paragraphs stacked inside float extent (both wrap correctly)
2. **Wikipedia visual:** `<h2>History` should appear at `y≈500-600` at default 984px content width (matching Firefox). Page becomes visibly shorter vertically.
3. **Wikipedia regression check:** Tim Berners-Lee photo still renders correctly (it's inside the infobox, not affected). Article content below History still correct.

## References

- ADR-031 — float block-narrowing approximation (what we're replacing)
- ADR-032 — deferred line-wrap (this ADR finally implements it)
- ADR-074 — float positioning diagnosis (Bugs A and B, both fixed here)
- ADR-076 — cascade gaps (mobile menu is separate, tracked there)
- ADR-078 — media-scoped vars (independent, keeps its fix)
- layout.cpp:1224 — the honest "deferred" comment
