# ADR-037: CSS Value Functions — calc() / min() / max() / clamp() (with var()-inside)

**Status:** Implemented
**Date:** 2026-10-27
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-013 (length units), ADR-026 (var() resolution), ADR-027 (@media + narrow calc for queries)

## Context

ADR-013 shipped CSS length units (px, em, rem, %, pt, etc.) but
explicitly deferred `calc()`. ADR-026 shipped `var()`
substitution at cascade time. ADR-027 shipped a *narrow* calc
parser in `stylesheet.cpp:482-535` covering exactly
`calc(Npx ± Mpx)` for `@media` query conditions only (needed
for `@media (min-width: calc(1120px - 1px))`).

The main `parse_length()` — the one layout uses for every
`width`, `height`, `margin`, `padding`, `gap`, `font-size`,
`top`, `left`, and so on — does **not** handle `calc()`.

### Measurement (2026-10-27, Wikipedia HTML + CSS)

**parse_length behavior on calc forms** (all return 0.0):

| Expression | Result |
|---|---|
| `calc(10px + 5px)` | **0.0** |
| `calc(2em + 8px)` | **0.0** |
| `calc(100% - 20px)` | **0.0** |
| `calc(10px * 2)` | **0.0** |
| `calc(100px / 4)` | **0.0** |
| `calc(calc(10px + 5px) * 2)` | **0.0** |
| `calc(var(--x) + 4px)` | **0.0** |

**Wikipedia's calc() usage**:

- **162 occurrences**, **23 unique expressions**
- **Operators** (space-delimited): `+` 79, `-` 62, `*` 15, `/` 1
- **Units present**: `px` 143, `em` 107, `rem` 107, `%` 3, `vh` 1
  (em/rem counts are from `var(--font-size-medium,1rem)` fallback values)

**Which properties use calc**:

| Property | Count |
|---|---|
| `background-size` | 23 |
| `-webkit-mask-size` / `mask-size` | 23 / 23 |
| `width` | 19 |
| `height` | 16 |
| `margin-right` / `margin-left` | 4 / 4 |
| `padding-left` / `padding-right` | 3 / 2 |
| `max-width` / `max-height` | 1 / 1 |
| `left` / `right` | 1 / 1 |

**First-viewport impact** (5 elements at y<900):

| Declaration | Visible effect |
|---|---|
| `width: calc(var(--font-size-medium,1rem) + 4px)` | Search icon width → currently 2×2, should be ~20×20 |
| `height: calc(...)` | Search icon height → currently 2×2 |
| `padding-left: calc(8px + 8px + calc(var(--font-size-medium,1rem) + 4px))` | Search input left padding → currently 0 |
| `max-height: calc(100vh - (24px * 2))` × 2 | Sticky pinned containers — no visible delta (sticky deferred) |

**The var()-inside-calc bug**: computed value still shows raw
`var(--font-size-medium,1rem)` inside `calc()`. ADR-026 handles
top-level `var()` but not `var()` nested inside `calc(...)`.
That means even after shipping a calc parser, `calc(var(--x)
+ 4px)` would receive raw `var()` as an operand and return 0.
**Fixing calc() without fixing var-inside-calc delivers zero
visible delta.**

**The pre-existing narrow parser**: `parse_length_maybe_calc`
in `stylesheet.cpp` handles only `calc(Npx ± Mpx)` for
`@media` query conditions. It's not general (no `*`, no `/`,
no parens, no units other than px) but its narrow scope is
intentional — @media conditions always evaluate to a single
number (viewport width in px).

## Decision

Two coupled features shipped together in one ADR:

1. **A general `calc()` / `min()` / `max()` / `clamp()` value
   evaluator** in `parse_length()`
2. **`var()` resolution extended to work inside those
   functions** (companion fix to ADR-026)

Both are shipped as one ADR because calc() without
var-inside-calc is zero-visible-delta on the current first
viewport.

### The expression parser

Standard recursive descent, standard CSS operator
precedence:

```
expr    := term (('+' | '-') term)*
term    := factor (('*' | '/') factor)*
factor  := '-'? primary
primary := number unit?
         | '(' expr ')'
         | function '(' expr (',' expr)* ')'
function:= "calc" | "min" | "max" | "clamp"
```

