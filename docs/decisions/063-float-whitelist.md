# ADR-063: Whitelist `float` and `clear` (unlock ADR-030/036 layout)

**Status:** Accepted
**Date:** 2026-11-02
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-030 (float layout), ADR-036 (clear handling), ADR-011 (block layout)

## Context

`layout.cpp:1205-1390` contains ~180 lines of **complete
float layout implementation** from ADR-030 (float placement at
content edge, float_left_w/right_w/bottom extent tracking,
narrowing of following block children to avoid overlap) plus
ADR-036 (clear: left/right/both jumps below prior floats).

The implementation reads `child_style->get("float")` (line
1234) and `child_style->get("clear")` (line 1284).

**Neither `"float"` nor `"clear"` is in `supported_properties()`.**
Both properties are dropped during CSS parse, so `get("float")`
always returns nullopt, so the entire 180-line implementation is
**dead code that never executes** on any real page.

### Wikipedia usage

| Property | Rules in site.styles |
|---|---|
| `float` | **56** |
| `clear` | **34** |

Largest float users on the HTML article:
- `.infobox { float: right; clear: right; width: 22em }`
  (sidebar info box, ~1 per article)
- `figure[typeof~='mw:File/Thumb'] { float: right; clear: right }`
  (thumbnail images with captions)
- `.mw-logo-icon { float: left }` (Wikipedia logo)
- `#footer-icons { float: right }` (footer link icons)
- `.vector-page-titlebar .mw-portlet-lang { float: right }`
  (language selector)

## Decision

Add `"float"` and `"clear"` to `supported_properties()` in
`stylesheet.cpp`. Two strings added to the whitelist set.

The layout side already works. This is a whitelist gap, not a
feature gap.

## Design decisions

### Why this is 2 lines, not 200

The layout implementation from ADR-030 and ADR-036 is complete:
- Float placement at content edge
- `float_left_w` / `float_right_w` / `float_bottom` accumulation
- Following block children narrowed to avoid overlap
- `clear: left/right/both` jumps below prior same-direction
  floats

Everything except the parser whitelist is in place.

### Why this is still an ADR (not a "quick fix")

**Zero float tests exist.** The 180 lines were written against
synthetic fixtures (if tested at all) and have **never been
exercised on real Wikipedia content** with its deeply nested
DOM, multiple floats on the same line, float + clear interleaved
with block children, and infoboxes inside article bodies inside
page containers.

The screenshot verification is the **first real-world test of
ADR-030 + ADR-036 together**.

### What this ADR will reveal (three honest outcomes)

**A. Visible improvement** (expected):
Infoboxes appear right-aligned, thumbnails float right with
captions beside text, logo floats left with tagline beside it,
footer icons align right.

**B. No visible change**:
Float layout has a bug preventing it from firing on Wikipedia's
content, or the elements are ancestor-culled. Would prompt a
diagnostic-only follow-up.

**C. Regression** (unlikely but real):
Float layout has bugs only visible on real content. Revert path:
`git revert` returns to current state immediately.

### The honest approximation caveat

**ADR-030 does NOT do text reflow around floats.** The comment
at layout.cpp:1209-1214 says so explicitly:

> Full per-line text-wrap is deferred to ADR-032; this is a
> block-narrowing approximation that covers the infobox case.

Meaning: text after a right-floating infobox will start below
the float's vertical extent, not beside it. **This is
"close but not perfect"** — matching Firefox's layout where a
float is present but wrapping behavior differs.

## Consequences

**Better (if outcome A):**
- Infoboxes, thumbnails, logos, footer all render in
  approximately-correct positions
- Largest single visual delta since ADR-058 (magnifier chain)
- ~90 CSS rules unblocked at parser level
- First real-world validation of ADR-030/036

**Accepted limitations:**
- **No text reflow** — approximation, not full text-wrap
- **Deep nesting may expose bugs** — real Wikipedia is more
  complex than any synthetic fixture

**Unchanged:**
- ADR-005 through ADR-062 behavior
- Elements without float or clear
- Paint, fetch, DOM

## Implementation plan

One commit:
- `stylesheet.cpp`: add `"float"` and `"clear"` to `supported_properties()`
- Build + run tests (687 / 2627 must stay green — no existing
  test uses float)
- Screenshot Wikipedia HTML article before/after; user reports
  visible delta

If outcome A: add fixture + 2–3 float tests + docs close.
If outcome B: docs close as decision record.
If outcome C: revert + separate diagnostic ADR.

## Verification

**Screenshot comparison (Wikipedia HTML article):**
- **User reports** what visibly changed (I cannot see the
  screen — this is user-reported, honestly labelled)
- **Verification category**: production-visual delta,
  user-reported

**Test count**: unchanged at 687 / 2627 (whitelist add cannot
break existing behavior).

## Acceptance criteria

ADR-063 is complete when:

- [ ] `"float"` and `"clear"` in `supported_properties()`
- [ ] 687 / 2627 tests still pass
- [ ] Wikipedia screenshot before/after captured by user
- [ ] User reports visible delta (or explicit "no delta")
- [ ] Outcome documented honestly in ADR-063 Implementation section
- [ ] README row + CHANGELOG
- [ ] ADR-063 status → Implemented
