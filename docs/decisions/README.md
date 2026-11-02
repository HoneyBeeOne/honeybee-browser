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
| 050 | [@supports Conditional Rules](050-supports.md) | `@supports` parse + feature-query evaluator; 73 blocks unblocked; layout-verified `.vector-pinned-container` display:block |
| 051 | [Viewport Units (vh/vw/vmin/vmax)](051-viewport-units.md) | `vh`/`vw`/`vmin`/`vmax` in `parse_length` + `parse_operand`; viewport dims threaded; layout-tree verified, 0 production delta pending GTK height |
| 052 | [GTK Viewport Height Threading](052-gtk-viewport-height.md) | `m_scroll_window.get_height()` threaded through render/relayout; verified via log (`viewport=1024x678`); closes ADR-051 Deferred |
| 053 | [Honor viewport units in max-width / max-height clamps](053-max-clamps-viewport-units.md) | Removes ADR-037-era vw/vh skip guard; clamps verified via 3 new tests (100vw/100vh/calc(100vh-48px)); consumer blocked by ancestor culling |
| 054 | [CSS transform: rotate()](054-transform-rotate.md) | Four-layer implementation: `parse_angle` + whitelist + field + cairo rotate wrapper. Verified via permanent fixture (diamond + tilted rect render correctly). Zero production delta (Wikipedia uses translateY, deferred). Revealed shorthand `background:`/`border:` parser gap. |
| 055 | [CSS shorthand background: (color forms)](055-background-shorthand.md) | `try_expand_background` handles color / none / !important / var forms; url/gradient/multi-layer drop entirely. Fixture-verified (5 rows, 2026-11-01). Corrects ADR-054's imprecise "background: and border: fail" claim. |
| 056 | [CSS background: URL forms](056-background-url-forms.md) | 5 new `LayoutBox` fields mirroring mask; `paint_background_image` (Cairo pattern matrix, repeat); grammar parser for all 4 Wikipedia url() forms. **Production-verified 2026-11-01** (search icon + external link icons render on Wikipedia). 11/11 forms parse. |
| 057 | [`parent_content_height` threading + `%` height/max-height](057-parent-content-height.md) | Threaded through 8 call sites; `%` height/min/max resolve (auto parent still skips per CSS spec); `<html>` uses `viewport_h`. 6 new tests. Closes ADR-039/053 `%` deferrals. Enables ADR-058. |
| 058 | [`top: 50%` + `transform: translateY(-50%)`](058-top-translate-y.md) | `top: %` against containing block height; `translateY()` against own box height. Paint wrapper `did_transform`. **Production-verified 2026-11-02** (magnifier centered at y=18). Closes 5-ADR chain 054/055/056/057/058. Also ships perf split-timer. |
| 059 | [Selector index for cascade performance](059-selector-index.md) | `RuleIndex` + `anchor_for` + `candidates_for`. **5.6× cascade speedup measured across 3 runs** (1058ms → ~190ms, Release, Wikipedia). Cascade share drops 31% → 6-7%. Revealed layout variance (3×) — ADR-060 methodology lesson. |
| 060 | [Layout copy elimination](060-layout-copy-elimination.md) | `ComputedStyle::get_view()` (transparent hash, `string_view` return) + `fill_sides` inline lookup. **12.0% layout win, median-of-3 measured** (2678ms → 2357ms). Combined with ADR-059: **25.5% total Wikipedia perf win.** |
| 061 | [Layout perf diagnostic](061-perf-diagnostic.md) | **Decision record** (no code): typed `ComputedStyle` fields hypothesis disproven by re-profile. Post-ADR-060, the target lookup is 0.25% (not 25% as projected). Closes the perf stream — ADR-059 + 060 captured everything >5% individually. |
| 062 | [`linear-gradient()` background support](062-linear-gradient.md) | Parser + layout + paint for linear-gradient. **`rgb()`/`rgba()` added to `parse_color`** (uncovered prerequisite). **Synthetic-fixture-verified** (5 rows, 2026-11-02). Zero Wikipedia delta (target triple-blocked: pseudo-element + h=0 parent + sticky). Closes the last 1/11 `background:` form; 11/11 forms parse. |

**Note:** ADR-005 exists only on the archived `feat/html-renderer` branch. It documented the GTK-based renderer that ADR-006 reverted.

## Reading order

If you are new: read **001 -> 006 -> 007 -> 008 through 062 in order**. That is the entire arc from "vendor a parser" to "render a page with CSS."

Skip 003 and 004 on a first pass. They record decisions that later ADRs superseded. Their value is historical context, not current guidance.
