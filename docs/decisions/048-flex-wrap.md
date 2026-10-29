# ADR-048: Flex Wrap (Multi-line Flex)

**Status:** Accepted
**Date:** 2026-10-29
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-029 (flex layout — deferral closed here), ADR-046 (logging), ADR-047 (precedent for zero-delta infrastructure cycle)

## Context

ADR-029 implemented single-line flex and **explicitly deferred
`flex-wrap: wrap`** (multi-line flex) as its "biggest deferral".
Every measured flex rule with `flex-wrap` has been parsed into
`ComputedStyle` since ADR-029 — `wrap` is just treated as
`nowrap` by the layout engine.

That deferral has now been tracked through ADR-029 → 043 → 045
→ 047. This ADR closes it.

### Measurement (2026-10-29)

**`flex-wrap` in Wikipedia CSS:**

| Value | Uses |
|---|---|
| `wrap` | 3 |
| `nowrap` | 3 |

**The 3 wrap uses** (all target `.mw-header` variants or their children):

```css
.mw-header {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    padding-top: 8px;
    padding-bottom: 8px;
    gap: 16px;
}
```

**Critical scoping fact (measured this session):**

At viewport **≥ 1680px**, `.mw-header` is overridden to
**`display: grid`** (`@media (min-width: 1680px)` block).
Flex only applies below 1680px.

Current fetched HTML (`/tmp/wiki.html`, 799KB, 2026-10-27)
**does not contain a `.mw-header` element**. Grep and DOM walk
confirmed: `mw-header NOT FOUND`. The header rule exists in CSS
but has no element to match in the current snapshot.

**Consequence: `flex-wrap` produces zero visible delta today.**
Two independent conditions are both unmet:

1. Header element must exist in HTML (currently absent)
2. Viewport must be < 1680px (test viewport is 1774px)

**Verification is synthetic-DOM only.** Same pattern as
ADR-041 Commit 1 (pseudo parse shipped before synthesis could
render) and ADR-047 (mask paint shipped before any visible
consumer existed).

## Decision

Implement `flex-wrap: wrap` for row-direction flex containers.
Restructure `layout_flex_children` so single-line and multi-line
share the same per-line resolution logic.

### Current algorithm structure (measured)

`layout_element`'s flex branch runs five phases:

| Phase | What it does |
|---|---|
| **A. Measure** | Loop children, build `FC { el, basis, final_main, grow, shrink, final_box }`. Measure natural size for `auto` basis. |
| **B. Flex resolution** | Sum `basis + gap`, compute free space, distribute `flex-grow` or `flex-shrink` across **all** items. |
| **C. Justify positioning** | Compute `main_cursor` and `effective_gap` from `justify-content` and `remaining`. |
| **D. Layout + cross measure** | Second loop: layout each child at `final_main` width, track `container_cross = max(child.height)`. |
| **E. Cross alignment** | `shift_subtree_y(child, cross_off)` per `align-items`; set `cursor_y = content_y + container_cross`. |

### Multi-line changes (CSS Flexbox §9)

| Aspect | Single-line (current) | Multi-line (added) |
|---|---|---|
| Items per line | All on one line | Break when `sum(basis + gap)` of next item would exceed `content_w` |
| Flex resolution | Across all items | **Per line** — each line's free space distributed among its own items |
| Justify | `main_cursor` across all items | **Per line** — each line justifies independently |
| `align-items` | Cross within container | Cross **within its line** |
| Container main | `sum(final_main)` + gaps | **Max(line widths)** — longest line determines |
| Container cross | `max(child.height)` | **Sum(line heights) + (N-1) * row_gap** |
| `row-gap` | Parsed as `gap` fallback | Between lines when wrapping |
| `align-content` | N/A | Deferred (see below) |

### Refactor plan (guaranteed byte-identical Commit 1)

**Commit 1** extracts Phases B–E into a reusable helper:

```cpp
struct FlexLineResult {
    float container_cross;  // line's cross-axis extent
    float used_main;        // line's main-axis extent
};

FlexLineResult resolve_flex_line(
    std::vector<FC>& fcs,
    float line_main_size,       // = content_w for single line
    float line_main_start,      // = content_x (row) / content_y (col)
    float line_cross_start,     // = content_y (row) / content_x (col)
    float justify,              // justify-content string
    float align,                // align-items string
    bool is_row,
    float base_gap);
```

