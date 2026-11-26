# ADR-050: @supports Conditional Rules

**Status:** Implemented
**Date:** 2026-10-29
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-027 (@media — the parse pattern followed here), ADR-028 (grid), ADR-029 (flex), ADR-038 (pseudo-classes), ADR-047 (mask-image — feature-table entry needed for fallback exclusion)

## Context

`@supports` blocks are completely dropped by the current parser.
An empirical test against four synthetic stylesheets confirms:

| Test | Rules parsed |
|---|---|
| `@supports (display: grid) { .x { color: red } }` | **0** |
| `@supports not (display: grid) { .x { color: red } }` | **0** |
| `@media (...) { @supports (display: grid) { .x { color: red } } }` | **0** |
| `.x { color: red }` (baseline) | 1 |

Every rule inside every `@supports` block in Wikipedia CSS is
currently lost. This is not a partial implementation — the
block structure is entirely ignored.

### Measurement (Wikipedia CSS, 2026-10-29)

**73 `@supports` blocks total.** Breakdown:

| Condition form | Count |
|---|---|
| `(A) or (B)` | 42 |
| `not (...)` | 26 |
| `(single feature)` | 5 |

**Deduplicated conditions** (all 10 unique):

```
@supports (width: round(1.5px, 1px))
@supports (text-decoration: underline dotted)
@supports not selector(:focus-visible)
@supports (-webkit-mask-image: none) or (mask-image: none)
@supports not ((-webkit-mask-image: none) or (mask-image: none))
@supports (display: grid)
@supports (word-break: break-word)
@supports (overflow-wrap: anywhere)
```

**22 of 73 blocks are nested inside `@media`** — the current
`@media` parser explicitly skips nested at-rules (line 822:
"Nested at-rule: skip"), so those blocks are dropped twice:
once by the nested-skip, once by the missing `@supports`
handler.

### First-viewport-visible content inside these blocks

| Block | Rule inside | Impact if unblocked |
|---|---|---|
| `@supports (display: grid)` | `.vector-pinned-container { display: block }` | Sidebar TOC may render (currently `display: none`) |
| `@supports (mask-image)` | Icon `mask-image` rules | Icons stay correct (already working via ADR-047) |
| `@supports not (mask-image)` | Icon `background-image` fallbacks | **Must NOT apply** (mask wins on our engine) |

### The design subtlety: why unconditional include is wrong

The natural "small v1" is to parse `@supports` blocks and
**include all contained rules unconditionally** (assume all
features supported). That's what some engines do pragmatically.

**It fails for the mask-image case.**

Wikipedia pairs two blocks:

```css
@supports not ((-webkit-mask-image: none) or (mask-image: none)) {
    .icon { background-image: url(x.svg); }    /* fallback */
}
@supports ((-webkit-mask-image: none) or (mask-image: none)) {
    .icon { mask-image: url(x.svg); background-color: #202122; }  /* primary */
}
```

Note: these set **different properties**. `background-image` and
`mask-image` are not competing declarations on the same property
— both can be in the cascade simultaneously. Our paint order
(ADR-034 then ADR-047) draws background-image **over** the
mask-derived fill, producing **double-rendered icons** if both
blocks are unconditionally included.

**Source-order resolution does not help** because the
properties are additive, not competing.

**Conclusion: feature-query evaluation against a capability
table is required, not optional.**

## Decision

Parse `@supports` blocks and evaluate their feature queries
against a hardcoded capability table. Include the contained
rules only when the query evaluates to `true`.

### Capability table (v1)

| Query form | Behavior |
|---|---|
| `(prop: val)` | Check `prop` against supported-property list. Check `val` for unsupported math functions (`round`, `sqrt`, `sin`, `cos`, `tan`) and unsupported style keywords (`dotted`, `dashed`). |
| `not (query)` | Negate |
| `(A) or (B)` | Either |
| `(A) and (B)` | Both |
| `selector(:pseudo)` | Check pseudo against supported-pseudo list |

**Supported properties** (v1 — every property our cascade parses):
`display`, `margin*`, `padding*`, `border*`, `width`, `height`,
`min/max-width/height`, `color`, `background*`, `mask*`,
`font*`, `line-height`, `text-*`, `overflow*`, `position`,
`top/right/bottom/left`, `flex*`, `grid*`, `gap`, `column-gap`,
`row-gap`, `z-index`.

