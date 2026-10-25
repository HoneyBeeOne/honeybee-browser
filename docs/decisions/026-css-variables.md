# ADR-026: CSS Variable Resolution (`:root` + `var()`)

**Status:** Accepted
**Date:** 2026-10-25
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-009 (CSS parser), ADR-010 (cascade), ADR-013 (length units), ADR-025 (external CSS loading)

## Context

ADR-025 added external CSS loading. On Wikipedia's HTML article,
27.0% of rules parse (390 of 1,443). The remaining 73% are blocked
by `:not()`, `@media`, `::before`/`::after`, attribute operators,
and — critically for the parseable set — **CSS variables**.

### Measured on Wikipedia's CSS (2026-10-25)

| Metric | Value |
|---|---|
| `var()` calls in CSS | 550 |
| Variables defined in `:root` | **6** |
| `var()` calls with fallback | **516 (93.8%)** |
| `var()` calls without fallback | 34 (6.2%) |
| `var()` calls referencing a defined variable | 8 (1.5%) |
| `var()` calls referencing an undefined variable | 542 (98.5%) |

**Fallback value distribution** (of the 516 with fallback):

| Fallback type | Count | Example |
|---|---|---|
| Color | 316 | `#202122`, `#36c`, `transparent` |
| Length | 166 | `1rem`, `0.875rem`, `180px` |
| Keyword (`inherit`/`initial`/`unset`) | 17 | (not currently supported) |
| Other | 17 | (complex expressions) |

**The critical inversion**: 98.5% of `var()` calls reference an
**undefined** variable. That number looks catastrophic — but CSS
spec says `var(--undefined, fallback)` resolves to the **fallback**
when the variable has no value. So those 516 calls resolve cleanly
to their fallback values, which are overwhelmingly colors and
lengths our parser already handles.

**Effective resolution rate**: with fallback resolution
implemented, **508 of 550 `var()` calls (92.4%)** resolve to usable
CSS values. The 42 that don't: 34 fallback-less undefined, 8 (of the
17) keyword fallbacks — plus complexity from the "other" 17.

**Top variable usages** (all undefined, all with usable fallbacks):

| Variable | Uses | Fallback |
|---|---|---|
| `--font-size-medium` | 135 | `1rem` |
| `--color-base` | 27 | `#202122` |
| `--color-progressive` | 15 | `#36c` |
| `--font-size-small` | 14 | `0.875rem` |
| `--border-color-base` | 13 | `#a2a9b1` |

All five of these fallbacks are values `parse_color` and ADR-013's
length parser handle natively.

### Current state (pre-ADR-026)

`var(--x)` in a declaration is stored verbatim in `ComputedStyle`.
When layout.cpp calls `parse_color(style.get("color"))` on
`var(--color-base, #202122)`, the parse fails and returns black
(or transparent, depending on the property). So all 550 `var()`
calls currently produce no visible effect.

**`:root` is not a supported selector** — the parser drops `:root
{ --x: value }` rules entirely. Even the 6 defined variables are
currently invisible to the cascade.

### The Meta-lesson-v2 caveat (measurement beyond the headline)

The 92.4% resolution rate is **necessary but not sufficient** for
visible improvement. The rules whose declarations contain `var()`
must also target **properties our layout engine consumes** —
`color`, `background-color`, `border-color`, `font-size`, and the
length-based margins/paddings/widths from ADR-013. If most `var()`
usage is in `box-shadow`, `transition`, or `border-radius` (none of
which our layout engine reads), resolution changes nothing visually.

**The measurement plan in Commit 2 includes a "consumed-property
coverage" check**: of the 508 var() calls that will resolve, how
many are in properties our layout engine reads? If low (say <30%),
the visible delta is smaller than the resolution rate suggests, and
ADR-026's Implementation section documents that honestly. The
"measured prediction needs a code-path model" lesson from ADR-024
applies — resolving `var()` in `box-shadow` is invisible today.

## Decision

We add `:root` selector support, extract `--x` variable declarations
from `:root` rules, and pre-resolve `var()` in all rule declarations
at parse time.

### 1. `:root` selector support

Add `SimpleKind::PseudoRoot` to the selector parser. `:root` matches
the document's root element (`<html>` for any parsed HTML doc).