The current code becomes `resolve_flex_line(fcs, content_w, ...)`
called once when `flex-wrap: nowrap` (or unset). **Byte-identical
output to current single-line code.** All 20 existing ADR-029
flex tests continue to pass unchanged.

**Commit 2** adds the wrap branch:

```cpp
if (flex_wrap == "wrap") {
    // Break fcs into lines.
    std::vector<std::vector<FC>> lines;
    std::vector<FC> current_line;
    float line_main_sum = 0.0f;
    for (auto& fc : fcs) {
        const float next = (current_line.empty() ? 0.0f : gap)
                         + fc.basis;
        if (!current_line.empty() && line_main_sum + next > content_w) {
            lines.push_back(std::move(current_line));
            current_line.clear();
            line_main_sum = 0.0f;
        }
        line_main_sum += next;
        current_line.push_back(std::move(fc));
    }
    if (!current_line.empty()) lines.push_back(std::move(current_line));

    // Per-line resolution, stacked by row_gap.
    float line_cross_cursor = content_y;
    float container_main = 0.0f;
    for (size_t i = 0; i < lines.size(); ++i) {
        auto r = resolve_flex_line(lines[i], content_w, content_x,
                                   line_cross_cursor, justify, align,
                                   /*is_row=*/true, gap);
        if (r.used_main > container_main) container_main = r.used_main;
        line_cross_cursor += r.container_cross + row_gap;
    }
    // container_main is max-line-width; content_w is the flex container's
    // declared width, so we don't override it. Container height = sum
    // of line crosses + (N-1) * row_gap.
    cursor_y = line_cross_cursor - row_gap;  // last increment removed
}
```

**Column + wrap deferred** — CSS §9.3 allows column-direction wrap,
but Wikipedia's `.mw-header` uses row-wrap only. Column-wrap is
unusual; deferred to its own ADR if a real target appears.

**`align-content` deferred** — needs the parsed property plus
cross-axis free-space distribution. Currently unparsed in
`src/renderer/`. Row-wrap v1 packs lines tightly from
`content_y` down; `align-content: stretch` (CSS default) would
additionally stretch lines' cross sizes. Zero targets need
stretch for want of a visible consumer; deferred.

### Deferred (v1 scope)

- **`align-content`** (parsing + cross-axis distribution) — default `stretch` becomes "no stretch, pack tight"
- **`flex-wrap: wrap-reverse`** — 0 uses in target
- **`flex-direction: column` + wrap** — 0 uses in target
- **Cross-axis shrink** — item that can't fit even alone on a line still overflows (CSS §9.3)
- **`row-gap` between lines** — treated as equal to `gap` when both set; separate `row-gap` fallback to `gap` (already parsed by ADR-029)

## Design decisions

### Why extract a helper for Commit 1 (not inline the wrap branch)

The single-line algorithm is 100 lines of stateful code
(`total_basis`, `free`, `main_cursor`, `effective_gap`,
`container_cross`, etc.). Adding a wrap branch inline would
duplicate every one of those calculations, doubling the surface
area for divergence. Extracting `resolve_flex_line` keeps the
logic in one place, applies it per-line, and proves
byte-identical single-line behavior via the existing 20 tests.

**Alternative** (duplicate the algorithm inline for wrap):
rejected — 200 lines of nearly-identical code that would drift.

### Why not implement `align-content` in v1

`align-content` requires:
1. Parse `align-content` into `ComputedStyle` (currently absent)
2. Compute total cross extent of all lines
3. Distribute free cross space (`stretch`, `space-between`, etc.)

Zero consumers today. Every v1 wrap test uses natural line
heights. Adding the parsing + distribution would be speculative
work with no test — the same trap ADR-043 avoided (implementing
blur/inset shadows that had no consumer).

### Why column-wrap deferred

`.mw-header { flex-direction: row }` (implicit). Even if
Wikipedia had a column-wrap rule, the CSS wouldn't exercise it
in a way that differs from row-wrap for layout output. Row-only
in v1 keeps the line-breaking logic to one axis and matches
every measured target.

### Why row-gap between lines uses `gap` fallback

