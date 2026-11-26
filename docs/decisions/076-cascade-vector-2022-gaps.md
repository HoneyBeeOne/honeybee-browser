# ADR-076: Cascade completeness for Vector 2022

**Status:** Proposed (diagnosed)
**Date:** 2026-11-16
**Related:** ADR-027 (@media), ADR-073 (tracing), ADR-074 (float positioning), ADR-075 (superseded hypothesis)

## Context

ADR-075 proposed that Wikipedia's mobile-menu layout bug was caused by cascade not applying `position: sticky` to header elements. **Direct CSS analysis disproved this** — only 2 sticky declarations exist in Vector 2022's 220 KB of stylesheets, both on `.vector-sticky-pinned-container` (TOC sidebar). Header elements are correctly `pos=static`.

The actual cascade gaps were measured by fetching Wikipedia's `load.php` stylesheets and counting feature usage:

| Feature | Count in Vector 2022 CSS | Our support |
|---|---|---|
| `var(--)` | 569 | **None** (fallbacks mostly save us) |
| `@media` | 91 | Partial (`min-width`/`max-width` only; prefixed forms `all and`, `screen and`, `prefers-*` likely unsupported) |
| `@supports` | 75 | **None** (gated rules always fall through to base) |
| `:where(` | 16 | **None** (would need to treat as `*` with 0 specificity) |
| `:has(` | 8 | **None** (parent selectors) |
| `position: sticky` | 2 | Enum exists, but layout doesn't currently use it (only TOC needs it) |

Any of these failing can contribute to the Wikipedia layout offset. The trace evidence (mobile menu elements laid out at desktop width, `mw-navigation` at `h=0`) is most consistent with `@media` conditions not being correctly evaluated for prefixed forms.

## Decision

**Defer implementation to a future session.** This ADR is a decision record with measured scope, no fix.

### The four sub-tasks

**076-A. `@media` prefixed forms.**
ADR-027's parser (`parse_media_block` at stylesheet.cpp:1164) supports `min-width` / `max-width` in the direct form. Wikipedia uses many prefixed forms:
- `@media all and (min-width: 640px)`
- `@media screen and (max-width: 639px)`
- `@media screen and (prefers-color-scheme: dark)`
- `@media (prefers-reduced-motion: reduce)`

If the parser fails on `all and` / `screen and` prefixes, the rules are either dropped or treated as unconditional — both break layout. **Smallest, highest-impact of the four.** Likely one session, 60-100 lines.

**076-B. CSS custom properties (`var()`).**
569 uses. Many with fallback (`var(--color-progressive,#36c)`) so masked. Rules without fallback silently fail. Need:
- Parse `--name: value` declarations (already stored in ComputedStyleMap?)
- Resolve `var(--name)` in property values during cascade
- Inherit custom properties per CSS spec (`--*` inherit from parent)

One-to-two sessions.

**076-C. `@supports` evaluation.**
75 uses. Gates progressive-enhancement rules like `@supports (text-decoration:underline dotted)`. Without evaluation, both branches may apply (last wins) or the wrong branch wins. Small — ~50 lines to parse the condition and evaluate against a hardcoded capability set.

**076-D. `:where()` / `:has()` selectors.**
16 + 8 uses. `:where()` requires zero-specificity handling. `:has()` is rarely load-bearing for layout. Lower priority. Defer to a follow-up ADR.

### Verification path

For each sub-task, re-run Wikipedia at 984px content width and check:
- `p-navigation`, `vector-main-menu`, `mw-navigation`: `display` value matches Firefox?
- Vertical offset of `<h2>History`: should approach `y≈500` (Firefox) from `y≈2210` (current)

The permanent traces (`FLOAT`, `NARROW`, `pos=`, `ABS_PUSH`) already give full observability. No new traces needed for these ADRs.

### Order of implementation

**076-A first.** If `@media all and (min-width:...)` isn't matching, mobile-only rules are applying at desktop widths, which would explain most of the layout anomalies. This is the highest-leverage fix.

**076-B second.** Once mobile-only rules are correctly filtered, custom property resolution fixes remaining color/spacing mismatches.

**076-C third.** Minor fallback rules.

**076-D last.** Rarely load-bearing.

## What this ADR does NOT cover

- **`client-nojs` / `client-js` state.** Wikipedia's HTML is fetched in `client-nojs` state because we don't execute JavaScript. Firefox flips this at page load. Some Vector 2022 rules are gated on `client-js`. That's a separate architectural gap and would require either JS execution (a browser engine change, not a CSS change) or a pre-emptive class-swap heuristic. Not in scope here.
- **`position: sticky` layout support.** The enum exists (`Position::Sticky`), but layout doesn't use it. Only 2 rules use sticky, both on TOC. Lower priority than the four sub-tasks above.
- **`:has()` semantics.** Rarely load-bearing in Vector 2022 layout.

## References

