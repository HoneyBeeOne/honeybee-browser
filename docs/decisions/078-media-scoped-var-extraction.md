# ADR-078: Media-scoped and multi-selector custom property extraction

**Status:** Implemented
**Date:** 2026-11-16
**Related:** ADR-026 (var() resolution), ADR-027 (@media), ADR-077 (feature queries)

## Context

ADR-026's `resolve_var_whole()` (stylesheet.cpp:87) resolves `var(--x)` at parse time using `result.variables`, populated by an inline extractor in the top-level `parse()` loop:

```cpp
if (rule.selectors.selectors.size() == 1) {  // single selector
    if (sel.parts.size() == 1 &&
        sel.parts[0].compound.simples.size() == 1 &&
        sel.parts[0].compound.simples[0].kind == SimpleKind::PseudoRoot) {
        // extract --x from this rule
    }
}
```

This has two limitations:

**Gap 1 — Multi-selector rules skipped.** `:root, .skin-invert, .notheme { --color-base: red }` is a valid declaration. Var references to `--color-base` fail because the rule has 3 selectors, not 1.

**Gap 2 — `@media`-scoped rules skipped.** `parse_media_block()` (stylesheet.cpp:1224) pushes rules into `result.rules` but never runs the var extractor. So `@media screen { :root { --color-base: red } }` — a valid declaration at any viewport — never reaches `result.variables`.

### Measured impact

From Wikipedia Vector 2022 CSS (220 KB, ADR-076 diagnostic):

- **569 total `var()` uses**
- 523 with fallback (work around missing var)
- **46 without fallback** — silently fail
- 29 unique custom properties used without fallback

Section 4 of ADR-078 diagnostic shows their definitions:

| Variable | Defined at |
|---|---|
| `--color-base` | `:root,.skin-invert,.notheme` (inside `@media screen`) |
| `--color-visited` | same |
| `--color-destructive` | same |
| `--border-color-interactive` | same |
| `--font-size-medium` | `html.vector-feature-custom-font-size-clientpref-*` |
| `--image-size-small` | `:root` (top-level — already working) |

**5 of the 6 failing variables would be fixed by collecting from `@media screen` + multi-selector `:root` rules.** Their values cascade into everything downstream: link colors, visited states, borders, font sizes.

## Decision

**Two changes:**

### 1. Move var extraction to a post-pass over `result.rules`

Replace the inline extractor with a post-pass that runs after all rules are parsed:

```cpp
for (const auto& rule : result.rules) {
    // Must contain :root somewhere in its selectors
    bool has_root_selector = false;
    for (const auto& sel : rule.selectors.selectors) {
        if (sel.parts.size() == 1 &&
            sel.parts[0].compound.simples.size() == 1 &&
            sel.parts[0].compound.simples[0].kind == SimpleKind::PseudoRoot) {
            has_root_selector = true;
            break;
        }
    }
    if (!has_root_selector) continue;

    // Must apply at any viewport width (no width constraints)
    // and match our feature environment
    if (rule.media.has_value()) {
        const auto& mq = *rule.media;
        if (mq.min_width_px || mq.max_width_px) continue;  // width-dependent — deferred
        if (mq.q_hover_matches && !*mq.q_hover_matches) continue;
        if (mq.q_pointer_matches && !*mq.q_pointer_matches) continue;
        if (mq.q_light_matches && !*mq.q_light_matches) continue;
        if (mq.q_no_reduced_motion_matches && !*mq.q_no_reduced_motion_matches) continue;
        if (mq.q_no_high_contrast_matches && !*mq.q_no_high_contrast_matches) continue;
    }

    // Extract custom property declarations
    for (const auto& decl : rule.declarations) {
        if (decl.property.size() >= 2 &&
            decl.property[0] == '-' && decl.property[1] == '-') {
            result.variables[decl.property] = decl.value;
        }
    }
}
```

**Why skip width-constrained `@media`?** Parse-time var collection runs before the viewport is known. If we collect `@media (min-width: 640px) { :root { --x: large } }` at parse time, refs at viewport < 640 use the wrong value. Skip is safer; those need cascade-time resolution (deferred).

**Why include feature-constrained `@media`?** Feature flags (`q_hover_matches` etc.) already reflect our environment. If `q_light_matches == true`, the rule would apply at cascade. Collecting is correct.

### 2. Add custom properties to `is_inherited`

```cpp
bool is_inherited(std::string_view property) {
    // ADR-078: CSS custom properties inherit per CSS Variables spec.
    if (property.size() >= 2 &&
        property[0] == '-' && property[1] == '-') {
        return true;
    }
    return property == "color" ||
           property == "font-family" ||
           property == "font-style" ||
           property == "font-weight" ||
           property == "line-height" ||
           property == "visibility";
}
```

Correctness improvement. Does not affect the Wikipedia fix (custom properties are globally collected at parse time; inheritance is a cascade-time concept) but is CSS-correct and would matter for future cascade-time resolution.

## What this fixes

- `--color-base`, `--color-visited`, `--color-destructive`, `--border-color-interactive` — defined in `:root,.skin-invert,.notheme` inside `@media screen`. Now collected. Refs without fallback resolve.
- Multi-selector `:root, .foo` patterns in any page.
- `@media screen { :root { ... } }` in any page.
- `@media (prefers-color-scheme: light) { :root { ... } }` — collected (our env is light).
- `@media (prefers-color-scheme: dark) { :root { ... } }` — **correctly skipped**.

## What this does NOT fix

- **`@media (min-width: ...) { :root { ... } }`** — width-dependent var collection remains deferred (needs cascade-time resolution).
- **Wikipedia's History position** — the mobile menu anomaly is 076-D (`:where`/`:has`), and the History 1400px offset is ADR-074 Bug B (line-wrap).

## Verification

1. **Unit tests** (test_stylesheet.cpp): 5 new cases covering each gap:
   - `@media screen { :root { --x } }` collected
   - `:root, .foo { --x }` collected
   - `@media (min-width: 640px) { :root { --x } }` **NOT** collected (deferred)
   - `@media (prefers-color-scheme: dark) { :root { --x } }` **NOT** collected
   - `@media (prefers-color-scheme: light) { :root { --x } }` collected
2. **Regression**: existing 702 tests should stay green
3. **Wikipedia smoke**: visual inspection — colors, borders, link states should be closer to Firefox (fewer `var()` failures)

## References

- ADR-026 — var() resolver (parse-time, whole-value)
- ADR-027 — @media parser
- ADR-077 — feature queries (q_* flags used here)
- ADR-076 — cascade gaps roadmap