- `+` and `-` **must have whitespace on both sides** per CSS
  spec. `calc(10px-5px)` is invalid; `calc(10px -5px)` is
  invalid; `calc(10px - 5px)` is valid. This is genuinely
  required by CSS syntax and prevents ambiguity with negative
  numbers. **v1 does NOT enforce this** (permissive parsing
  accepts both). Documented as leniency.
- `*` and `/` do not require whitespace.
- Unary `-` is supported: `calc(-1 * 12px)`, `calc(-1 * 22px / 2)`.
- Nested `calc(...)` and grouping parens are both supported.

### Unit resolution

Every operand evaluates to `(value, unit)`. Arithmetic:

| Operation | Unit rule |
|---|---|
| `px + px` | Add values, unit = px |
| `em + em` | Add values, unit = em (then resolve at end) |
| `% + px` | Convert % to px via `ctx.parent_content_width`, then add |
| `px * number` | Multiply, unit = px |
| `number * px` | Multiply, unit = px |
| `px / number` | Divide, unit = px |
| `px / px` | Yield unitless number (only legal at outermost or inside another operator) |

**Mixed-unit `em + px`**: convert `em` to px via
`ctx.font_size` (or `ctx.parent_font_size` for `parent_font_size`
context). Then add. Matches CSS spec.

**Percentages**: convert to px via `ctx.parent_content_width`
for horizontal, `ctx.parent_content_height` for vertical (the
latter isn't currently in `LengthContext` — deferred to ADR-038;
v1 treats vertical % as 0). Documented limitation.

**Units deferred to ADR-038**: `vh`, `vw`, `vmin`, `vmax` — need
`viewport_height` in `LengthContext`. Currently only
`viewport_width` is threaded for `vw` semantics; no vertical
context. Wikipedia has 1 `vh` use (`calc(100vh - (24px * 2))`)
on a `max-height` for sticky containers, which isn't visible
since sticky is deferred. Zero first-viewport impact.

### Function semantics

- **`calc(expr)`**: returns `expr` result. Single argument.
- **`min(a, b, ...)`**: returns minimum. All args must have
  compatible units (or be convertible to px).
- **`max(a, b, ...)`**: returns maximum.
- **`clamp(min, preferred, max)`**: returns
  `max(min_val, min(preferred, max_val))`.

These functions can nest: `calc(max(calc(var(--x) + 4px), 10px))`
appears in Wikipedia.

### var()-inside-calc resolution

ADR-026's var() resolver currently walks the top-level value
string and substitutes `var(--name, fallback)` occurrences. It
misses var() inside `calc(...)` because... (root cause
hypothesis: the var-resolver runs before calc is recognized as
a value function, or the resolver's regex/scan doesn't recurse
into parenthesized groups).

**v1 fix**: extend var() substitution to work inside
`calc(...)`, `min(...)`, `max(...)`, `clamp(...)` — the
substitution is string-level and applies to any `var(...)`
occurrence regardless of nesting. Same code path, extended
scope. The specific bug (why ADR-026 missed it) is measured in
Commit 1's diagnostic before implementation.

**Fallback defaults**: `var(--font-size-medium, 1rem)` uses
the `1rem` default when `--font-size-medium` isn't defined.
ADR-026 supports fallback; the diagnostic will confirm whether
fallback resolution works inside calc.

### Reuse of ADR-027's narrow parser

`parse_length_maybe_calc` in `stylesheet.cpp` stays as-is — its
scope is intentionally narrow (@media query conditions only:
`calc(Npx ± Mpx)` → single px number). The general calc
evaluator is a separate function in `style_values.cpp` used by
`parse_length()`. **Two callers, two behaviors, no forced
coupling.** Documented.

**Alternative considered**: refactor `parse_length_maybe_calc`
to use the general parser. Rejected because @media conditions
have different evaluation context (no `em`-to-px conversion
since font-size isn't known at query time — conditions always
resolve against viewport width alone).

## Design decisions

### Why bundle calc + var-inside-calc