- Parser: `try_parse_simple` recognizes `:root` → `SimpleKind::PseudoRoot`
- Matcher: `matches_simple` case — returns true iff `el` has no parent
  element (the topmost element in the tree)
- Specificity: counts as class weight, same as `:hover`/`:visited`

### 2. `Stylesheet::variables` field

```cpp
struct Stylesheet {
    std::vector<Rule> rules;
    std::unordered_map<std::string, std::string> variables;
    static Stylesheet parse(std::string_view text);
};
```

During `parse()`, after each rule is parsed, if the rule's selector is
exactly `:root` (single-part, single-simple, kind `PseudoRoot`), extract
declarations whose name starts with `--` into `variables`. Those
declarations remain in the rule too (they apply to `<html>` as normal
custom-property declarations — currently dead state, since we don't
consume custom properties per-element yet, but semantically correct).

### 3. `var()` pre-resolution at parse time

After a `Stylesheet` is fully parsed (all rules + `variables` map),
walk every rule and resolve `var()` in every declaration value:

```
for each rule:
  for each declaration:
    if value contains "var(--":
      resolved = resolve_var(value, sheet.variables)
      if resolved is nullopt:
        drop declaration  // CSS spec: invalid at computed-value time
      else:
        declaration.value = *resolved
```

`resolve_var` handles the two CSS-spec cases:

- `var(--x, fallback)` → `sheet.variables[--x]` if defined, else `fallback`
- `var(--x)` → `sheet.variables[--x]` if defined, else `nullopt` (drop declaration)

**Single-pass only.** If `sheet.variables["--x"]` itself contains `var(--y)`,
the value returned is still unresolved. Per the measurement, only 1
variable (`--image-size-user: var(--image-size-standard)`) is recursive;
consumers of that variable fall back to something else anyway. One-pass
resolution suffices for v1 — recursion is deferred.

### 4. Cache invalidation

`Stylesheet::variables` is populated by `parse()`. `Stylesheet` is
cached in `RenderPipeline::sheet_cache_`, populated in `relayout()`
from `merged_css_`. `var()` pre-resolution happens once per stylesheet
parse (per navigation, or per `render_with_css` call), not per element
or per restyle. No additional invalidation needed.

## Design decisions

### Why pre-resolve at parse time, not cascade time or consumption time

**Parse-time** is optimal for this project for three reasons:

1. **Performance**: A rule with `color: var(--x, red)` applied to N
   elements would resolve N times at cascade time. Parse-time does
   it once per rule. On Wikipedia, N ≈ 2,000, giving parse-time a
   ~2,000x speedup for the resolution work.

2. **Correctness**: CSS spec says `var()` resolution happens at
   "computed-value time" — which for a stylesheet with static
   variables is effectively parse time. Since we don't have dynamic
   custom properties (no JS, no `:hover`-triggered variable changes),
   parse-time resolution matches the computed-value-time behavior.

3. **Zero code changes downstream**: `parse_color(style.get("color"))`
   sees `#202122` instead of `var(--color-base, #202122)` — no changes
   needed in `layout.cpp` or any consumer. The resolved value is a
   normal CSS literal that existing parsers handle.

The cost is that **dynamic variables aren't supported** — e.g., if an
author wrote `.warning { --x: yellow } .warning:hover { --x: red }`,
the parse-time resolution would use whatever `:root` said (or the
fallback), not the hover value. Deferred (see below).

### Why per-sheet resolution, not merged UA+author

