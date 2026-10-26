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

**Note:** ADR-005 exists only on the archived `feat/html-renderer` branch. It documented the GTK-based renderer that ADR-006 reverted.

## Reading order

If you are new: read **001 -> 006 -> 007 -> 008 through 030 in order**. That is the entire arc from "vendor a parser" to "render a page with CSS."

Skip 003 and 004 on a first pass. They record decisions that later ADRs superseded. Their value is historical context, not current guidance.
