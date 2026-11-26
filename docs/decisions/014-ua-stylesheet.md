# ADR-014: User-Agent Stylesheet

**Status:** Accepted
**Date:** 2026-10-20
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-007 (DOM), ADR-009 (stylesheet parser), ADR-010 (cascade), ADR-011 (layout), ADR-013 (length units)

## Context

v0.1.0 shipped with the render pipeline working end-to-end: parse, cascade, layout, paint. `docs/current-state.md` documents what the pipeline renders and what it doesn't. The most visible gap on that page is the UA stylesheet.

Without a UA stylesheet, an unstyled HTML page looks nothing like it does in Firefox. A plain `<h1>Title</h1>` renders at 16px, same as `<p>`. There is no paragraph gap between elements. There is no `body` margin. Every one of these values comes from a UA stylesheet that browsers have shipped since the mid-1990s.

Authors write their CSS against the UA defaults. A page with `p { color: red }` and no font-size rule expects `<p>` to inherit a sensible size from `<body>`, which inherits from `<html>`, which defaults to 16px. That chain exists in every browser. HoneyBee doesn't have it yet.

This ADR decides how we add it, and the cascade change the addition requires.

## Decision

Two coordinated changes:

1. **Ship a UA stylesheet** as a first-class module (`renderer/css/ua_stylesheet.{hpp,cpp}`), containing high-leverage defaults.
2. **Make the cascade origin-aware**: `cascade(ua_sheet, author_sheet, doc)` -- for normal declarations, author rules override UA rules **regardless of specificity**. Per CSS 2.1 section 6.4.1.

### Scope of v1 (the UA stylesheet content)

~20 rules covering elements that layout already renders (block-only):

| Element | Declarations |
|---|---|
| `body` | `margin: 8px` |
| `h1` | `font-size: 2em; font-weight: bold; margin: 0.67em 0` |
| `h2` | `font-size: 1.5em; font-weight: bold; margin: 0.75em 0` |
| `h3` | `font-size: 1.17em; font-weight: bold; margin: 0.83em 0` |
| `h4` | `font-weight: bold; margin: 1.12em 0` |
| `h5` | `font-size: 0.83em; font-weight: bold; margin: 1.5em 0` |
| `h6` | `font-size: 0.67em; font-weight: bold; margin: 1.67em 0` |
| `p` | `margin: 1em 0` |
| `em`, `i`, `cite`, `var`, `address` | `font-style: italic` |
| `strong`, `b` | `font-weight: bold` |
| `code`, `pre`, `kbd`, `samp`, `tt` | `font-family: monospace` |
| `ul`, `ol` | `margin: 1em 0; padding-left: 40px` |
| `a` | `color: #0000EE` |

**Note on `font-weight: bold` / `font-style: italic`**: these declarations cascade correctly and are stored in `ComputedStyle`. They don't yet render, because `LayoutText` doesn't carry `font_weight` or `font_style` into paint. A future font-metrics ADR will consume them. Declaring them now means the UA sheet matches CSS 2.1's intent, and the future ADR only needs to touch layout and paint, not this sheet.

### Deferred