CSS custom properties cascade like any other property. If UA sheet has
`:root { --a: 1 }` and author sheet has `:root { --b: 2 }`, a rule in
the author sheet using `var(--a)` should resolve against the *merged*
computed value of `--a` (which includes UA's contribution).

For v1: **per-sheet resolution**. Each `Stylesheet` extracts its own
variables and pre-resolves its own rules. This is correct for the
current stylesheets — UA has no `:root` rules (checked: `:root` is a
novel selector we're adding; UA sheet doesn't use it), so per-sheet
and merged produce identical results. Wikipedia's 6 variables are all
in the author sheet's `:root` block, resolvable from within that sheet.

Multi-sheet custom-property merging is deferred (requires a small
architectural change to `parse()` or a separate merge step).

### Why `:root` also becomes a normal rule, not just a variable source

`:root { --x: red }` has dual meaning per CSS spec:
- It defines `--x` for the root element (and inheritance propagates it
  to descendants) — the variable behavior
- It also declares `--x: red` on `<html>`'s computed style — the
  custom-property-declaration behavior

For v1, we extract `--x` into the sheet's variable map (variable
behavior) AND keep the rule (declaration behavior). The declaration
behavior is currently dead state (we don't consume `--x` from
`ComputedStyle`). But it's semantically correct, and when custom
properties become full first-class citizens (a future ADR), this rule
already does the right thing.

` :root { color: red }` (a non-custom-property declaration on `:root`)
behaves identically to `html { color: red }` — matches `<html>`, applies
`color: red`, inherits down. Correct.

### Why drop declarations with unresolved `var()` and no fallback

Per CSS spec, `color: var(--undefined)` (no fallback) is invalid at
computed-value time — the declaration does not participate in the
cascade. If a lower-specificity rule would have applied a valid color,
it wins. If we kept the unresolved declaration, `parse_color` would
return `transparent` or black (parse failure), incorrectly winning
specificity over a valid lower-priority rule.

**Dropping is correct.** 34 of 550 var() calls have no fallback, and
their declarations get dropped — losing ~6% of the var() declarations
but preserving correct cascade behavior for the other 94%.

### Why 92% resolution is acceptable even at ~150 lines

The improvement path from 27% effective CSS (ADR-025) to ~55% (var()
resolution applied — 27% + (73% × 88% × rule-fraction-using-var)),
is the highest single-ADR delta in the arc. 150 lines for it is
excellent ROI compared to:

- `:not()` support (~80 lines for 391 rules) — but those rules likely
  contain `var()` too, so :not() alone without var() resolution has
  low visible impact
- `@media` support (~100 lines for 72 blocks) — bigger visible impact
  (layout), but scope is larger (viewport query evaluation)

var() is the right next step: cheapest per visible-delta-point, and
its improvement compounds with every subsequent ADR (the rules
already parse; they just need their values to resolve).

## Deferred

- **Recursive `var()` resolution**: `--x: var(--y)` requires cycle
  detection and multi-pass resolution. Only 1 case on Wikipedia
  (`--image-size-user`), and its consumers fall back anyway.
- **Element-scoped custom properties**: `--x` on non-`:root` selectors
  (e.g., `.theme-dark { --bg: black }`). Requires custom properties as
  inherited `ComputedStyle` fields. Currently only `:root`-scoped
  variables work.
- **Dynamic custom property cascade**: `:hover`-changing variables,
  `@media`-changing variables. Deferred because parse-time resolution
  is fundamentally incompatible — this would require cascade-time
  resolution + custom-property inheritance.
- **`env()` environment variables**: `env(safe-area-inset-top)` etc.
  Not in Wikipedia's CSS. Deferred.
- **Keyword fallbacks** (`inherit`, `initial`, `unset`): 17 fallback
  cases. Requires parser support for these keywords in `parse_color`
  and `parse_length`, which is a separate ADR.
- **Merged UA+author variable maps**: for correctness when UA sheets
  start using custom properties. Trivial to add when needed.
- **`var()` in property names**: `var(--prop-name): red`. Not in
  current CSS. Deferred indefinitely.

## Consequences

**Better:**
- **92.4% of `var()` calls resolve** to usable values. Rules whose
  declarations were previously dead (their `var()` values failed
  `parse_color`/length parse) now produce real declarations.
- **Specific expected improvements on Wikipedia**:
  - Font sizes (135 rules reference `--font-size-medium`, resolve to `1rem`)
  - Text colors (27+ `--color-base`, resolves to `#202122`)
  - Border colors (13+ `--border-color-base`)
  - Background colors (9+ `--background-color-base`)
- **Headings, body text, and structural colors** likely refine further
  beyond ADR-025's initial improvements.
- **Sidebar link colors** — already partially improved by ADR-025 —
  may complete with the missing 27 `--color-base` usages resolving.
- Estimated effective CSS rate: **~55%** (up from 27%).

