# Architecture Decision Records

Every significant design choice in HoneyBee is recorded as an ADR. They read as a coherent design record: each references the ones before it.

| # | Title | Summary |
|---|---|---|
| 001 | [Technology Stack](001-tech-stack.md) | lexbor + Cairo + Pango + GTK, C++20 |
| 002 | [Language Policy](002-language-policy.md) | Arabic for user-facing text, English for identifiers |
| 003 | [README Language](003-readme-language.md) | Historical -- superseded by 002's policy |
| 004 | [HTML Rendering Strategy](004-html-rendering.md) | Superseded by ADR-006 |
| 006 | [Return to ADR-001](006-return-to-adr-001.md) | Reverted the custom parser; back to lexbor |
| 007 | [DOM Ownership Model](007-dom-ownership.md) | HoneyBee owns the DOM, not lexbor |
| 008 | [CSS Selector Matching](008-css-selectors.md) | Implement selectors ourselves |
| 009 | [CSS Rule Parsing](009-css-parser.md) | Implement stylesheet parser ourselves |
| 010 | [The Cascade](010-cascade.md) | Importance > specificity > source order |
| 011 | [The First Paint](011-first-paint.md) | Two-stage layout + paint pipeline |
| 012 | [Shorthand Expansion](012-shorthand-expansion.md) | Parse-time expansion of box shorthands |
| 013 | [Length Units and Auto Margins](013-length-units.md) | em/rem/%/auto with LengthContext |
| 014 | [User-Agent Stylesheet](014-ua-stylesheet.md) | UA defaults + origin-aware cascade |
| 015 | [Text Wrapping](015-text-wrapping.md) | Word-level line breaking + RTL heuristic |
| 016 | [Inline Formatting](016-inline-formatting.md) | Inline flow, display property, dir attribute |
| 017 | [Display None](017-display-none.md) | Hide `head`, `style`, `script`, `title`, `meta`, `link`, `base`, `template` |
| 018 | [State Pseudo-Classes](018-pseudo-classes.md) | `:link`, `:visited`, `:hover` with a state-tracking model |
| 019 | [Back and Forward Navigation](019-back-forward-navigation.md) | Session-scoped history stack in the browser layer |
| 020 | [Page Caching for Back/Forward](020-page-cache.md) | Session-scoped URL to HTML cache; Back/Forward instant |
| 021 | [Real-Page Interaction](021-real-page-interaction.md) | Scrolling + incremental restyle for large pages |
| 022 | [Targeted Cascade for Hover](022-targeted-cascade.md) | Re-cascade only changed elements; ~40x faster hover |
| 023 | [Skip-Subtree Optimization for Hover](023-skip-subtree.md) | Precompute hover-sensitive elements; wrapper hovers fast too |
| 024 | [Hover-Match Transition Check](024-hover-transition-check.md) | Cascade only elements whose :hover match changed; wrapper hovers ~25ms |
| 025 | [External CSS Loading](025-external-css-loading.md) | Load `<link rel=stylesheet>`, merge with inline CSS |
| 026 | [CSS Variable Resolution](026-css-variables.md) | `:root` + `var()` via fallbacks; ~92% resolution |
| 027 | [@media Block Support](027-media-queries.md) | Parse @media, filter by viewport; 52% of blocks fire at 1024 |
| 028 | [CSS Grid Layout (Basic)](028-grid-layout.md) | `display: grid` + track sizing + named areas; sidebar transform |
| 029 | [Flex Layout (Basic)](029-flex-layout.md) | `display: flex` + single-line algorithm; header bar layout |
| 030 | [Feature-Class Extensions + Skip-Link Hide](030-feature-class-extensions.md) | Second viewport flip (`page-tools-pinned`); Tools panel collapse |
| 031 | [Float Layout (v1) + Stray Closing-Tag Fix](031-float-layout.md) | `float: left\|right` block-level; infobox right-anchor |
| 032 | [Visibility + Opacity Paint Support](032-visibility-opacity.md) | Paint culling for `visibility:hidden` / `opacity:0`; precondition for `:checked` |
| 033 | [Position Layout (relative + fixed)](033-position-relative-fixed.md) | `position: relative` (no-op + context); `fixed` (viewport-anchored out-of-flow); `absolute` deferred |
| 034 | [`<img>` Element Paint + `position: absolute`](034-img-absolute.md) | PNG image fetch/decode/paint (paint-time); absolute containing-block tracking |
| 035 | [Layout-Integrated `<img>`](035-img-layout.md) | Intrinsic dims reserve space; tagline unhide; thumbnails no longer overlap |
| 036 | [Attribute Selectors + `clear`](036-attr-clear.md) | `[attr~=val]` etc.; float stacking via `clear`; thumbnail right-anchoring |
| 037 | [CSS Value Functions: calc() / min() / max() / clamp()](037-calc.md) | Math expression evaluator + var()-inside-calc; search icon sized correctly |
| 038 | [Pseudo-Class Expansion](038-pseudo-expansion.md) | 14 pseudos: structural (10) + logical `:not`/`:is`/`:where` (3) + `:lang` (1); ~840 rules unblocked |
| 039 | [Box Model Completion](039-box-model.md) | `height` + `min/max` clamps + `box-sizing` + `overflow: hidden`; closes ADR-037 height gap |
| 040 | [Border Shorthand Expansion](040-border-shorthand.md) | `border: <width> <style> <color>` + per-side forms; parse-only, first-viewport borders appear |
| 041 | [Pseudo-elements (::before / ::after)](041-pseudo-elements.md) | Parse + cascade (partial; visible synthesis deferred to ADR-042) |
| 042 | [Position (relative / absolute)](042-absolute-positioning.md) | `relative` (containing block) + `absolute` (out-of-flow) + pseudo synthesis; title underline appears (PNG-verified) |
| 043 | [Box Shadow](043-box-shadow.md) | Non-inset, non-blurred shadow (CSS-spec clipped); toolbar line appears (PNG-verified) |
| 044 | [Border Radius](044-border-radius.md) | `border-radius` + per-corner longhands; rounded corners (visual subtle at 2px, behaviorally complete) |
| 045 | [Inline Backgrounds](045-inline-backgrounds.md) | `background-image` + size/position/repeat on inline elements; external link icons appear (scroll-PNG verified) |
| 046 | [Logging Infrastructure](046-logging.md) | Runtime-gated `HB_LOG` with category + level; env vars `HONEYBEE_LOG` / `_LEVEL` / `_FILE` |
| 047 | [CSS Mask Image](047-mask-image.md) | `mask-image` parse + paint (cairo_mask); zero first-viewport delta (all targets in hidden ancestors) |
| 048 | [Flex Wrap (Multi-line)](048-flex-wrap.md) | `flex-wrap: wrap` (row) multi-line algorithm; synthetic-DOM verified; zero first-viewport delta |
| 049 | [Text Align & Spacing (Deferred)](049-text-align.md) | Measured: `text-align` 26 rules (justify is print-only), `letter-spacing`/`word-spacing` 0 uses. No code; decision recorded |
| 050 | [@supports Conditional Rules](050-supports.md) | `@supports` parse + feature-query evaluator against capability table; unblocks 73 Wikipedia blocks, `.vector-pinned-container` display |

**Note:** ADR-005 exists only on the archived `feat/html-renderer` branch. It documented the GTK-based renderer that ADR-006 reverted.

## Reading order

If you are new: read **001 -> 006 -> 007 -> 008 through 050 in order**. That is the entire arc from "vendor a parser" to "render a page with CSS."

Skip 003 and 004 on a first pass. They record decisions that later ADRs superseded. Their value is historical context, not current guidance.
