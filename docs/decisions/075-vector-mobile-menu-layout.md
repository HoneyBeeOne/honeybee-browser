# ADR-075: Vector 2022 mobile menu layout at narrow viewport

**Status:** Proposed (hypothesis disproven — see Correction section). Superseded by ADR-076.
**Date:** 2026-11-16
**Related:** ADR-031 (float layout), ADR-074 (float positioning), ADR-073 (permanent tracing)

## Context

Following ADR-074 (float positioning diagnosis), permanent `position` and `ABS_PUSH` traces were added to `layout_element` (ADR-073 policy). Fresh trace at default window (984px content width) revealed the Wikipedia `<h2>History` misplacement is **rooted in the Vector 2022 mobile menu layout**, not float positioning as ADR-074 hypothesized.

### The trace data (2026-11-16, default window at 984px content)

```
mw-navigation        x=24, y=217.9, w=976,  h=0       pos=static
p-navigation         x=16, y=69.0,  w=0,    h=300.4   pos=static
vector-main-menu     x=16, y=16.0,  w=0,    h=718.8   pos=static
bodyContent          x=24, y=531.9, w=976,  h=30418   pos=relative
mw-content-text      x=24, y=575.9, w=976,  h=30302   pos=static
content (main)       x=24, y=449.9, w=976,  h=30500   pos=static
```

### Four anomalies

**A1. Cascade is not applying `position: sticky` to Vector 2022 header elements.**
All header elements report `pos=static`. Vector 2022's CSS declares `position: sticky` on `.mw-header`, `.vector-sticky-header`, and related elements. Firefox at 984px content width shows a working sticky header; HoneyBee cascades `static`. Cause likely: selectors using `:where()`, `:is()`, `@supports`, CSS custom properties, or media queries that our cascade doesn't fully resolve.

**A2. Invalid in-flow geometry for menu children.**
`p-navigation` at `x=16` is 8px LEFT of its parent `mw-navigation` at `x=24`. Its `y=69` is 148px ABOVE parent `y=217.9`. Its `w=0` while `h=300.4`. No in-flow `pos=static` child can produce this geometry. Either cascade is applying `width: 0` (impossible with these heights) or layout is placing them in a different coordinate space.

**A3. Parent does not contain its children.**
`mw-navigation` shows `h=0` despite containing elements with `h=300.4` and `h=718.8`. Real header content lives inside `.vector-dropdown-content` divs which are `position: absolute` (ABS_PUSH trace confirms). Their absolute positioning takes them out of flow, so parent `mw-navigation` correctly has `h=0` per CSS. **This is not the bug.** But it means `mw-navigation`'s contribution to `mw-page-container`'s height is 0, which produces the wrong layout downstream if `mw-page-container` is supposed to reserve vertical space for the mobile menu.

**A4. Menu elements laid out 12+ times.**
`p-navigation` and `vector-main-menu` EXIT the trace ~12 times each, each time with identical geometry. This suggests they are re-laid out on every `ABS_PUSH` occurrence (each dropdown toggles a new `vector-dropdown-content` which triggers layout of the same descendants). Performance cost and geometry-doubling risk.

### The 358px gap

