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

**Note:** ADR-005 exists only on the archived `feat/html-renderer` branch. It documented the GTK-based renderer that ADR-006 reverted.

## Reading order

If you are new: read **001 -> 006 -> 007 -> 008 through 013 in order**. That is the entire arc from "vendor a parser" to "render a page with CSS."

Skip 003 and 004 on a first pass. They record decisions that later ADRs superseded. Their value is historical context, not current guidance.
