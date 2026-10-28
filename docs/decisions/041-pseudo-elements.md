# ADR-041: Pseudo-elements (`::before` / `::after`)

**Status:** Accepted
**Date:** 2026-10-28
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-016 (inline formatting), ADR-018 (link state), ADR-033 (position: absolute), ADR-038 (pseudo-classes), ADR-040 (border shorthand -- h1 prediction corrected here)

## Context

ADR-040 shipped border shorthand expansion. Its Implementation
section recorded a deferred prediction: `h1#firstHeading`'s
bottom underline would appear once `border: 1px solid X`
expanded. It did not appear.

**Diagnostic (2026-10-28) resolved the mystery.** h1's
ComputedStyle has `border-bottom-width: 1px`, `border-style:
solid`, `border-color: #a2a9b1` -- ADR-040's expansion works.
But a higher-specificity rule wins:

    h1 { border-bottom: 1px solid #a2a9b1 }              (0,0,1)
    .vector-page-titlebar .firstHeading { border: 0 }    (0,2,0)

The `border: 0` rule from `.vector-page-titlebar .firstHeading`
wins. **The actual underline visible on real Wikipedia is a
pseudo-element**, not an h1 border:

    .vector-page-titlebar::after {
        content: '';
        display: block;
        position: absolute;
        bottom: 0; left: 0; right: 0;
        height: 1px;
        background-color: var(--border-color-base, #a2a9b1);
    }

The pseudo-element architecture is entirely missing from our
engine. This ADR adds it.

### Measurement (Wikipedia CSS, 2026-10-28)

Pseudo-element usage in the Wikipedia HTML article:

| Category | Count | Example |
|---|---|---|
| `::after { content: '' }` (empty, decorative) | ~25 | Title underline, dropdown chevron, magnify icon |
| `::before { content: 'literal text' }` | ~12 | `[edit]` brackets, citation backlinks `↑`, figure icons |
| `::after { content: 'literal text' }` | ~8 | Closing brackets, separators ` | ` |
| `::before { content: attr(...) }` | ~4 | Syntax highlight line numbers |
| `::before { content: url(...) }` | ~6 | External link icons |

**45+ pseudo-element rules** in the target page. ~37 use only
`content: ''` or `content: 'literal text'` -- the scope of this
ADR. The remaining 8 need `attr()` or `url()` and are deferred.

### First-viewport impact (viewport 1774)

| Element | Pseudo | Visible effect |
|---|---|---|
| `.vector-page-titlebar::after` | `::after` | **Title "HTML" gains underline** |
| `.mw-editsection a::before` | `::before` | **`[edit]` bracket appears** |
| `.mw-editsection a::after` | `::after` | **`]` closing bracket appears** |
| `.mw-cite-backlink::before` | `::before` | **`↑` citation backlink** |

**4 genuinely visible first-viewport changes**, including the
title underline that ADR-040's misdiagnosis pointed at.

### What's NOT in scope (deferred)

**`content: attr(...)` / `counter(...)` / `url(...)`** --
required for syntax-highlight line numbers, external-link
icons. Deferrable: 8 rules, all small visual chrome.

**`::first-line`, `::first-letter`, `::selection`,
`::placeholder`, `::marker`** -- separate mechanism (breaks
inside an element's own content). Defer to a future ADR if
needed.

**Complex pseudo-class + pseudo-element combinations**
(`:hover::after`, `:focus::before`) -- state-aware pseudo
elements. Defer; our state tracking (ADR-018) doesn't yet
support hover-driven layout recalculation.

**`content` sub-properties** (`content: '' / ''` quoting,
`content: open-quote`) -- unusual; skip.

## Decision

Implement `::before` / `::after` pseudo-elements across three
stages, matching ADR-040's two-commit + docs rhythm:

### Stage 1 (Commit 1): Parse + Cascade

**`selectors.hpp`:**
- Add `enum class PseudoElement : std::uint8_t { None, Before, After };`
- Add `PseudoElement pseudo = PseudoElement::None;` to `CompoundSelector`
- Add two-argument match: `bool matches(const dom::Node& node,
  PseudoElement target_pseudo) const;` -- returns false if
  selector's pseudo doesn't match the target

**`selectors.cpp`:**
- In the pseudo-class parser branch: recognize `::before` and
  `::after` (double-colon forms) as pseudo-element markers,
  not pseudo-classes. Set `pseudo` on the containing
  `CompoundSelector`. The `::` alone is the modern syntax;
  `:before` (single-colon legacy) is also recognized
- Reject pseudo-elements not in the `{None, Before, After}`
  set (drop rule, matching existing pseudo-class behavior for
  unknown)

**`cascade.cpp`:**
- `cascade()` return type becomes:

      struct CascadeResult {
          std::map<const dom::Element*, ComputedStyle> element_styles;
          std::map<std::pair<const dom::Element*, PseudoElement>,
                   ComputedStyle> pseudo_styles;
      };

- Two-pass match per rule: first against `element_styles` (pseudo
  = None), then against `pseudo_styles` for each supported pseudo
- Callers update to use `.element_styles` (mechanical changes at
  ~5-10 sites)

**Commit 1 is behavior-neutral**: no selector with `::before` /
`::after` matches today (they're all dropped), so the pseudo
map is always empty after Commit 1. No visual change.

### Stage 2 (Commit 2): Layout + Paint

**`layout.cpp`:**

In `layout_element`'s child loop, before processing DOM children:
1. Check `pseudo_styles` for `(el, PseudoElement::Before)` --
   if present, synthesize a `LayoutBox` for it and append to
   `box.children` **before** the first DOM child
2. After processing DOM children, do the same for
   `(el, PseudoElement::After)` -- append **after** last DOM
   child

**Synthesized pseudo-box layout:**
- Parse `content` property:
  - `content: ''` or `content: ""` (empty string) → zero-width
    text fragment, but the box still participates in layout
    (needed for `position: absolute` + `background-color` case
    like the title underline)
  - `content: 'text'` (single-quoted literal) → a normal
    inline text run inside a block box
  - anything else (`attr()`, `counter()`, `url()`) → **no box**
    (deferred, no visual)
- Apply pseudo-element's own `display`, `position`, `width`,
  `height`, `background-color`, `border`, `margin`, `padding`
  from its `ComputedStyle`. This is identical to how normal
  elements are laid out -- the pseudo-box just uses the
  pseudo-element's `ComputedStyle` as its style context.
- `position: absolute` handling comes free from ADR-033. The
  title underline case positions itself at `bottom: 0; left:
  0; right: 0` inside `.vector-page-titlebar`.

**Paint:** zero code changes. The pseudo-box is a `LayoutBox`
with a `background_color` and/or `border` -- paint already
handles these. The title underline draws as a 1px background
rectangle. `[edit]` brackets draw as text fragments.

**Commit 2 is the visible-delta commit.**

### Stage 3 (Commit 3): Docs

- ADR-041 status → Implemented
- **ADR-040 Post-ADR-041 correction**: append to ADR-040's
  Implementation section: "h1 underline prediction was wrong.
  The visible underline is `.vector-page-titlebar::after`, not
  an h1 border. h1's `border-bottom` is overridden by
  `.vector-page-titlebar .firstHeading { border: 0 }`. Post-
  ADR-041, the underline appears via pseudo-elements."
- README row, CHANGELOG entry

## Design decisions

### Why `::before` / `::after` only (not `::first-line` etc.)

`::before` and `::after` are **siblings of the element's
children** -- they're laid out like child nodes. Our layout
engine can synthesize them without architectural change.

`::first-line` and `::first-letter` are **sub-ranges of an
element's own text content**. Synthesizing them requires
splitting text runs mid-flow, and they don't correspond to any
DOM node. That's an inline-formatting change, not a box-tree
change. Different ADR if needed.

`::marker` (list bullet styling) is its own mechanism.
`::selection` needs selection state. `::placeholder` needs
form input semantics. None are in scope.

### Why `content: ''` still produces a box

The title underline is `content: ''` -- an empty string. In
literal text terms, it renders nothing. But it has `display:
block; position: absolute; height: 1px; background-color:
#a2a9b1`. Those are the visible properties. Skipping the box
would eliminate the underline.

So the rule is: **if `content` is an empty string literal, still
synthesize the box; its visual comes from `background-color` /
`border` / `width` / `height`, not from text.** This is what
real browsers do too.

Same applies to pseudo elements with `content: 'text'` -- the
box wraps the text and background/border/padding apply around
it.

### Why pseudo-selectors are rejected today and accepted tomorrow

Currently `::before` fails `failed_ = true; // unknown
pseudo-class` in `selectors.cpp`. The whole selector is dropped,
so the `content:` declaration and any associated styles are
never seen. This is the correct conservative behavior for an
engine that can't render pseudo-elements.

ADR-041 changes it: `::before` and `::after` become recognized
pseudo-elements, so their rules enter the cascade. Rules with
other pseudo-elements (`::first-line`, `::selection`, etc.)
continue to drop.

### Why a separate `pseudo_styles` map (not a combined key map)

Two structures:

    std::map<const Element*, ComputedStyle> element_styles;
    std::map<std::pair<const Element*, PseudoElement>,
             ComputedStyle> pseudo_styles;

**Alternative**: single map keyed by `(element, pseudo)`. Rejected
because 99% of consumers only want `element_styles` (the normal
case). Keeping it as a separate map makes the common path
identical to today's code. Callers who need pseudo-styles opt in
explicitly. Also makes the cascade's two match passes symmetric.

The return type change (`CascadeResult` struct instead of
`std::map`) is mechanical across ~5-10 caller sites.

### Why `content: attr()` / `counter()` / `url()` deferred

`attr(data-line)` requires resolving an HTML attribute value at
paint time. `counter()` requires a counter system (CSS counters
are their own subsystem). `url(...)` requires background image
support (not yet shipped).

8 rules use these. All are small visual chrome (syntax
highlight line numbers, external link icons). **The 4 first-
viewport wins of ADR-041 don't need any of them.** Ship the
bigger batch now, defer the smaller.

## Deferred

- **`content: attr(...)` / `counter(...)` / `url(...)`** --
  8 rules, own ADR if/when needed
- **`::first-line`, `::first-letter`, `::selection`, `::marker`,
  `::placeholder`** -- different mechanism, own ADR if needed
- **State-aware pseudo-elements** (`:hover::after`,
  `:focus::before`) -- requires layout recalc on state change;
  our state (ADR-018) is paint-only
- **`content` with multiple quotes** (`content: '' ''`) --
  0 uses, defer

## Consequences

**Better:**
- **Title "HTML" gains underline** -- the visual ADR-040's
  prediction was targeting, now correctly achieved
- **`[edit]` brackets appear** at each section heading
- **Citation backlinks (`↑`)** appear after references
- **~37 rules enter the cascade** that previously dropped
- **ADR-040's prediction is corrected** -- the record becomes
  accurate about what draws the underline
- Pseudo-elements infrastructure ready for future decorators

**Accepted limitations:**
- 8 rules with `attr()`/`counter()`/`url()` still dropped
- `::first-line` etc. still unsupported
- State-aware pseudo-elements still paint-only

**Unchanged:**
- ADR-018 through ADR-040 -- all features still work
- Layout engine's block / inline / grid / flex dispatch
- Paint's box and inline-text rendering

## Implementation plan

Three commits:

1. **Parse + Cascade** (structural, behavior-neutral):
   - `PseudoElement` enum + `CompoundSelector::pseudo` field
   - Parser recognizes `::before` / `::after` / `:before` /
     `:after`
   - `Selector::matches(node, PseudoElement)` two-arg overload
   - `CascadeResult` return type + `pseudo_styles` map
   - ~6-8 parse tests + ~5-6 cascade tests
   - Every existing caller updated (mechanical)
   - No visual change

2. **Layout + Paint** (visible, the payoff):
   - Pseudo-box synthesis in `layout_element`
   - `content: ''` and `content: 'text'` handling
   - Absolute-position inheritance via ADR-033
   - ~8-10 layout tests (2-column, absolute-positioned empty
     pseudo, text-content pseudo)
   - **Wikipedia PNG verification required before merge** --
     title underline must appear, `[edit]` brackets must appear

3. **Docs**:
   - ADR-041 status → Implemented
   - **ADR-040 Post-ADR-041 correction** about the h1
     prediction
   - README + CHANGELOG

## Verification

**Unit tests:**

Parse tests:
- `p::before` → Selector with pseudo=Before
- `p::after` → Selector with pseudo=After
- `p:before` (legacy single-colon) → pseudo=Before
- `p::first-line` → rejected (dropped)
- `p::marker` → rejected
- `.x.y::after` → compound with class + pseudo

Cascade tests:
- `p::after { content: ''; background: red }` → pseudo_styles
  entry for `(p_element, After)` with background red
- `p::before { content: 'x' }` → pseudo_styles entry
- `p::before` and `p::after` both present → both entries exist
- Element rules still go to `element_styles` (regression)

Layout tests:
- `<p>` with `p::after { content: ''; display: block;
  height: 5px; background: red }` → p's LayoutBox has an extra
  child, 5px tall, red background
- `<p>` with `p::before { content: 'X' }` → p has a child text
  fragment with "X"
- `p::before` at absolute position → box positioned per
  position: absolute rules (ADR-033)
- No pseudo-element rules → layout identical (regression)

**Visual verification (Commit 2, PNG-verified before merge):**

| Check | Before | After |
|---|---|---|
| Title "HTML" underline | Missing | **1px gray line under title** |
| Edit brackets next to headings | Missing | **`[edit]` text at right** |
| Citation backlinks | Missing | **`↑` after refs** |
| No layout shift | -- | **article text same position** |
| Full suite green | -- | -- |

## Acceptance criteria

ADR-041 is complete when:

- [ ] `PseudoElement` enum added
- [ ] Parser recognizes `::before` / `::after` (both colon forms)
- [ ] `Selector::matches(node, PseudoElement)` two-arg overload
- [ ] `CascadeResult` struct with `element_styles` and `pseudo_styles`
- [ ] All existing callers updated (mechanical)
- [ ] Pseudo-box synthesis in `layout_element`
- [ ] `content: ''` and `content: 'text'` produce boxes
- [ ] `content: attr(...)` etc. produce no box (deferred)
- [ ] 14+ new unit tests
- [ ] Wikipedia PNG verification: title underline + edit
      brackets + citation backlinks appear
- [ ] No unintended layout shift (article text at same y)
- [ ] ADR-040 Post-ADR-041 correction recorded
- [ ] `main` green: 504 + N cases, all pass, zero warnings
- [ ] ADR-041 status → Implemented in docs commit

**Explicit claim**: title underline, edit brackets, and citation
backlinks appear in the first viewport. This closes ADR-040's
h1 misdiagnosis with the correct mechanism.
