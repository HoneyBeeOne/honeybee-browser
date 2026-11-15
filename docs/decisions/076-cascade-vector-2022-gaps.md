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