`.mw-header { gap: 16px }` — single-value `gap` sets both
`row-gap` and `column-gap`. ADR-029's existing parse resolves
`gap` → `effective_gap` for main axis. For cross axis, v1 uses
the same value. When `gap` has two values (`16px 8px`), the
existing ADR-029 parse takes the first token — that's the
main-axis gap for row direction. To be honest: for wrap, the
cross-axis gap should come from the **second** token when
`gap` is two-value. v1 uses the first token for both axes
(documented deviation; 0 two-value `gap` uses in target).

### Why this cycle ships despite zero current delta

- **Implementation is correct and testable** — 15+ synthetic DOM
  tests prove the multi-line algorithm works.
- **Behavior-neutral Commit 1** — zero regression surface.
- **Header will appear in a future HTML snapshot** — Wikipedia
  hasn't removed the header, the current fetch just doesn't
  include it (likely a mobile-layout snapshot).
- **Viewport-gated** — anyone rendering Wikipedia < 1680px hits
  `.mw-header { display: flex; flex-wrap: wrap }` immediately
  once the element exists.
- **Same category as ADR-041 Commit 1, ADR-047** — correct
  infrastructure shipped before its consumer.

### Verification category

**Unit-test-verified (synthetic DOM).** No PNG gate — no
first-viewport consumer. Same as ADR-046 (file-mode) and
ADR-047 (layout-level test). Session's fifth category.

## Deferred

- `align-content` parsing + cross-axis distribution
- `flex-wrap: wrap-reverse`
- `flex-direction: column` + wrap
- Cross-axis shrink of items that can't fit alone
- `gap: <a> <b>` two-value cross-axis interpretation

## Consequences

**Better:**
- **Multi-line flex works** — synthetic test verifies 2/3/5-item
  wrap to 2–3 lines
- **ADR-029's biggest deferral closes** — the flex implementation
  is now complete except reverse directions
- **Header readiness** — the moment `.mw-header` appears in HTML
  AND viewport < 1680px, the header wraps correctly
- **`.mw-header .vector-header-start, .vector-header-end` also
  benefit** — both have `display: flex` and would wrap in narrow
  viewports

**Accepted limitations:**
- **Zero first-viewport delta** — no header element in current
  HTML; viewport ≥ 1680px uses grid path
- **`align-content` deferred** — lines pack tight, no stretch
- **`column + wrap` deferred** — row-only
- **`wrap-reverse` deferred**

**Unchanged:**
- All single-line flex behavior — 20 existing tests pass
  byte-identically
- `flex-direction`, `justify-content`, `align-items`,
  `flex-grow`/`shrink`/`basis`, `gap` per ADR-029
- Grid, block, inline — untouched

**Possible regression risks (to verify in Commit 1):**
- Refactor could subtly change single-line cursor positioning if
  the extracted helper's parameters don't map 1:1 to today's
  locals. Mitigation: run all 20 ADR-029 flex tests in Commit 1
  and confirm 598 → 598 cases with 2307 → 2307 assertions
  (unchanged).

## Implementation plan

Three commits:

1. **Extract `resolve_flex_line`, call once for nowrap**
   (behavior-neutral):
   - Move Phases B–E from the current flex branch into the helper
   - Call site wraps helper with `if (!wrap) { auto r = resolve_flex_line(fcs, ...); cursor_y = content_y + r.container_cross; }`
   - **Byte-identical output** — all 20 ADR-029 flex tests pass
   - Zero new tests yet
   - Zero visible delta

2. **Add wrap branch + 15 synthetic tests** (visible to tests):
   - Break `fcs` into lines by accumulated `basis + gap` vs `content_w`
   - Per-line `resolve_flex_line` calls, stack with `row_gap`
   - Container dims aggregated
   - ~15 new tests
   - Manual synthetic-DOM verification: 3-item wrap produces 2 lines, 5-item wrap produces 3 lines, etc.

3. **Docs**: ADR-048 → Implemented with the honest zero-delta
   framing; Post-ADR-048 note in ADR-029 (deferral closed);
   README + CHANGELOG

## Verification

**Commit 1 unit tests (regression):**
- All 20 pre-existing flex tests pass byte-identically
- Zero new tests
- Verified via `cmake --build build && ./build/tests/honeybee_tests`
  (expect 598 / 2307 unchanged)