**Explicitly unsupported values:**
- `round(...)`, `sqrt(...)`, `sin(...)`, `cos(...)`, `tan(...)` —
  not in `eval_math_expression`
- `dotted`, `dashed` for `border-style`, `text-decoration-style` —
  our paint draws all styles as solid
- `:focus-visible`, `:focus-within`, `:has`, `:where`, `:is` with
  complex args — not in ADR-018 or ADR-038

**Supported pseudos** (ADR-018 + ADR-038): `:link`, `:visited`,
`:hover`, `:root`, `:first-child`, `:last-child`, `:only-child`,
`:empty`, `:first-of-type`, `:last-of-type`, `:only-of-type`,
`:nth-child`, `:nth-last-child`, `:nth-of-type`,
`:nth-last-of-type`, `:not`, `:is`, `:where`, `:lang`.

### Parse structure

**Top-level parse** (`parse()` in `stylesheet.cpp`):
- After the existing `@media` branch (line ~903), add an
  `@supports` branch: parse prelude, evaluate against capability
  table, if `true` parse the block body and emit rules; if
  `false` skip the block body entirely

**Nested inside `@media`** (line ~822 in `parse_media`):
- Replace `skip_at_rule(body, skip)` for `@supports` with a
  recursive call to the `@supports` parser (same evaluator, same
  rule emission). Nested `@media` remains skipped (unchanged).

**Not in scope**: nested `@supports` inside `@supports`
(0 uses in Wikipedia).

### Rule emission

Rules inside a matching `@supports` block enter the cascade
exactly like top-level rules. Their `MediaQuery` is inherited
from any surrounding `@media` (unchanged from ADR-027). The
`@supports` condition is a **parse-time gate**, not a
cascade-time gate.

**Why**: `@supports` is a static feature-detection at parse
time; once the engine has decided it supports (or doesn't
support) a feature, that decision doesn't change per-element.
Same shape as `@media`'s viewport check but evaluated once at
parse rather than per-cascade.

## Design decisions

### Why a hardcoded capability table (not runtime feature detection)

A "real" implementation would probe the engine for each feature
(query whether `display: grid` actually works by running a
test). That's ~200 more lines and requires a small layout test
harness. For 8 unique conditions in the target page, a lookup
table is the correct scope.

**Cost of hardcoding**: when a new property is added (e.g. a
future ADR adds `text-decoration: dotted`), the table needs
updating. That's a one-line change in the same file as the
implementation. Acceptable.

### Why `@supports` is a parse-time gate (not cascade-time)

`@media`'s viewport check must be cascade-time (relayout at
different widths changes which rules apply). `@supports` is
static: either we support `display: grid` or we don't. Parse-
time evaluation matches the semantics and avoids threading a
capability-table lookup into the cascade hot path.

### Why `@media`-wrapped `@supports` must work in v1

22 of 73 blocks are `@media`-wrapped. If the `@media` parser
continues to `skip_at_rule` for nested `@supports`, we'd drop
those 22 blocks even after implementing the top-level handler.
The extension to `parse_media` (recursive call) is ~10 lines and
captures 30% of the target blocks.

### Why include `not selector(...)` in the capability table

