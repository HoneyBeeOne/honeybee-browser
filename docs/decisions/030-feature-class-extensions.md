# ADR-030: Feature-Class Extensions + Skip-Link Hide

**Status:** Accepted
**Date:** 2026-10-26
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-025 (external CSS), ADR-028 (grid), ADR-029 (flex + first feature-class flip)

## Context

ADR-029 shipped the first viewport feature-class flip
(`main-menu-pinned-disabled` -> `-enabled`) and unlocked the
largest visual delta of the CSS-layout arc: header collapsed
from 727px to 36px, `.mw-page-container-inner` moved from y=750
to y=59, article title at y=107.

Live visual verification of Wikipedia (screenshot session
2026-10-26) confirmed three remaining bugs in the first viewport,
all with the same root cause -- server-shipped `vector-feature-*`
flags that JS flips at desktop load:

1. **Tools panel (687px)** -- `.vector-page-toolbar` renders its
   "Tools" dropdown (Tools / General / What links here / Related
   changes / Permanent link / Page information / Cite this page /
   Shortcuts / Download as PDF / In other projects / Abstract
   Wikipedia / Wikimedia Commons / MediaWiki / Wikibooks /
   Wikiversity / Wikidata item) as inline blocks in the article
   column. Real Wikipedia with JS collapses this to a compact
   "Tools" word that opens on click.