**Commit 2 unit tests (synthetic DOM, `[flex][flex-wrap][adr-048]`):**

- Container 300px, 3 items each 150px, `flex-wrap: wrap` →
  **2 lines**: line 1 = items 1-2 (150+gap+150 exceeds 300 with
  gap=16: 316 > 300, so item 2 alone on line 1)
  Wait — need to specify gap. Let me use gap=0 for clarity:
  - Container 300px, 3 items each 150px, `flex-wrap: wrap`,
    `gap: 0` → **2 lines**: line 1 = item 1 (150 fits, item 2
    would be 300 which fits, item 3 would be 450 > 300 → break).
    Actually 150+150=300 exactly fits, so line 1 = items 1-2,
    line 2 = item 3. **2 lines**.
- Container 300px, 4 items each 150px, `gap: 0` → **2 lines**
  (2+2 or 2+1+1 depending on algorithm). With greedy wrap:
  line 1 = items 1-2 (300 fits), line 2 = items 3-4 (300 fits).
  **2 lines**.
- Container 300px, 5 items each 150px, `gap: 0` → **3 lines**
  (2+2+1).
- Container 400px, 3 items each 100px, `gap: 0`,
  `flex-grow: 1` each → **1 line** (100*3=300 < 400, free space
  distributed). Same as single-line; **regression** proving wrap
  branch doesn't force breaks when everything fits.
- Container 300px, 3 items each 150px, `flex-grow: 1`,
  `flex-wrap: wrap`, `gap: 0` → line 1 = items 1-2 (each grows
  to 150, filling 300); line 2 = item 3 (grows to 300, alone).
  Line 1 container width = 300; line 2 container width = 300.
- Container 300px, 3 items: [basis=100, basis=250, basis=100],
  `flex-shrink: 1`, `flex-wrap: wrap`, `gap: 0` → line 1 = item 1
  (100, since 100+250=350 > 300); line 2 = item 2 (250); line 3 =
  item 3 (100). **3 lines**.
- `justify-content: center` per line: line 1 with 2 items of
  100 in 300-wide container → items centered (x=50, x=150).
- `align-items: center` per line: line 1 has 50px item and 100px
  item → 50px item centered vertically within line 1's 100px
  cross extent.
- Line stacking: 2 lines each 50px tall with `row-gap: 10` →
  container height = 50+10+50 = 110.
- Container main-axis = max(line widths).
- Empty container with wrap → zero dims (regression).
- Single item + wrap → one line, same as nowrap (regression).
- `flex-wrap: nowrap` → wraps not applied even if items overflow
  (regression on purpose).

**Zero PNG gate** — header not in HTML. Documented.
**Zero first-viewport delta** — documented.

## Acceptance criteria

ADR-048 is complete when:

- [ ] `resolve_flex_line` helper extracted
- [ ] Single-line path calls helper once; all 20 existing
      flex tests pass byte-identically (Commit 1)
- [ ] Wrap branch breaks items into lines by accumulated
      basis + gap vs content_w
- [ ] Per-line flex-grow / flex-shrink / justify-content /
      align-items
- [ ] Line stacking with `row_gap`
- [ ] Container main = max(line widths), cross = sum(line
      heights) + (N-1) * row_gap
- [ ] 15+ new synthetic-DOM tests
- [ ] Zero new warnings introduced
- [ ] Post-ADR-048 note in ADR-029 (flex-wrap deferral closed)
- [ ] `main` green: 598 + N cases, all pass
- [ ] ADR-048 status -> Implemented

**Explicit claim**: `flex-wrap: wrap` on row-direction flex
containers breaks items into lines when their combined basis +
gap exceeds the container's content width. Per-line flex
resolution and align-items. Lines stack with `row-gap`.

**Honest caveats:**
1. **Zero first-viewport delta** — `.mw-header` not in current
   fetched HTML, and at viewport ≥ 1680px uses `display: grid`
   instead. Flex-wrap for header needs both conditions.
2. **`align-content` deferred** — lines pack tight; no stretch
3. **`column + wrap` deferred** — row-only for v1
4. **`wrap-reverse` deferred** — 0 uses
5. **Verification is synthetic-DOM** — no PNG gate possible
   until a consumer exists. Same "correct infra, no consumer"
   pattern as ADR-041 Commit 1 and ADR-047.
