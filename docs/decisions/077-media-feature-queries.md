# ADR-077: Media feature queries

**Status:** Implemented
**Date:** 2026-11-16
**Related:** ADR-027 (@media width queries), ADR-073 (tracing), ADR-076 (cascade gaps)

## Context

ADR-027's `parse_media_prelude` supports `min-width` / `max-width` conditions and media keywords `screen`, `all`, `and`, `only`. Any prelude containing `prefers-`, `hover`, `pointer`, `print`, `aspect-ratio`, `orientation`, `resolution`, or range syntax (`>`, `<`) is dropped at parse time (whole-rule safe drop).

For layout-critical width queries, this is correct — all 138 of Wikipedia's width-based preludes are accepted, and layout-relevant responsive rules apply as expected.

**But there's a real bug in the rejected set:** `@media (prefers-color-scheme: light) { ... }` is currently dropped entirely. If a page has:

```css
body { color: white; }
@media (prefers-color-scheme: light) { body { color: black; } }
```

HoneyBee renders `white` (light rule dropped). Firefox (in light mode) renders `black`. **Real visual divergence from correct CSS behavior.**

The `prefers-` prefix is on the fast-reject list, which drops BOTH `:dark` (correct — we don't support dark) AND `:light` (wrong — we DO render light mode).

Similarly:
- `@media (hover: hover) { ... }` — some sites use to style hover-capable UI. We ARE hover-capable. Rule should apply.
- `@media (pointer: fine) { ... }` — we have a fine pointer (mouse). Rule should apply.
- `@media (prefers-reduced-motion: no-preference) { ... }` — we don't reduce motion. Rule should apply.

None of these affect Wikipedia's specific layout, but all are correct CSS that any page could use.

## Decision

Extend `MediaQuery` with **feature-query support** for a fixed set of environment-derived booleans:

- `hover: hover` (true) / `hover: none` (false)
- `pointer: fine` (true) / `pointer: coarse` (false)
- `prefers-color-scheme: light` (true) / `dark` (false)
- `prefers-reduced-motion: no-preference` (true) / `reduce` (false)
- `prefers-contrast: no-preference` (true) / `more` (false) / `less` (false)

**Environment is fixed** because HoneyBee renders as a desktop screen browser in light mode with no accessibility preferences set:
- hover-capable (mouse / trackpad)
- fine pointer
- light mode
- no reduced motion
- no high contrast

The `MediaQuery` struct stores, per feature, whether the queried value **matches** our environment. Rules are rejected at cascade time when the query asks for a value we don't match.

### Parser changes

`parse_media_prelude`:
- Remove `"prefers-"`, `"hover"`, `"pointer"` from the fast-reject keyword list (they're now parsed)
- Keep `"print"`, `"aspect-ratio"`, `"orientation"`, `"resolution"`, `">"`, `"<"` in fast-reject (still unsupported)
- Add feature-query branches in the paren handler for each feature, parsing `(feature: value)` into the new `MediaQuery` fields
- Unknown features still reject (safe fallback per CSS spec)

### Matcher changes

`media_matches`:
- After width checks, test each feature flag: reject if the flag exists and equals `false`

### What this fixes

- `@media (prefers-color-scheme: light) { body { color: black } }` — now applies. Previously dropped.
- `@media (hover: hover) { a:hover { ... } }` — now applies. Previously dropped.
- `@media (pointer: fine) { ... }` — now applies. Previously dropped.
- `@media (prefers-reduced-motion: no-preference) { * { animation: ... } }` — now applies.

### What this does NOT fix

- **Wikipedia's mobile menu layout anomaly.** All layout-relevant queries in Vector 2022 are `min-width` / `max-width`, already correctly accepted. The mobile-menu-at-desktop-width bug is elsewhere (see ADR-075 Correction, ADR-076 sub-tasks B/C/D).
- **`prefers-color-scheme: dark` rules still correctly skipped** (we render light mode).
- **`print` still skipped** (screen-only render).

## Not in scope (deferred)

- `orientation: portrait | landscape` — needs viewport height
- `aspect-ratio: X/Y` — needs viewport height
- `resolution: Xdpi` — assume 96 dpi, match if range includes it (medium value, low usage)
- `scan`, `color`, `monochrome` — rarely used
- `not (...)` prefix — few sites use
- Comma-separated queries (`@media screen, print`) — currently rejected

## Verification

1. **Unit tests** (`test_cascade.cpp`): 8 new cases covering each feature in both matching and non-matching forms
2. **Updated tests** (`test_stylesheet.cpp`): the two "prefers-* is rejected" tests now assert "parses with feature flag set" (rule present, won't match at cascade)
3. **Wikipedia smoke test**: page should render identically to pre-ADR-077 (all layout queries unaffected, only previously-dropped non-layout rules change)

## References

- ADR-027 — @media width support
- ADR-073 — permanent tracing policy (MEDIA prelude/accept/reject traces added here)
- ADR-076 — cascade gaps (this ADR closes 076-C's `@supports` deferral indirectly — actually no, `@supports` is separate; @media feature queries are new)

Note: ADR-076-C (at-supports) is separate work, not covered here.