`@supports not selector(:focus-visible)` should **include** its
block (since we don't support `:focus-visible`). What's inside
is `:focus` fallback rules — which we also don't match
(`:focus` is not in ADR-018's pseudo set). Including them is
harmless: the rules enter cascade but the selector never
matches. Non-matching selectors are already dropped at
match time.

## Deferred

- **Runtime feature detection** — actual probe-based `@supports`
  (would matter on a site that used complex capability queries)
- **`@supports` with `selector(...)` for complex argument forms** —
  v1 handles simple `:pseudo` only; `selector(a > b)` deferred
- **Nested `@supports` inside `@supports`** — 0 uses in target
- **`@supports` in `@import`'s layer chain** — not exercised
- **`@supports font-tech(...)` / `font-format(...)`** — future CSS
  feature queries; not in target page
- **Capability table auto-derivation** — the table is hand-
  maintained; a future ADR could generate it from the actual
  parser/layout/paint supported set

## Consequences

**Better:**
- **73 `@supports` blocks reach cascade** (was: all dropped)
- **`.vector-pinned-container { display: block }` applies** —
  unblocks sidebar TOC (subject to other culling rules, see
  Verification)
- **`@media`-wrapped `@supports` blocks work** (22 of 73)
- **Mask-image primary rules included, fallback rules excluded** —
  no double-rendered icons
- **`@supports not selector(:focus-visible)` blocks include** —
  `:focus` fallback rules enter cascade (harmless, never match)

**Accepted limitations:**
- **Hardcoded capability table** — new properties need table
  entries (one-line changes in the same file)
- **`@supports` in `@supports`** deferred
- **`selector(...)` complex args** deferred
- **Capability table isn't auto-generated** from actual engine
  state — could drift if a future ADR adds a property but forgets
  the table

**Unchanged:**
- `@media` behavior (except nested `@supports` now processed)
- Everything else

**Possible regression risks (to verify in Commit 2):**
- If `@supports not (mask-image)` blocks are incorrectly included,
  ADR-047's icon tests could regress (double-rendered icons)
- If a `@supports` block contains unsupported rules that
  shouldn't fire (e.g. `round()` inside), those rules could
  enter cascade with wrong values — mitigated by capability
  table's unsupported-value checks
- ADR-047's mask rules and ADR-034's background-image rules
  coexist in cascade — the previous session verified they
  render correctly independently; new risk is the fallback
  rules being included incorrectly

## Implementation plan

Three commits:

1. **Parse + capability table + evaluator** (behavior-neutral
   for existing tests — no existing test uses `@supports`):
   - New `bool feature_query_supported(std::string_view prelude)`
     in `stylesheet.cpp` with capability table
   - Top-level `@supports` branch in `parse()`
   - `parse_media`'s `skip_at_rule` for `@supports` becomes
     recursive `@supports` parse
   - No test changes yet

2. **Tests + visual verification**:
   - 15 unit tests in `test_stylesheet.cpp` (`[css][supports]`)
   - Layout-level test: `.vector-pinned-container` no longer
     `display: none`
   - First-viewport PNG: sidebar TOC renders (or document
     zero-delta if other culling rules block it)
   - Regression: ADR-047 icons still render correctly

3. **Docs**: ADR-050 → Implemented; Post-ADR-050 note in
   ADR-027; README + CHANGELOG

## Verification

**Unit tests (Commit 2, `[css][supports][adr-050]`):**

- `@supports (display: grid) { .x { color: red } }` → 1 rule,
  `color: red`
- `@supports (display: block) { ... }` → 1 rule (display: block
  supported)
- `@supports (width: round(1px, 1px)) { ... }` → 0 rules
  (`round()` unsupported)
- `@supports not (width: round(1px, 1px)) { ... }` → 1 rule
  (`not` of unsupported = supported)
- `@supports (mask-image: none) { ... }` → 1 rule (mask-image
  supported after ADR-047)
- `@supports not (mask-image: none) { ... }` → 0 rules (fallback
  excluded)
- `@supports (-webkit-mask-image: none) or (mask-image: none) { ... }` → 1 rule
- `@supports not ((-webkit-mask-image: none) or (mask-image: none)) { ... }` → 0 rules
- `@supports (text-decoration: underline dotted) { ... }` → 0
  rules (`dotted` unsupported)
- `@supports not selector(:focus-visible) { .x:focus { ... } }` →
  1 rule (`:focus-visible` unsupported → `not` = true)
- `@supports selector(:hover) { ... }` → 1 rule (`:hover`
  supported)
- `@media (min-width: 100px) { @supports (display: grid) { ... } }`
  → 1 rule with media query preserved
- `@media (max-width: 50px) { @supports (display: grid) { ... } }`
  → 1 rule but media query is unsatisfied at viewport 1774
  (still parsed, cascade drops)
- `@supports (display: grid) { }` (empty block) → 0 rules, no
  crash
- Malformed `@supports foo { ... }` → 0 rules, no crash

**Layout test (Commit 2):**
- `.vector-pinned-container` element in Wikipedia HTML →
  computed `display: block` (was `none`)
- Layout tree: `.vector-pinned-container` box has height > 0

**First-viewport PNG verification (Commit 2):**

| Check | Before | After |
|---|---|---|
| Sidebar TOC region (`vector-pinned-container` TOC nav) | Invisible (display: none) | **Possibly visible** — pending other culling |
| ADR-047 icons (issue box, external links) | Correct | Correct (regression) |
| Everything else | — | Unchanged |

**Honest prediction**: The sidebar TOC may still not render because `.vector-main-menu-container` and other ancestors have their own culling rules (`display: none` from `.vector-main-menu-landmark` etc.). ADR-050 unblocks `@supports`-gated `display: block`, but other rules may still hide the TOC. If so, the ADR-050 delta is verified at layout-tree level (`.vector-pinned-container` has display:block + height > 0) but zero at first-viewport PNG. Documented honestly, same pattern as ADR-047.

## Acceptance criteria

ADR-050 is complete when:

- [ ] `feature_query_supported()` with capability table
- [ ] Top-level `@supports` parse branch
- [ ] `@media`-nested `@supports` recursively parsed
- [ ] `not`, `and`, `or`, `selector(...)` handling
- [ ] Unsupported-value checks (`round`, `dotted`, `dashed`)
- [ ] 15 unit tests
- [ ] Layout test: `.vector-pinned-container` display:block
- [ ] First-viewport PNG verification (visible sidebar delta or
      documented zero-delta with layout-tree proof)
- [ ] ADR-047 icons regression test passes
- [ ] Zero new warnings
- [ ] Post-ADR-050 note in ADR-027
- [ ] `main` green: 611 + N cases
- [ ] ADR-050 status → Implemented

**Explicit claim**: 73 `@supports` blocks in Wikipedia CSS
reach the cascade for the first time. `@supports (display: grid)`
unblocks `.vector-pinned-container { display: block }`, giving
`.vector-pinned-container` height > 0 (was 0). Sidebar TOC may
render at first-viewport depending on other culling rules.

**Honest caveats:**
1. **Sidebar TOC may still not render** — other culling rules
   (`.vector-main-menu-landmark { display: none }`) may block
   it; verified at layout-tree level regardless
2. **Capability table is hand-maintained** — future property
   additions need table entries
3. **`@supports` in `@supports` and complex `selector(...)`
   deferred**
4. **Runtime feature detection deferred** — table is
   pragmatic v1

## Implementation

Implemented on `feat/supports` across three commits (Commit 1
= parse + evaluator, Commit 2 = tests, Commit 3 = this docs
update).

**Layout-tree verified.** First-viewport PNG verification
deferred pending ancestor culling check (see below).

### What shipped

**Commit 1 -- parse + capability table + evaluator:**
- `supported_properties()` -- set of properties the engine
  parses
- `supported_pseudos()` -- ADR-018 + ADR-038 set
- `unsupported_value()` -- rejects `round`/`sqrt`/`sin`/`cos`/
  `tan` (ADR-037 deferral) and `dotted`/`dashed` (ADR-040
  deferral)
- `feature_query_supported()` -- recursive evaluator
  - Recursion inside outer-paren-stripped branch (fixes
    `not ((A) or (B))` when both supported)
- `parse_supports_block()` -- top-level + `@media`-nested
- Top-level `@supports` sibling to `@media` in `parse()`
- `@media`-nested `@supports` recursive parse (replaces
  `skip_at_rule` for `@supports` only)

**Commit 2 -- 15 tests:**
- All 10 unique conditions in Wikipedia CSS covered
- **The load-bearing test**: `not ((A) or (B))` for mask
  correctly returns false -> block dropped -> no
  double-rendered icons (ADR-047 mask + fallback
  background-image conflict)

**Commit 3 (this commit) -- docs:**
- ADR-050 status -> Implemented
- Post-ADR-050 note in ADR-027 (nested at-rule handling)
- README row update
- CHANGELOG entry

### Verified delta

**Layout-tree verification (Wikipedia HTML article,
`/tmp/wiki.html`)**:

```
.vector-pinned-container computed style:
  display = block         <- was none (from top-level rule only)
```

That rule exists **only** inside `@supports (display: grid)`.
Pre-ADR-050 the block was dropped entirely, so
`.vector-pinned-container` fell back to the top-level
`display: none`. Post-ADR-050 the `@supports` gate fires, the
rule enters cascade, and source order makes it win over the
earlier `display: none`.

**This is stronger than an aggregate rules-count check**: a
rule that could only exist inside `@supports` is now reaching
the cascade.

### Honest delta limitation

**First-viewport PNG delta**: not yet verified. The
`.vector-pinned-container` element now has `display: block`,
but its ancestors have their own culling rules:

- First `.vector-pinned-container` sits under
  `.vector-main-menu-landmark`, which has `display: none`
  (from `.vector-feature-main-menu-pinned-disabled
  .vector-main-menu-container .vector-main-menu-landmark`)
- Second `.vector-pinned-container` sits under
  `.vector-sticky-pinned-container` under
  `.vector-column-start`, which the ADR-028 diagnostic
  showed has `h=0` from other rules

**The `@supports (display: grid)` gate is now unblocked**, but
whether the TOC becomes visually visible in the first
viewport depends on whether those ancestor culling rules are
also unblocked. Documented as a follow-up: when the
header-flex (ADR-029) or ancestor-display work lands, the TOC
may render; ADR-050's contribution (the `@supports` gate) is
complete either way.

### Rules-parsed count observation

Post-ADR-050, `sheet.rules.size()` = 968 for `/tmp/wiki.css`.
Prior "~1147" was an unreliable guess (never cleanly measured
this session). The `display: block` per-element proof is
stronger than the aggregate count: without ADR-050 that rule
could never reach the cascade regardless of total rule count.

### Test count

- Before ADR-050: 611 cases, 2386 assertions
- After Commit 1: unchanged (behavior-neutral parse)
- After Commit 2: 626 cases, 2407 assertions (+15 / +21)
- After Commit 3: unchanged (docs only)

Zero new warnings.

### Deferred (documented)

- **Runtime feature detection** (probe-based, ~200 lines) --
  hardcoded capability table is pragmatic v1
- **`@supports selector(...)` complex args** -- v1 handles
  simple `:pseudo` only
- **Nested `@supports` inside `@supports`** -- 0 uses in target
- **`@supports font-tech(...)` / `font-format(...)`** -- not
  in target page
- **Capability table auto-derivation** -- hand-maintained



## Post-ADR-051 note

ADR-050 unblocked `.vector-pinned-container { display: block }`
via `@supports (display: grid)`. The next blocker in the
sticky-sidebar chain was `.vector-sticky-pinned-container`'s
`max-height: calc(100vh - (24px * 2))`, which resolved to
`(unset)` because `vh` wasn't parsed.

**ADR-051 closes that gap**: `parse_length` and `parse_operand`
now handle `vh`/`vw`/`vmin`/`vmax`. `calc(100vh - 48px)` at
`viewport_h=800` returns 752; at `viewport_h=0` (production
default, until GTK height threading) returns -48. Either way,
the expression resolves to a number rather than failing to
parse.

**Sticky-sidebar chain after ADR-050 + ADR-051**:

| Layer | Status |
|---|---|
| `@supports (display: grid)` gate | ADR-050 unblocked |
| `.vector-pinned-container { display: block }` | Reaches cascade |
| `max-height: calc(100vh - 48px)` | ADR-051 resolves to a number (0 in production until GTK height threading) |
| Layout reads `max-height` | Deferred (min/max clamp ADR) |
| Ancestor `.vector-column-start h=0` | Deferred (separate culling rules) |

The remaining sticky prerequisites after ADR-051 are:
1. **GTK allocated height threading** to `render_pipeline` --
   without this, `vh` is 0 in production, so `calc(100vh - ...)`
   computes but yields `-48`
2. **`max-height` honoring in layout** -- cascade stores the
   value, layout needs to clip
3. **Ancestor culling unblock** -- `.vector-column-start h=0`
   comes from other rules

`position: sticky` itself (the scroll-aware positioning behavior)
is a separate cycle from all three of those.