2. **Page actions floating mid-page** ("Read / View source /
   View history") -- should sit inline with the Article / Talk
   tabs at y~=147, but rendered at y~=4873 (below the Tools panel)
   because the toolbar's 687px of inline content has pushed them
   down.

3. **User links floating full-width** ("Donate / Create account /
   Log in") -- should sit in the compact header bar right-aligned,
   but rendered full-width at y~=107 because the header's
   `.vector-user-menu` children render inline without the
   dropdowns collapsing.

4. **"Jump to content" skip link visible** at top-left -- should
   be off-screen until Tab-focused. Real Wikipedia uses
   `.mw-jump-link:not(:focus) { position: absolute; clip:
   rect(1px,1px,1px,1px); ... }` plus `#mw-navigation, .noprint,
   .mw-jump-link, ... { display: none }` in print. Without
   `:not()` and without `position: absolute` / `clip`, neither
   rule hides it.

### Measurement

From the diagnostic on Wikipedia's CSS (2026-10-26):

| Signal | Value |
|---|---|
| Rules with `position:` | 47 |
| `position: relative` | 22 |
| `position: absolute` | 16 (+3 `!important`) |
| `position: fixed` | 3 |
| `position: sticky` | 2 |
| Rules with `:checked` | 14 |
| `:not()` support in our selector matcher | **none** |

**Key finding: `:checked` is used 14 times but does not directly
drive any of the four visible bugs.** The direct driver is the
`vector-feature-page-tools-pinned-*` class state on `<html>` --
same JS-set pattern as ADR-029's `main-menu-pinned-*`.

### The scope reframe

The original plan for ADR-030 was **position + :checked**. The
measurement reframes it: ~80% of the visible fix is a **second
feature-class flip** (five-line change to ADR-029's existing
helper), and the remaining 20% is the skip link, which needs
either `:not()` + `position: absolute` + `clip` (a substantial
new subsystem) or a small `display: none !important` workaround.

Structural `position` + `:checked` + `:not` + `clip` work is
genuinely needed for **future** features (dropdowns, infoboxes,
thumbnails, sticky headers, hover menus) but not for the four
visible bugs in this viewport.

**Decision: split into two ADRs.** ADR-030 (this one) is the
small, measurement-driven follow-up that fixes the visible bugs.
ADR-031 (deferred, planned) is the structural work.

### Feature flags on the html element today

From the raw HTML:

```text
vector-feature-language-in-header-enabled          (correct)
vector-feature-language-in-main-menu-disabled      (correct)
vector-feature-language-in-main-page-header-disabled
vector-feature-page-tools-pinned-disabled          <-- flip to -enabled
vector-feature-toc-pinned-clientpref-1
vector-feature-main-menu-pinned-disabled           (already flipped by ADR-029)
vector-feature-limited-width-clientpref-1
vector-feature-limited-width-content-enabled
vector-feature-custom-font-size-clientpref-1
vector-feature-appearance-pinned-clientpref-1
```

Only `page-tools-pinned` needs flipping in this ADR. `main-menu-pinned`
is already flipped by ADR-029's Commit 3. `clientpref-*` flags
are user preferences and are intentionally left alone.

### The Tools-panel hide rule

```css
.vector-feature-page-tools-pinned-enabled
    .vector-page-toolbar-container .vector-page-tools-landmark,
.vector-feature-page-tools-pinned-disabled
    .vector-column-end .vector-page-tools-landmark {
    display: none;
}
```

Currently `<html>` has `-disabled` and there is no
`.vector-column-end` in the DOM (confirmed by ADR-029's
layout-tree dump: `vector-column-end NOT FOUND`). Neither branch
matches.

After flipping to `-enabled`: branch 1 matches
(`.vector-page-toolbar-container .vector-page-tools-landmark`),
the Tools panel hides, and the toolbar shrinks by 687px.

## Decision

Two changes, both following the ADR-029 pattern.

### 1. Extend apply_viewport_feature_classes with a flip table

Instead of one hardcoded flip, a small constexpr table:

```cpp
struct FeatureFlip {
    const char* disabled;
    const char* enabled;
};
static constexpr FeatureFlip kDesktopFlips[] = {
    {"vector-feature-main-menu-pinned-disabled",
     "vector-feature-main-menu-pinned-enabled"},
    {"vector-feature-page-tools-pinned-disabled",
     "vector-feature-page-tools-pinned-enabled"},
};

void apply_viewport_feature_classes(
    renderer::dom::Document& doc, float viewport_width) {
    auto* html_el = doc.find_element("html");
    if (!html_el) return;
    std::string cls = html_el->get_attribute("class");
    for (const auto& f : kDesktopFlips) {
        if (viewport_width >= 1120.0f) {
            flip_class(cls, f.disabled, f.enabled);
        } else {
            flip_class(cls, f.enabled, f.disabled);
        }
    }
    html_el->set_attribute("class", cls);
}
```

Same threshold (1120). Same idempotent round-trip semantics.
Easily extensible: future ADRs add a table entry.

### 2. Hide the skip link at all viewports

Inject `.mw-jump-link { display: none !important }` into
`viewport_css` (the string ADR-029's `#p-lang-btn` workaround
uses). `!important` beats every non-important rule in the cascade
regardless of specificity, including the
`.mw-jump-link:not(:focus)` rule that would otherwise show it.

**Why `!important` rather than a proper hide:** the standard
visually-hidden pattern requires `position: absolute; clip:
rect(1px,1px,1px,1px); overflow: hidden; width: 1px; height:
1px;` -- that's four separate unimplemented CSS features. And
even if all four were implemented, the skip link is only useful
with `:focus` support, which we also don't have. Hiding it with
`!important` is honest: we don't support the feature that would
make it useful.

**When ADR-031 lands (`position: absolute` + `:not` + `clip`),
this workaround can be removed** -- the proper visually-hidden
pattern will take over, and `:focus` support (future ADR) will
make the link Tab-reachable again.

### What stays as-is

- ADR-029's `#p-lang-btn { display: none }` workaround (still
  needed -- the language dropdown is `:checked`-controlled, ADR-031)
- All position declarations (47 rules) remain unhonored. Elements
  with `position: absolute` continue to participate in block flow.
  This is unchanged behavior; ADR-030 does not regress it.

## Design decisions

### Why a table rather than a second hardcoded flip

ADR-029 added one flip inline. A second means we've turned a
one-off into a pattern. The table is ~4 lines longer than two
if-blocks, but it's:
- **Trivially extensible** -- future ADRs add one entry
- **Self-documenting** -- one place to see all flips
- **Safer** -- the flip/undo symmetry is enforced by the loop

### Why page-tools-pinned but not language-in-main-menu-pinned

The `<html>` class list shows:

- `vector-feature-language-in-main-menu-disabled` -- **correct**.
  At desktop, language lives in the header (`language-in-header-enabled`),
  not in the main menu. Flipping would duplicate language links.
- `vector-feature-page-tools-pinned-disabled` -- **wrong at desktop**.
  Real Wikipedia flips to `-enabled` at load.

Only the second is our concern.

### Why clientpref-* flags are left alone

`toc-pinned-clientpref-1`, `limited-width-clientpref-1`,
`custom-font-size-clientpref-1`, `appearance-pinned-clientpref-1`
are user-preferred values. A real browser reads the user's
preference from `localStorage` at load. We have no `localStorage`
and no UI to change these. Flipping them would override user
choice with a hardcoded value; leaving them honors the
server-side default (which is what JS-off Wikipedia shows).

### Why hide the skip link rather than implement :not() + clip

The `:not()` selector is used elsewhere on Wikipedia (`:not(:focus)`,
`:not(.emptyPortlet)`, `:not(:has(...))` in some rules). Implementing
it properly is worth its own ADR -- it's a general selector feature,
not a one-off. Hiding one skip link with `!important` is ~2 lines;
implementing `:not()` correctly is ~40-60 lines plus tests. The
honest split: small workaround now, general feature when it's
needed for something else.

Similarly `clip: rect(1px,1px,1px,1px)` -- a rect-based clip
mechanism that requires layout support. Deferred to ADR-031.

### What the visible delta should be

After flip + skip-link hide, expected (viewport 1774):

| Element | Before ADR-030 | Expected after |
|---|---|---|
| `.vector-page-toolbar` height | 687 | ~40-60 |
| `.vector-page-toolbar` y | 147 | 147 (unchanged) |
| "Read/View source/View history" y | ~4873 | ~180-200 |
| `.vector-body` (article intro) y | 834 | ~250-350 |
| "Jump to content" | visible top-left | hidden |
| "Tools" dropdown (inline) | 687px of stacked list | hidden |

The article **intro** finally appears near the top of the first
viewport (roughly y~=300), one small scroll from the fold at 900.
Not quite "above the fold" -- that requires the language dropdown
(140 items, ADR-031 territory) and possibly other dropdowns to
collapse too. But the largest remaining obstruction (687px) is
gone.

## Deferred (ADR-031 candidate)

- **`:checked` pseudo-class** (14 uses in Wikipedia's CSS) --
  general selector-state support for stateful dropdowns. Required
  for `#p-lang-btn` proper fix, user menu, page tools dropdown.
- **`position: absolute`** (16 uses) -- out-of-flow positioning.
  Required for dropdown panels, infoboxes, thumbnails.
- **`position: fixed`** (3 uses) -- viewport-anchored positioning.
- **`position: relative`** (22 uses) -- positioning context for
  absolute children. May not need layout changes (relative
  positioning is a paint-time shift in CSS), but we still need
  to compute the parent chain for `absolute` children to anchor
  to.
- **`:not()` selector** -- general negation for compound
  selectors. Needed for `.mw-jump-link:not(:focus)`,
  `.mw-portlet-views:not(.emptyPortlet)`, and others.
- **`clip` / `clip-path`** -- visual clipping. Needed for the
  visually-hidden pattern.
- **`position: sticky`** (2 uses) -- sticky-pinned sidebar
  containers. Complex (scroll-dependent). Defer to its own ADR.
- **`z-index`** -- paint order for positioned elements. Required
  for correct dropdown-on-top-of-content rendering.

## Consequences

**Better:**

- **Tools panel collapses** (687px reclaimed). Article body moves
  from y=834 to ~y~=250-350. Largest remaining single obstruction
  in the first viewport.
- **Page actions move up** to inline with Article/Talk tabs
  (~y~=180 instead of y~=4873).
- **User links visible at correct position** in the compact
  header bar.
- **Skip link hidden** -- top-left of first viewport becomes
  visually clean.
- **Refactor of ADR-029's feature-class flip into a table** -- the
  pattern becomes extensible without further code duplication.

**Accepted limitations:**

- Article intro at y~=300, still one small scroll below the fold
  at 900px viewport. Full "intro at top" requires ADR-031
  (`:checked` for the language dropdown).
- Skip link hidden even for keyboard users. Acceptable because
  we don't support `:focus` yet.
- All 47 `position:` rules still unhonored. No regression --
  unchanged behavior from ADR-029.

**Unchanged:**

- ADR-029's flex algorithm, feature-class flip, and
  `#p-lang-btn` workaround. The flip table **extends** the
  existing helper without changing its semantics for the existing
  main-menu flip.
- All prior ADRs (018-028).

## Implementation plan

Two commits on `feat/feature-class-extensions`:

1. **Extend apply_viewport_feature_classes to a flip table
   + hide skip link** -- add `page-tools-pinned` to the table;
   append `.mw-jump-link { display: none !important }` to
   `viewport_css`. Add 3-4 tests (page-tools flip at desktop,
   no flip at mobile, skip-link hidden at desktop, skip-link
   not touched at mobile if that's the choice). Verify visual
   delta on Wikipedia.
2. **Docs** -- ADR-030 status -> Implemented, Post-Implementation
   note in ADR-029 updated with new y-coordinates, CHANGELOG,
   README row.

## Verification

**Unit tests** (in `tests/test_layout.cpp`, extending the existing
`[feature-classes]` tag):

- page-tools-pinned-disabled -> -enabled at >=1120
- main-menu-pinned-disabled -> -enabled at >=1120 (regression)
- both flipped back at <1120 (round-trip)
- `.mw-jump-link` display == none at >=1120
- `.mw-jump-link` display == none at <1120

**Visual verification on Wikipedia** (viewport 1774):

| Check | Before ADR-030 | After ADR-030 |
|---|---|---|
| `.vector-page-toolbar` height | 687 | ~40-60 |
| "Read/View source/View history" y | ~4873 | ~180-200 |
| `.vector-body` (article intro) y | 834 | ~250-350 |
| "Jump to content" skip link | visible | hidden |
| "Tools" dropdown stacked list | visible | hidden |
| Header height | 36 | 36 (unchanged) |
| `.mw-page-container-inner` y | 59 | 59 (unchanged) |
| Article title visible | yes (y=107) | yes (y=107) |

**Second-viewport visual**: article body paragraphs continue to
render correctly (unchanged from ADR-029).

## Acceptance criteria

ADR-030 is complete when:

- [ ] `page-tools-pinned` added to the feature-flip table
- [ ] `.mw-jump-link { display: none !important }` injected at desktop
- [ ] Round-trip resize preserves correct flip state for both flags
- [ ] 3-4 unit tests added and passing
- [ ] Measured visible delta on Wikipedia (Commit 1 message)
- [ ] ADR-029's Post-Implementation Note updated with new y-coordinates
- [ ] `main` is green: 394 + N cases, all pass, zero warnings
- [ ] ADR-030 status -> Implemented in Commit 2

**Explicit claim**: Tools panel collapses from 687px to ~40-60px.
Article body intro moves from y=834 to y~=250-350. Skip link hidden.
The article **title + page chrome + page actions** all visible in
the first viewport. The **intro** is one small scroll from the
fold (previously it was many scrolled-pages down because the Tools
panel and language list dominated).

**Honest caveat**: "Article above the fold" requires ADR-031
(`:checked` for the language dropdown, which is currently hidden
by the ADR-029 workaround but would naturally collapse to a small
indicator with proper `:checked` support). ADR-030 is the largest
remaining feature-class flip; it clears 80% of the visible gap.