Ship them together: (1) both are value-function features, (2)
splitting would mean shipping calc() with a documented "no
visible delta because var() unresolved" caveat, then shipping
var-inside-calc separately with "now visible delta." Two ADRs,
one boolean of visible progress. Bundled, single ADR, single
visible delta — cleaner.

The test suite checks them as separate features (var-inside-calc
tests are about cascade; calc() tests are about arithmetic), so
regression coverage is fine either way.

### Why standard precedence, not CSS spec's "no precedence"

CSS's `calc()` precedence rules are **the same as standard
math**: `*` and `/` bind tighter than `+` and `-`. `calc(2 +
3 * 4)` = 14, not 20. There's no CSS-specific precedence
oddity. Standard recursive descent is correct.

**Alternative** (flat left-to-right): would mishandle
`calc(2 + 3 * 4)` and violate CSS spec. Rejected.

### Why not `calc()` inside `@media` (extends ADR-027's narrow form)

The general parser needs a `LengthContext` with font-size,
parent-width etc. `@media` conditions evaluate at query time
before any element is chosen — no font-size context. Keeping
the narrow parser for @media is honest: the input domain is
smaller, the output must be a plain number.

### Why not vh/vw in v1

`vh` requires `viewport_height` in `LengthContext`, which isn't
threaded through layout today. Adding it is small but touches
every `parse_length` call site (needs viewport_height to flow
down through `layout_element`). Deferred to ADR-038 (which will
likely need it anyway for other features).

Wikipedia uses `vh` once, in `max-height: calc(100vh - (24px *
2))` — a sticky-container size. Sticky is deferred, so no
visible delta from vh today. Zero first-viewport impact.

## Deferred (ADR-038+ candidates)

- **`vh`, `vw`, `vmin`, `vmax`** — need `viewport_height` in
  `LengthContext` (small threading work, own ADR)
- **Percentages in vertical axis inside calc** — needs
  `parent_content_height` in `LengthContext`, same ADR as above
- **`calc()` in `grid-template-columns`** — grid already has
  its own tracker parser in `grid_layout.cpp`; unification
  deferred
- **`calc()` inside `font-size`** — font-size resolution has a
  separate path in `layout_element`; unification deferred
- **`attr()` value function** — used by Wikipedia's `::after`
  content rules; pseudo-elements themselves are ADR-039
- **`counter()` value function** — same dependency
- **CSS-wide math functions `round()`, `mod()`, `rem()`** —
  unused in current target pages

## Consequences

**Better:**

- **Search icon sized correctly.** Currently `width: calc(var(--font-size-medium,1rem) + 4px)` on the search icon box resolves to 0 (unresolved calc), so the icon renders at 2×2. After ADR-037, resolves to `1rem + 4px = 20px`. **First-viewport visible delta.**
- **Search input left padding correct.** Currently `padding-left: calc(8px + 8px + calc(var(--font-size-medium,1rem) + 4px))` = 0. After: `8 + 8 + (16 + 4) = 36px`. Search text doesn't collide with icon.
- **~30 additional declarations apply correctly.** Every `width`, `height`, `margin`, `padding`, `max-width`, `left`, `right` in Wikipedia CSS with calc now resolves.
- **Foundation for future value function work.** `clamp()` for responsive typography, `min()`/`max()` for container queries — all share this parser.
- **Fixes the var()-inside-calc bug in ADR-026.** Fallback resolution works everywhere.

**Accepted limitations (honest, documented):**

- **`vh` / `vw` inside calc** — 0 for current usage. Deferred.
- **Vertical percentages inside calc** — 0 for current usage.
  Deferred.
- **`calc()` in grid track sizing / font-size** — different
  code paths, unchanged.
- **`attr()`, `counter()`** — separate functions, separate ADR.
- **Permissive operator whitespace** — `calc(10px-5px)` accepted
  where CSS spec rejects. Doesn't affect current pages.
- **No `+`/`-` whitespace enforcement** — same.

**Unchanged:**

- ADR-013's `parse_length` non-calc paths (px, em, rem, %, pt,
  auto, normal).