`mw-navigation.y=217.9` → `mw-content-text.y=575.9` = **358px of untraced content.** This gap contributes to but does not fully explain the 1710px History offset at default window (ADR-074 documented the remaining ~565px comes from block-narrowing per ADR-031's approximation, and ~532px more from something else in the header area).

## Decision

**Defer implementation.** This ADR is a decision record: diagnostic findings plus permanent trace additions, no fix.

### Cascade vs layout — unresolved split

Two paths forward, both needed:

**Path A: Cascade completeness (next session, first priority).** Compare Firefox DevTools computed styles on each key element to our `ComputedStyleMap` dump. Specifically for `mw-navigation`, `p-navigation`, `vector-main-menu`, `vector-main-menu-container`, `mw-page-container`, `mw-page-container-inner`:

| Property | Firefox value at 984px | HoneyBee value | Fix |
|---|---|---|---|
| `position` | (to be measured) | `static` | If different: fix cascade selector support (`:where`, `:is`, `@supports`, CSS vars) |
| `display` | (to be measured) | (to be measured) | If different: cascade fix |
| `width` | (to be measured) | `0` for menu children | If different: cascade fix |
| `height` | (to be measured) | `0` / `300` / `718` | If different: cascade + layout coordination |
| `float` | (to be measured) | (to be measured) | Already traced by ADR-074 FLOAT trace |

**Path B: Layout coordinate space (following Path A).** If cascade values match Firefox but geometry is still wrong, the bug is in layout — likely the same class as ADR-071's inline routing (children laid out in absolute vs parent-relative coordinates) or ADR-074's float block-narrowing. Both are open questions.

### Explicit scope split for next session

ADR-075 will likely split into two fix ADRs once the cascade comparison completes:

- **ADR-076 (Proposed, next session): Cascade support for Vector 2022 selectors.** Add `:where`, `:is`, `@supports`, CSS custom property resolution, or media query handling as needed. Enables correct cascade of `position: sticky` and responsive display/width.
- **ADR-077 (Proposed, next session): Layout coordinate space for absolute-dropdown content.** Once cascade values are correct, verify that `mw-page-container` reserves correct vertical space for the mobile menu header, and that menu children get correct width/x coordinates.

### Alternative: accept current behavior

Wikipedia at MAX viewport (1814px content) is closer to Firefox because Vector 2022 exits mobile mode. If full Wikipedia fidelity at all widths is not a priority, ADR-075 can be marked "Deferred — mobile viewport fidelity is post-v1." This is a legitimate decision.

## Permanent traces added (ADR-073 policy)

| Trace | Category | Purpose |
|---|---|---|
| `pos=<static/relative/absolute/fixed/sticky>` appended to `layout_element: EXIT` | LayoutDetail | Cascade's chosen position value per element |
| `ABS_PUSH <tag> id='...' class='...'` | LayoutDetail | Which elements are pushed to `pending_absolutes` |

Both zero-cost when `HONEYBEE_LOG` is unset. Existing `FLOAT` and `NARROW` traces (ADR-074) remain in place.

## What this ADR does NOT do

- Does not fix Vector 2022 cascade support (`:where`, `:is`, `@supports`, CSS vars, media queries)
- Does not fix layout coordinate space for absolute-dropdown content
- Does not fix block-narrowing over-stretch (still tracked in ADR-074 Bug B)
- Does not attempt to make Wikipedia indistinguishable from Firefox at all viewport widths — that's a multi-ADR arc, not a single fix

## Verification category

**Diagnostic-only.** No behavior change, no fix. Permanent traces now enable next-session cascade-vs-layout diagnosis without any new code.

## References

- ADR-031 — float layout (block-narrowing approximation)
- ADR-074 — float positioning diagnosis (symptom; root cause is broader)
- ADR-073 — permanent tracing policy
- ADR-071 — inline routing + culling fixes (same class of coordinate-space bug — potential parallel here)
- Wikipedia Vector 2022 CSS: uses `:where()`, `:is()`, CSS custom properties, media queries for responsive mode

## Correction (same day, post-CSS-analysis)

The primary hypothesis above — *"cascade isn't applying Vector 2022's `position: sticky`"* — is **disproven** by reading Wikipedia's actual stylesheets.

### Evidence

Fetched the two `load.php?modules=...&only=styles&skin=vector-2022` stylesheets (220 KB combined) and extracted feature counts:

| Feature | Count | Where used |
|---|---|---|
| `position: sticky` | **2** | Both `.vector-sticky-pinned-container` (TOC sidebar) |
| `var(--)` | 569 | Everywhere — colors, spacing, breakpoints |
| `@media` | 91 | Multiple forms: `@media all and (min-width:640px)`, `@media screen and (max-width:639px)`, `@media print`, `@media (prefers-reduced-motion:reduce)` |
| `@supports` | 75 | Gates like `@supports (text-decoration:underline dotted)` |
| `:where(` | 16 | Low-specificity wrappers |
| `:has(` | 8 | Parent-selector rules |

**None** of the elements in our trace (`mw-navigation`, `p-navigation`, `vector-main-menu`) have `position: sticky` declared. They're static per CSS. Our cascade correctly reports `pos=static`. The sticky-as-out-of-flow theory was wrong.

### What the trace actually shows

The layout anomalies (`p-navigation` at `x=16, w=0`, `vector-main-menu` at `x=16, w=0`, `mw-navigation` with `h=0`) are consistent with **mobile-only elements being laid out at desktop width**. They should have `display: none` at 984px content width but don't.

### The real cascade gaps

See ADR-076 (Proposed) for the actual measured gap between our cascade and Wikipedia's requirements: `@media` prefixed forms (`all and`, `screen and`, `prefers-*`), `var()` without fallback, `@supports`, `:where()`.

### What this ADR still provides

The permanent `pos=` and `ABS_PUSH` traces added here **directly enabled** the disproving of the sticky hypothesis. Per ADR-073 policy, they remain in place. The trace infrastructure was correct; only the interpretation was wrong. That's how diagnostic work is supposed to behave — hypothesis → measurement → correction → revised hypothesis.

The diagnosis in this ADR was wrong; the diagnostic tooling is correct and permanent. Do not revert the tooling.

