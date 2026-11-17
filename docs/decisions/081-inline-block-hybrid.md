# ADR-081: Inline-block hybrid layout

**Status:** Initial hybrid implementation; production verification required  
**Date:** 2026-11-17  
**Related:** ADR-015, ADR-016, ADR-029, ADR-080

## Context
The layout engine needs to represent an element that participates in inline flow while containing its own block formatting context. CSS `display: inline-block` is externally atomic in inline flow and internally a block container.

## Decision
Represent inline-block as an atomic inline fragment backed by a recursively laid-out `LayoutBox`. Its border-box dimensions plus margins contribute to line breaking; its subtree is painted at the final fragment position.

## Implementation
- Add `Display::InlineBlock` and map it in `display_of()`.
- Route direct inline-block children through the parent inline run.
- Detect nested inline-block descendants during inline flattening.
- Lay out each internal subtree recursively and carry it through `StyledWord` to `LineFragment`.
- Use border-box dimensions plus margins when calculating atomic inline contribution.
- Prevent text fragment merging across atomic boxes.
- Relocate all descendant coordinates, including nested atomic descendants, into document coordinates.
- Reuse `paint_box()` for backgrounds, borders, clipping, images, transforms, and descendants.

## Automated verification
Four layout tests and one Cairo paint test cover display mapping, inline-context participation, subtree propagation, margin-inclusive fragment width, ordinary inline isolation, and actual nested-box painting. The Ubuntu CMake/Ninja + CTest workflow is the build gate; this reconstruction has not yet passed it.

## Production observations
The previous development session reported an improved Wikipedia header, intact references, and initial layout time around 2.9 seconds. Category layout remained unresolved. These are handoff observations, not re-verified results from this branch.

## Deferred limitations
- Auto-width shrink-to-fit: auto-width inline-block currently receives the available content width.
- Baseline and `vertical-align`: initial placement uses the line top.
- Mixed inline borders/backgrounds/padding require further production verification.
- Bidi, line-height details, replaced elements, and margin-collapse edge cases are not fully covered.

## References
- CSS 2.1 §9.2.2.1 — inline-level and inline-block elements.
- ADR-016 — inline formatting context.
- ADR-029 — inline-flex.