- ADR-026's var() resolution at top-level (extended, not
  replaced).
- ADR-027's narrow @media calc parser (still used for query
  conditions).
- All layout, paint, cascade behavior for non-calc values.

## Implementation plan

Three commits on `feat/calc`:

1. **Calc expression parser + evaluator** — new function in
   `style_values.cpp`: `eval_math_expression(s, ctx) -> float`.
   Recursive descent, all four operators, nested parens, unary
   negation, functions (`calc`, `min`, `max`, `clamp`), units
   (px, em, rem, %, pt). Wire into `parse_length()` for any
   value whose first token is `calc`/`min`/`max`/`clamp`. ~180-220 lines + 15-18 tests.
2. **var()-inside-calc resolution** — diagnostic first (why
   does ADR-026 miss var() inside calc?), then fix in
   `cascade.cpp`. ~30-50 lines + 5-8 tests.
3. **Docs** — ADR-037 status → Implemented, Post-ADR-037
   note in ADR-026 (var-inside-calc fix recorded),
   CHANGELOG, README.

## Implementation

**Implemented 2026-10-27.** Three PRs (spec #191, impl #192,
this docs PR). First spec-driven ADR after the arc's pivot
from "fix Wikipedia" to "implement CSS spec features."

### What shipped

**`eval_math_expression()`** (~280 lines in
`style_values.cpp`):

- Recursive descent, standard precedence (`* /` before `+ -`)
- Nested parens + nested `calc()`/`min()`/`max()`/`clamp()`
- Unary negation
- `min(n-ary)`, `max(n-ary)`, `clamp(ternary)`
- Units: `px`, `em`, `rem`, `%`, `pt` (vh/vw deferred)
- **Every operand converts to px at parse time**, so
  mixed-unit arithmetic (`2em + 8px`) works via
  common-unit-at-parse-time

**`resolve_var_whole()` scan-and-splice extension** (~50
lines in `stylesheet.cpp`):

- Old: only whole-value `var()` resolved
- New: any `var()` occurrence resolves, recursively
- Fixes ADR-026's deferred limitation ("partial-value
  declaration left unchanged")
- Required for `calc(var(--font-size-medium,1rem) + 4px)`
  to work — the exact Wikipedia search-icon case

### Measured visible delta (viewport 1774)

| Check | Before | After |
|---|---|---|
| Search icon `width` | 2 | **20** |
| Search icon `height` | 0 | **0** (unchanged) |

Width grew correctly via calc(). Height stayed at 0
because `layout_element` does not currently apply `height`
from ComputedStyle to non-`<img>` boxes. **Pre-existing gap
unrelated to calc** — calc evaluates the value correctly,
layout just doesn't consume it for the height property on
generic boxes. ADR-035's img-sizing block applies width AND
height, but only for `<img>`.

**Deferred to ADR-040 (box model completion).** Documented
honestly, not expanded in ADR-037's scope.

### What this unblocks (verified via test suite)

- `calc()` arithmetic: `+`, `-`, `*`, `/` with correct
  precedence
- mixed-unit arithmetic (`em + px`, `% + px`, `rem + px`)
- nested `calc()` / `min()` / `max()` / `clamp()`
- unary negation, parenthesized grouping
- `var()` embedded in any value position (calc, composite
  values, nested)
- composite-with-var patterns (`border: 1px solid var(--color)`)
  that ADR-026 deferred

### What stays deferred

- `height` on non-img boxes (ADR-040)
- `vh` / `vw` (need `viewport_height` in LengthContext)
- vertical `%` inside calc (same)
- `calc()` in grid-track sizing / font-size (different code
  paths, unchanged)
- `attr()`, `counter()` (depend on pseudo-elements, ADR-039)

### Test count

- Before: 431 cases / 1765 assertions
- After: **456 cases / 1792 assertions** (+25 / +27)
- Full suite all pass, zero warnings
- CI green on PR and post-merge

### The `test_stylesheet.cpp` transition

One existing test documented ADR-026's deferred behavior:

```cpp
TEST_CASE("var(): partial-value declaration left unchanged (deferred)") {
    CHECK(decl.value == "1px solid var(--border-color, #ccc)");
}
```

