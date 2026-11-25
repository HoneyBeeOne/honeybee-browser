# ADR-074: Float positioning at narrow viewport

**Status:** Proposed (diagnosed, implementation deferred — parked pending ADR-076 cascade work)
**Date:** 2026-11-15
**Related:** ADR-031 (float layout), ADR-073 (permanent tracing), ADR-032 (line-wrap deferred)

## Context

While diagnosing Wikipedia `<h2>History` at wrong y-coordinates (before this ADR, we assumed it was one bug), the permanent `FLOAT` trace added during ADR-073 extension revealed **two distinct problems**, not one:

### Symptom
Wikipedia's `<h2>History` appears at:
- **Default window (984px content width):** `y=2210` (Firefox: ~y=500)
- **Maximized (1814px content width):** `y=925` (Firefox: ~y=500)

Both are 400-1700px lower than Firefox expects. The magnitude depends on viewport width.

### Root causes traced

**Bug A — Infobox placement is width-dependent (532px swing):**
```
DEFAULT:  FLOAT <table class="infobox"> y=799.1  h=738.4  bottom=1545.5
MAX:      FLOAT <table class="infobox"> y=267.2  h=738.4  bottom=1013.6
```
The infobox is `.infobox` (Wikipedia's right-floated metadata table). In Firefox, its y is roughly the same regardless of window width — it sits near the top, right after the title. In HoneyBee, it lands at `y=799` at narrow width and `y=267` at wide. **The ~532px difference comes from content appearing/occupying space BEFORE the infobox in DOM order at narrow width but not at wide width.**

**Bug B — Block-narrowing over-stretches paragraphs (565px swing):**
Per ADR-031's design, when a paragraph starts within the float's vertical extent, its ENTIRE height is laid out at `content_w - float_right_w`. The paragraph's lines don't individually wrap around the float — the whole block is narrowed. Because of this, intro paragraphs that would be ~10 lines at full width become ~15-20 lines at narrowed width, stretching the content between the infobox and `History` by ~565px extra at DEFAULT viewport.

**Both bugs compound:** Bug A pushes the infobox down 532px; Bug B stretches the following paragraphs. Together they put History 1710px lower than Firefox at DEFAULT.

## Decision

**Defer implementation to a future session.** This ADR is a decision record documenting the diagnosis. The float trace added during ADR-073 extension is retained permanently, so the next session starts with full observability.

### Scope split

Two ADRs are needed because the fixes are independent:

- **ADR-074 (this one): Float positioning at narrow viewport.** Investigate why 532px of extra content appears before `.infobox` at narrow width. Candidates:
  - Media query handling (some `@media (max-width: ...)` rule reveals hidden content at DEFAULT but not MAX)
  - Fixed/absolute-positioned elements (like `.search-toggle` buttons appearing in FLOAT trace at y=0) being counted in flow instead of out of it
  - A tall empty element (like `.mw-indicators` at `h=0.0` but `y=539.9`) not collapsing correctly

- **ADR-075: Line-wrap around floats.** Complete the ADR-031 → ADR-032 handoff. Implement line-by-line float wrap in `flatten_inline_to_words` / `break_styled_words_into_lines` so paragraph lines below the float's bottom use full width. This is the ADR-032 deferral finally being addressed.

### Fixing ADR-074 alone won't reach Firefox fidelity

Even with perfect float positioning:
- History would go from `y=2210` → `y=~1680` at DEFAULT
- Still 1180px lower than Firefox's `~500`

The remaining gap is Bug B (block-narrowing). Both ADRs are needed for full float fidelity.

## Verified diagnostic data (2026-11-15)

Permanent trace output:
```
=== FLOAT placements ===
DEFAULT:
  search-toggle × 8      y=0       right_w=450
  mw-indicators          y=539.9   h=0.0    right_w=976
  infobox                y=799.1   h=738.4  right_w=358.4

MAX:
  mw-indicators          y=8.0     h=0.0    right_w=948
  infobox                y=267.2   h=738.4  right_w=358.4
```

```
=== History final y-coordinate ===
DEFAULT:  y=2210.4
MAX:      y=925.2
```

**The `.mw-indicators` discrepancy (y=539.9 vs y=8.0) is the direct cause of Bug A.** This div has `h=0.0` (empty) but its y differs by 532px between widths. Something before it in DOM order is taking vastly different vertical space at narrow vs wide.

## Permanent tracing added

Per ADR-073 policy, two new traces were added (permanent, guarded):

| Trace | Category | Purpose |
|---|---|---|
| `FLOAT <tag> class='...' dir=... y=... h=... bottom=... left_w=... right_w=...` | LayoutDetail | Where each float is placed, its size, cumulative side reserves |
| `NARROW <tag> start_y=... float_bottom=... narrowed=0/1 w=...->...` | LayoutDetail | Per-child decision: whether this block was narrowed by an active float |

Both zero-cost when `HONEYBEE_LOG` is unset.

## What this ADR does NOT do

- Does not fix the float positioning bug (deferred to next session)
- Does not fix block-narrowing (deferred to ADR-075)
- Does not change existing trace output formats

## References

- ADR-031 — original float tracking implementation (block-narrowing approximation)
- ADR-032 — deferred line-wrap (never implemented)
- ADR-073 — permanent tracing policy
- This ADR — diagnosis + trace additions, no code fix

## Session note (2026-11-25)

Diagnosis parked. Working hypothesis: root cause is cascade-driven
(per ADR-075), not float machinery. Resuming after ADR-076 cascade
gaps close. Reproduction recipe retained above; trace category is
`layout_detail`, enable with:

    HONEYBEE_LOG=layout,layout_detail \
    HONEYBEE_LOG_LEVEL=trace \
    HONEYBEE_LOG_FILE=/tmp/hb-trace.log \
    ./build/bin/honeybee

Note: `HONEYBEE_LOG=layout_detail:trace` (as quoted in ADR-079's
implementation status) is incorrect — the parser splits on commas
only, so `:trace` is swallowed and the mask ends up 0. Correct form
is two env vars: one for categories, one for level.

Open evidence from the 2026-11-25 trace:

- `mw-indicators` FLOAT placed at `y=539.9` at 984px content width,
  matching ADR-074's original 2026-11-15 measurement — bug unchanged.
- `#firstHeading` laid out twice; second pass reports `x=24.0 w=0.0`
  (invalid geometry, matches ADR-075's observation).
- Wide-viewport capture was not obtained; resize did not trigger a
  fresh layout pass in the trace. Future session needs either a CLI/
  env init-width override, or a way to force relayout after resize.