- `display: inline` vs `display: block` -- every element is still block in layout (ADR-011's scope)
- `display: none` for `<head>` -- the hardcoded skip list from PR #110 stays (see Open Questions)
- Table elements: `table`, `tr`, `td`, `th`, `caption`, `thead`, `tbody`
- Form elements: `input`, `button`, `select`, `textarea`
- List markers (`list-style-type`) -- no marker drawing yet
- `text-decoration: underline` on `<a>` -- paint doesn't support it
- `a:link`, `a:visited` distinction -- pseudo-classes deferred; v1 applies `a { color: #0000EE }` unconditionally
- `<pre>` whitespace preservation -- requires `white-space: pre`
- UA `!important` rules (accessibility defaults) -- not needed yet
- Full CSS 2.1 UA stylesheet (100+ rules)

### Cascade origin order

```
   Normal declarations (this ADR):

       UA  <  Author   -- author wins, regardless of specificity

   Important declarations (future ADR):

       Author !important  <  UA !important   -- spec: UA wins,
                                                protects accessibility

   v1 implements the first line only.
```

The `beats()` function gains origin as a tiebreaker **after** importance and **before** specificity:

```cpp
bool beats(const CascadeValue& a, const CascadeValue& b) {
    if (a.important != b.important) return a.important;
    if (a.origin != b.origin) return a.origin == Origin::Author;
    if (a.specificity == b.specificity) {
        return a.source_order > b.source_order;
    }
    return b.specificity < a.specificity;
}
```

`CascadeValue` gains an `Origin` field. `cascade()` takes two stylesheets and applies UA rules first, then author rules, tracking which origin each `CascadeValue` came from.

### Why a module, not an inline string

The UA stylesheet will grow to 100+ rules over the project's life. It is a document -- deserves its own file, its own tests, its own review history.

An inline string in `render_pipeline.cpp` cannot be tested independently, cannot be reused by a future headless renderer entry point, and is invisible to the codebase as documentation. A module file has its content visible in `git log` when it changes, has its tests next to it, and any future entry point gets the same defaults.

## Rationale

### 1. Origin-aware cascade is correct; concatenation is subtly wrong

The simplest implementation would concatenate:

```cpp
auto sheet = Stylesheet::parse(ua_css + author_css);
```

This works for most pages, but breaks on a specific case:

- UA: `h1 { color: navy }` -- specificity (0,0,1)
- Author: `* { color: red }` -- specificity (0,0,0)

**Concatenation**: UA rule first, author rule second. `beats()` checks specificity first, so author's (0,0,0) loses to UA's (0,0,1). The h1 stays navy. **Wrong per spec.**

**Origin-aware**: author origin beats UA origin before specificity is checked. The h1 becomes red. **Correct per spec.**

This case is not common, but it is the specific case the CSS 2.1 spec calls out, and it will bite anyone who writes `* { box-sizing: border-box }` in a reset -- a very common pattern. Getting this right now is cheap; retrofitting later is not.

### 2. `font-size` in UA and `em` in author rules interact correctly

ADR-013 established that `font-size: 1.5em` resolves against parent font-size. A UA rule like `h2 { font-size: 1.5em }` sets the h2's own font-size to 1.5 times body's computed value. An author rule like `h2 { padding: 2em }` resolves 2em against the h2's own 1.5x-parent font-size. That chain works automatically because ADR-013 put font-size computation in layout, not cascade.

Before ADR-013, `h2 { font-size: 1.5em }` would have been impossible to implement correctly. This ADR is possible because ADR-013 landed.

### 3. UA defaults make every page look more like its author intended

The visible effect is: every unstyled or lightly-styled page gains heading hierarchy, paragraph gaps, and body margin. A page that currently renders as a text dump becomes a page with structure. That is the single highest-value visible improvement available on the current feature base.

### 4. Scope to properties layout/paint consume

Every declaration in v1's UA stylesheet either flows into `LayoutText` (font-size, color, font-family), flows into `LayoutBox` (margin, padding, border, background), or is a forward declaration for a future font-metrics ADR. Nothing is a no-op that wasn't considered.

## Alternatives Considered

### Concatenate UA CSS with author CSS

**Rejected.** Breaks on author `* { color: red }` rule (see rationale 1). Works for most pages, fails for the CSS reset pattern.

### Inline the UA string in `render_pipeline.cpp`

**Rejected.** A document deserves its own file. See Decision.

### Defer UA stylesheet entirely, require authors to style everything

**Rejected.** Every real HTML page relies on UA defaults. A `<p>` without CSS should have a paragraph gap. A page that requires author CSS to look like anything is an HTML viewer, not a browser.

### Full CSS 2.1 UA stylesheet (~100 rules)

**Rejected for v1.** Covers tables, forms, details/summary, ruby, dialog. Layout renders none of those. Ship what layout consumes; extend when layout supports more elements.

### `display: none` for `<head>` in the UA stylesheet

**Rejected for v1.** Requires layout to model `display: none`. The hardcoded skip list from PR #110 handles this today. Retiring it is a separate ADR.

### UA `!important` rules

**Deferred.** The spec has UA `!important` winning over author `!important` (protects focus outlines, form styling). v1 has no UA `!important` rules, so the behavior doesn't matter yet.

## Consequences

### Files changed

- `src/renderer/css/ua_stylesheet.hpp` (new) -- `const Stylesheet& ua_stylesheet();`
- `src/renderer/css/ua_stylesheet.cpp` (new) -- string constant, lazy parse, stable reference
- `src/renderer/css/cascade.hpp` -- `Origin` enum; `CascadeValue::origin`; `cascade(ua, author, doc)` signature
- `src/renderer/css/cascade.cpp` -- `beats()` origin tiebreaker; two-pass `compute_styles`
- `src/browser/render_pipeline.cpp` -- pass `ua_stylesheet()` as first argument
- `src/CMakeLists.txt` -- `ua_stylesheet.cpp` in `honeybee_lib`
- `tests/test_cascade.cpp` -- updated for new signature; new origin tests
- `tests/test_ua_stylesheet.cpp` (new) -- parses to expected rule count, spot-checks content
- `tests/CMakeLists.txt` -- add `test_ua_stylesheet.cpp`

No changes to `renderer::dom`, `renderer::layout`, `renderer::paint`, `renderer::html`, or `renderer::css::selectors`/`stylesheet`.

### Behavior examples (unstyled HTML)

| HTML | Before ADR-014 | After ADR-014 |
|---|---|---|
| `<h1>Title</h1>` | 16px, no margin | 32px, margin 0.67em 0 |
| `<p>Text</p>` | 16px, no margin | 16px, margin 1em 0 |
| `<body>text</body>` | content touches edges | 8px margin all around |
| `<em>hi</em>` | upright | italic (declared, not yet rendered) |
| `<a href=x>link</a>` | black | #0000EE blue |
| `<h1>` with author `h1 { color: navy }` | navy | navy |
| `<h1>` with author `* { color: red }` | (no author rule match) | red (author origin beats UA) |

### What layout doesn't change

Block-flow algorithm is unchanged. `<h1>` still stacks as a block, `<p>` still stacks as a block. The difference is their computed font-size and margin values, which layout reads as it does today.

### What paint doesn't change

Paint reads `color`, `background-color`, `border-color`, `font-size`, `font-family` from `LayoutText`/`LayoutBox` -- all already flowing. `font-weight` and `font-style` are declared in UA but don't flow into `LayoutText` (that's a font-metrics ADR).

## Open Questions

### Retiring the `<head>` skip list

PR #110 introduced a hardcoded skip list in `layout.cpp` for elements that shouldn't render: `head`, `style`, `script`, `title`, `meta`, `link`, `base`. Once layout supports `display: none`, the UA stylesheet can declare those as `display: none` and the skip list can be deleted. That is a `display: none` ADR, not this one.

### User stylesheets

CSS 2.1 has three origins: UA, author, user. User stylesheets (reader modes, accessibility overrides) come with their own ADR. When they land, `Origin` enum gains a third value, and the cascade order becomes: UA < user < author for normal; UA-important > user-important > author-important for important.

### Default font

`body { font-family: sans-serif; font-size: 16px }` -- should we include this? Without it, fonts come from Pango's default (sans-serif, effectively 16px). Explicit is better for debugging, but adds declarations with no visible effect. Defer.

### `<pre>` whitespace

`pre { white-space: pre }` requires layout to preserve whitespace, which v1 doesn't do. Defer the `pre` rule until a whitespace-preservation ADR lands.

### `<h1>` default size across browsers

Firefox, Chrome, and Safari all use `font-size: 2em` for `<h1>`. Our choice matches the universal default.

## Record of Changes

- **2026-10-20:** Initial decision.