- ADR-027 — @media query support
- ADR-073 — permanent tracing policy
- ADR-074 — float positioning diagnosis
- ADR-075 — superseded sticky hypothesis (see Correction section)
- Wikipedia Vector 2022 stylesheet analysis (`/tmp/wiki-css/merged.css`, 220 KB + 22 KB inline)

## Implementation status update (2026-11-25)

This addendum records implementation work completed after the
original diagnosis dated 2026-11-16. The original measurements,
diagnosis, four subtasks, and implementation priorities above are
preserved as historical documentation. This section describes the
subsequent implementation status; it does not retroactively change
the original diagnosis.

### 076-A. `@media` support

The original diagnosis identified uncertainty around media-type
prefixes and feature queries.

Subsequent implementation records clarify the actual support:

- **ADR-027** already supports `screen`, `all`, `and`, `only`,
  `min-width`, and `max-width`.
- **ADR-077** adds selected environment-dependent feature queries:
  `hover`, `pointer`, `prefers-color-scheme`,
  `prefers-reduced-motion`, and `prefers-contrast`.

The following remain deferred or unsupported:

- `print`
- `orientation`
- `aspect-ratio`
- `resolution`
- Comma-separated media queries
- The `not (...)` media-query prefix
- Other media features not implemented by ADR-027 or ADR-077

Therefore, the original concern about `all and` and `screen and`
prefixes is superseded by the existing parser support. Media-query
support is still partial, but the remaining gaps are narrower and
more precisely identified.

### 076-B. CSS custom properties (`var()`)

The original diagnosis identified custom-property resolution and
extraction as major gaps. Subsequent ADRs implemented substantial
parts of this work:

- **ADR-026:** `:root` variable extraction and parse-time `var()`
  resolution, including fallback handling.
- **ADR-037:** Recursive resolution of embedded `var()` references
  in composite values and mathematical expressions.
- **ADR-078:** Post-parse extraction from multi-selector rules and
  applicable feature-scoped `@media` rules. Custom properties were
  also added to the inherited-property set.

The following limitations remain:

- Custom-property extraction from width-constrained `@media` rules
  is deferred because the correct value depends on the viewport.
- Fully dynamic, element-scoped custom-property cascading is not
  implemented. The current parse-time resolution architecture
  does not provide complete CSS custom-property cascade semantics.
- Merging custom-property maps across stylesheets remains a
  documented limitation in ADR-026.

Consequently, the original count of `var()` occurrences describes
the stylesheet examined during the initial diagnosis, not the
number of unresolved occurrences in the current implementation.

### 076-C. `@supports` evaluation

**ADR-050 implemented `@supports` parsing and feature-query
evaluation** against a hardcoded capability table.

The implementation supports property queries, `not`, `and`, `or`,
and simple `selector(:pseudo)` queries. It also handles
`@supports` nested inside `@media`.

The following remain deferred:

- Runtime feature detection through actual capability probes.
- Complex selector arguments in `selector(...)`.
- Nested `@supports` inside another `@supports`.
- Additional feature-query forms such as `font-tech()` and
  `font-format()`.

The original diagnosis that `@supports` blocks were entirely
unsupported is therefore historical; the core implementation is
now in place.

### 076-D. Logical pseudo-classes

**ADR-038 implements full selector-list arguments for
`:not()`, `:is()`, and `:where()`**, including combinators,
nested logical pseudo-classes, and recursive specificity
calculation. `:where()` contributes zero specificity.

The implementation and its limitations are documented in ADR-038.
In particular, forgiving selector-list parsing and pseudo-elements
inside logical pseudo-class arguments remain deferred.

`:has()` is still unsupported. Relative-selector parsing and
relational matching remain future work.

### Current status summary

| Area | Current status | Remaining work |
|---|---|---|
| 076-A `@media` | Partially implemented | Additional media features and query forms |
| 076-B `var()` | Core resolution implemented | Width-dependent and fully dynamic custom properties |
| 076-C `@supports` | Implemented | Runtime detection and advanced query forms |
| 076-D logical pseudos | `:not()`, `:is()`, `:where()` implemented | `:has()` and remaining selector semantics |

### Relationship to the original diagnosis

The original Wikipedia layout investigation and its measured
coordinates remain historical evidence. The subsequent CSS
implementations do not, by themselves, establish that the original
Wikipedia layout anomaly has been resolved.

The original verification path therefore remains useful for
future integration testing. Any claim that a particular
implementation fixes the observed layout should be supported by
fresh comparison against Firefox and the existing HoneyBee traces.

### Related implementation records

- ADR-026 — CSS variable resolution
- ADR-027 — `@media` block support
- ADR-037 — CSS math functions and embedded variable resolution
- ADR-038 — pseudo-class expansion and logical selector lists
- ADR-050 — `@supports` conditional rules
- ADR-077 — media feature queries
- ADR-078 — media-scoped and multi-selector custom-property extraction