ADR-037's scan-and-splice extension resolves that var(). The
test now reads:

```cpp
TEST_CASE("var(): embedded var() in composite value resolved (ADR-037)") {
    CHECK(decl.value == "1px solid #ccc");
}
```

Same pattern as ADR-029's "display_of unknown value" test:
**the contract was deferred, now it's resolved, and the test
name + comment record the transition** rather than being
silently updated.

## Verification

**Unit tests** (new `[css][calc][adr-037]` tag):

- `calc(10px + 5px)` = 15.0
- `calc(2em + 8px)` = 40.0 (with font_size=16)
- `calc(100% - 20px)` = 780.0 (with parent_content_width=800)
- `calc(10px * 2)` = 20.0
- `calc(100px / 4)` = 25.0
- `calc(10px + 5px * 2)` = 20.0 (precedence: `*` before `+`)
- `calc((10px + 5px) * 2)` = 30.0 (parens override)
- `calc(-1 * 12px)` = -12.0 (unary negation)
- `calc(calc(10px + 5px) * 2)` = 30.0 (nested calc)
- `min(10px, 20px)` = 10.0
- `max(10px, 20px)` = 20.0
- `clamp(10px, 50px, 20px)` = 20.0 (clamped to max)
- `clamp(10px, 5px, 20px)` = 10.0 (clamped to min)
- `calc(100vh - (24px * 2))` = 0.0 (vh deferred, returns 0)
- Non-calc `10px` = 10.0 (control, unchanged)
- Non-calc `2em` = 32.0 (control, unchanged)
- Non-calc `auto` = 0.0 (control, unchanged)

**var()-inside-calc tests** (`[css][var][adr-037]` tag):

- `width: calc(var(--size) + 4px)` with `--size: 16px` →
  computed value is `calc(16px + 4px)` → parse_length = 20.0
- `width: calc(var(--missing, 8px) + 4px)` with `--missing`
  undefined → computed value is `calc(8px + 4px)` → 12.0
- `width: calc(var(--size) + calc(var(--size2) + 4px))` with
  both defined → nested var substitution correct

**Visual verification** (viewport 1774, Wikipedia HTML):

| Check | Before | After |
|---|---|---|
| Search icon box | w=2 h=2 (2×2) | **w=20 h=20** |
| Search input left padding | 0 | **36px** |
| First-viewport PNG diff | — | search icon visible, text not colliding |

## Acceptance criteria

ADR-037 is complete when:

- [ ] `eval_math_expression` in `style_values.cpp` handles
      all four operators with correct precedence
- [ ] Nested parens and nested `calc()` supported
- [ ] Unary negation supported
- [ ] `min()`, `max()`, `clamp()` supported (n-ary for
      min/max, ternary for clamp)
- [ ] Units: px, em, rem, %, pt (vh/vw deferred)
- [ ] `parse_length` dispatches to `eval_math_expression`
      when value starts with `calc(`/`min(`/`max(`/`clamp(`
- [ ] var()-inside-calc resolution works (companion fix)
- [ ] 18-20 unit tests added and passing
- [ ] Measured visible delta on Wikipedia (Commit 2 message)
- [ ] Post-ADR-037 note in ADR-026 (var-inside-calc fix)
- [ ] `main` green: 431 + N cases, all pass, zero warnings
- [ ] CI green (PR + post-merge)
- [ ] ADR-037 status → Implemented in Commit 3

**Explicit claim**: search icon sized correctly (2×2 →
20×20), search input padding correct (0 → 36px). Both
first-viewport. First ADR to ship a calc + var value-function
pair.

**Honest caveats**:

1. **`vh` / `vw` deferred** — 0 for current usage.
2. **Vertical percentages deferred** — 0 for current usage.
3. **`calc()` in grid / font-size** — different code paths,
   unchanged.
4. **`attr()` / `counter()`** — separate ADRs.
5. **Search-icon padding-right on `end-icon` variant** — the
   `end-icon` (right-side clear button) uses calc too; visible
   only when input has a value, not on current page. Still
   wired correctly.