**Accepted limitations:**
- **Structural layout** (sidebar columns, header bar, TOC positioning)
  does NOT improve — those rules are still `@media`-gated (ADR-027).
- Dynamic custom properties unsupported (parse-time resolution).
- Recursive variables resolve to their literal-string value, which
  may itself contain `var()` — such declarations are dropped unless
  they have a fallback that doesn't nest.
- 42 of 550 var() declarations dropped (34 fallback-less undefined +
  8 keyword-fallback + partial other).

**Unchanged:**
- Parse acceptance rate (still 27% of rules from Wikipedia's CSS).
- ADR-025 external CSS loading, cache, and fetch behavior.
- ADR-018/019/020/021/022/023/024 behavior.
- The three-ADR arc picture (ADR-027 `@media`, ADR-028 `:not()` +
  attribute operators) — var() is a prerequisite for their rules'
  values to resolve.

## Implementation plan

Three commits on `feat/css-variables`:

1. **`:root` selector + `Stylesheet::variables`** — new `SimpleKind`,
   parser + matcher support, extract `--x` from `:root` rules into
   `Stylesheet::variables`. Tests: parse `:root`, extract variables,
   match `:root` against `<html>` not other elements.
2. **`var()` pre-resolution** — post-parse pass over all declarations
   resolving `var()` using `sheet.variables`; drop declarations with
   unresolved no-fallback. Tests: defined variable, undefined + fallback,
   undefined + no fallback (dropped), nested-var (dropped). **Commit
   message includes the consumed-property coverage measurement from
   Commit 2's diagnostic** (what fraction of resolved var() calls are
   in properties the layout engine reads).
3. **Docs** — ADR-026 status -> Implemented, README + CHANGELOG with
   measured visual delta.

## Verification

**Unit tests**:

- `parse :root { --x: red }` → `sheet.variables["--x"] == "red"`,
  `sheet.rules.size() == 1` (rule still exists)
- `parse :root { color: blue }` → rule exists, no variable extracted
- `matches :root` against `<html>` (true) vs `<body>` (false)
- `parse a { color: var(--color-base, #202122) }` with empty variables
  → declaration value becomes `"#202122"`
- `parse :root { --c: red } a { color: var(--c) }` → value becomes `"red"`
- `parse a { color: var(--undefined) }` → declaration dropped from rule
- `parse a { color: var(--recursive, #fff) }` with
  `--recursive: var(--other)` → resolves to `#fff` (fallback used
  because lookup fails on recursive value — single-pass behavior)

**Visual verification** (measured in Commit 2):

- Load Wikipedia's HTML article before/after ADR-026.
- Check:

| Sub-check | Expected |
|---|---|
| Body text font-size | **improved** (135 var() uses on `--font-size-medium`) |
| Inline text color | **improved** (27 on `--color-base`) |
| Border/divider colors | **improved** (13 on `--border-color-base`) |
| Sidebar link colors | possibly completed (marginal over ADR-025) |
| Sidebar layout | **NO CHANGE** (blocked by `@media`) |
| Header layout | **NO CHANGE** |

- Also measure: what fraction of resolved `var()` declarations target
  properties the layout engine consumes (color, background-color,
  border-color, font-size, length-based). If <30%, ADR-026's
  Implementation section documents the "resolution > visible delta"
  gap honestly.

## Acceptance criteria

ADR-026 is complete when:

- [ ] `SimpleKind::PseudoRoot` added, parsed, matched
- [ ] `Stylesheet::variables` extracted from `:root` rules
- [ ] `var()` resolution applied at parse time to all declarations
- [ ] Unresolved-no-fallback declarations dropped from rules
- [ ] Unit tests (8+ cases) covering parse/match/resolve
- [ ] Consumed-property coverage measured (Commit 2 message)
- [ ] Visual delta measured and documented (Commit 3)
- [ ] `main` is green: 288 + N cases, all pass, zero warnings
- [ ] ADR-026 status -> Implemented in Commit 3

**Explicit claim**: ~92% of Wikipedia's `var()` calls resolve to
usable values via fallback. Visible improvement expected in font
sizes and text colors. Structural layout unchanged (ADR-027).
