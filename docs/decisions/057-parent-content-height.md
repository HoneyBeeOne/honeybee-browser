# ADR-057: `parent_content_height` threading + `%` height/max-height

**Status:** Implemented
**Date:** 2026-11-02
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-039 (explicit height), ADR-051 (viewport_h threading), ADR-052 (viewport_h production), ADR-053 (max-height clamps), ADR-054 (magnifier diagnostic), ADR-056 (background URLs)

## Context

`LengthContext` (style_values.hpp:19) has `parent_content_width`
for `%` width resolution since ADR-011. It does **not** have
`parent_content_height`. Consequences measured 2026-11-02:

### What's blocked by the missing field

| Deferral | Source | Since |
|---|---|---|
| `height: 100%` skipped | ADR-039 | layout.cpp:1835 `find('%') == npos` guard |
| `min-height: N%` skipped | ADR-039 | layout.cpp:1848 same guard |
| `max-height: N%` skipped | ADR-039, ADR-053 | layout.cpp:1857 same guard |
| `top: 50%` doesn't resolve | ADR-054 diagnostic | Needs parent height |
| `translateY(-50%)` centering | ADR-054 diagnostic | Needs parent height via `top: 50%` |

### What exists that can be mirrored

`parent_content_width` is threaded cleanly:
- Field in `LengthContext` (line 23)
- Param in `layout_element` (position 6, line 291)
- Set on ctx at line 372: `ctx.parent_content_width = parent_content_width;`
- 8 recursive call sites pass it explicitly (lines 1064, 1109, 1193, 1278, 1397, 1563, 1677, 2140)

The shape is mechanical. `parent_content_height` mirrors it.

### The circularity problem (critical honest constraint)

`layout_element`'s flow today:
1. Recurse into children (children advance `cursor_y`)
2. **After** children, apply explicit `height:` value:
   `cursor_y = content_y + eh` (line 1839)

**The parent does not know its own content height when recursing
into children.** For a fixed-height parent (`height: 500px`),
the value is known at step 1. For an auto-height parent, the
final height is unknowable until step 2.

CSS spec resolves this:
> A percentage height on a child of an auto-height parent is
> treated as `auto`.

So ADR-057's correct behavior is:

| Parent's CSS | Child sees `parent_content_height` | `% height` on child |
|---|---|---|
| `height: 500px` (explicit) | `500` | resolves to `500 * pct` |
| `height: 50%` + grandparent explicit | resolves recursively | resolves |
| `height: auto` (default) | `0` | **skips** (matches CSS) |

**Zero is not a limitation — it's CSS spec.** Auto-height
parents genuinely don't have a content height during child
layout.

### `<html>` special case

`<html>` and `<body>` are the two elements where `height: 100%`
is common and where "parent" is `viewport`. CSS spec:
> `html { height: 100% }` resolves against the initial
> containing block, i.e. the viewport.

ADR-052 already threaded `viewport_h` to `layout_element`. So
the `<html>` path can special-case `%` → `viewport_h` without
needing `parent_content_height` at all.

## Decision

Ship `parent_content_height` threading + `%` height / min-height
/ max-height resolution. Defer `top: 50%` and `translateY(-50%)`
to a follow-up ADR.

### LengthContext

```cpp
struct LengthContext {
    // ... existing fields ...
    float parent_content_width = 0.0f;      // pre-existing
    float parent_content_height = 0.0f;     // NEW (ADR-057)
    // ... viewport_w / viewport_h ...
};
```

### `layout_element` signature

Add `parent_content_height` param immediately after
`parent_content_width`:

```cpp
LayoutBox layout_element(const dom::Element& el,
                         const css::ComputedStyleMap& styles,
                         const TextMeasurer& measurer,
                         float x_before_margin,
                         float y_before_margin,
                         float parent_content_width,
                         float parent_content_height,   // NEW
                         float parent_font_size,
                         float root_font_size,
                         float viewport_w,
                         float viewport_h,
                         Direction parent_dir,
                         ImageDimLookup image_dims);
```

### `ctx` population (line ~372)

```cpp
ctx.parent_content_width = parent_content_width;
ctx.parent_content_height = parent_content_height;  // NEW
```

### New helper: `parse_length_for_height`

A variant of `parse_length` that resolves `%` against
`parent_content_height` instead of `parent_content_width`:

```cpp
// ADR-057: parse a length where `%` resolves against the
// parent's content height. Falls back to parse_length for
// non-percentage values.
float parse_length_for_height(std::string_view s,
                              const LengthContext& ctx);
```

Implementation: detect trailing `%`, strip, parse number,
multiply by `ctx.parent_content_height / 100.0f`. Otherwise
delegate to `parse_length`.

### Three skip-guard removals

| Site | Line | Before | After |
|---|---|---|---|
| `height:` | 1835 | `h->find('%') == npos` | (removed) + use `parse_length_for_height` |
| `min-height:` | 1848 | `mh->find('%') == npos` | same |
| `max-height:` | 1857 | `xh->find('%') == npos` | same |

**`<html>` / `<body>` special case**: when `el.tag()` is
`"html"` and the height value contains `%`, resolve against
`viewport_h` instead of `parent_content_height`. Same for
`<body>` since `<body>`'s parent is `<html>` whose height
resolves from viewport (all common CSS resets do
`html, body { height: 100% }`).

### 8 recursive call sites: compute parent's own height before recursing

At each `layout_element(child_el, ...)` call, the new
`parent_content_height` argument is either:
- The parsed value of the current element's own explicit
  `height:` (if `height: Npx` or `height: N%` resolved), or
- `0.0f` if `height: auto`

To avoid duplicating this computation 8 times, add a helper:

```cpp
// ADR-057: compute what a parent's content height will be,
// before child layout runs. Uses only the parent's own CSS
// (height: Npx, height: N% with ctx, or 0 for auto).
float own_declared_content_height(
    const css::ComputedStyle* style,
    const LengthContext& ctx,
    float viewport_h,
    const std::string& tag);
```

Called once at top of `layout_element`, result cached in a
local `float own_content_height`. Every recursive call passes
this value (not the parent's own `parent_content_height`).

## Design decisions

### Why `%` skips for auto-height parents are correct (not a limitation)

CSS spec explicitly resolves percentage heights on auto-height
parents to `auto`. This is not "we can't compute it" — it's
"the spec says it's auto." An auto-height parent's content
height is only known after child layout. Treating `%` as auto
in that case matches Chromium, Firefox, and WebKit exactly.

**This means the visible delta is narrower than the deferral
list suggests.** Elements with `%` height on auto-height
parents still render as today. Elements with `%` height on
fixed-height parents get the correct resolution.

### Why `<html>` / `<body>` special-cases on viewport, not parent

A naïve mirror of `parent_content_width` would give `<html>`
a `parent_content_height = 0` (since `<html>`'s "parent" is
the document, which has no content height). That would make
`html, body { height: 100% }` (near-universal CSS reset) still
skip, defeating the whole cycle for the most common case.

CSS spec: `<html>`'s containing block is the initial
containing block = viewport. So `%` on `<html>` / `<body>`
resolves against `viewport_h`. ADR-052 already made `viewport_h`
correct in production (`viewport=1024x678`).

**Consequence**: `html, body { height: 100% }` now correctly
resolves on both — this is a real, visible change to page
structure (the document body actually fills the viewport).

### Why `top: 50%` and `translateY(-50%)` are deferred to ADR-058

`top: 50%` on the search magnifier needs to resolve against
`parent_content_height`. With ADR-057 shipped,
`.cdx-text-input` (the search box) has explicit `height: 42px`
(from the CSS), so `top: 50%` would resolve to `21`.

**But shipping `top: 50%` alone regresses the magnifier.**
Today it renders at `y=7` (container top). With `top: 50%`
resolved but `translateY(-50%)` still skipped, it moves to
`y=7+21=28` — visibly worse (bottom of container instead of
top). Only with `translateY(-50%)` does it land at `y=18`
(centered).

So `top: 50%` ships alongside `translateY` in ADR-058, not
here. ADR-057 provides the `parent_content_height` that ADR-058
needs — but ADR-057 itself only consumes it for block-flow
`height` / `min-height` / `max-height`, not absolute positioning
or transform.

**Magnifier chain after ADR-057:**

| Layer | Status |
|---|---|
| `background-image` renders | ✅ ADR-056 |
| `parent_content_height` available | ✅ ADR-057 (this ADR) |
| `top: 50%` resolves | ⏸ ADR-058 (alongside translateY) |
| `translateY(-50%)` centers | ⏸ ADR-058 |

One step closer, one step to go.

### Why 8 call sites is acceptable (no `parent_content_height` abstraction)

Mirroring `parent_content_width`'s threading pattern means 8
sites × 2 lines each (compute `own_content_height` once at
top, pass it 8 times). Adding an abstraction (struct of parent
dimensions, or context object) would be cleaner long-term but
is larger scope. Deferred to a future refactor ADR if it
becomes painful.

Same pattern ADR-052 chose for `viewport_h`: mechanical
threading, no abstraction.

## Deferred

- **`top: 50%` resolution** — ADR-058 (needs translateY ship together)
- **`translateY(-50%)`** — ADR-058
- **`%` width on inline elements** — unrelated, separate
- **`%` margin / padding** — different resolution (parent width, already works)
- **`%` border-radius** — separate concern
- **Percentages in `calc()`** — `calc(50% - 48px)` — works if `%` resolves; edge cases (mixed `%` + `vh`) deferred
- **`min-height` on `<html>` / `<body>`** — same `<html>` special case applies, deferred to ADR-058 if not naturally handled

## Consequences

**Better:**
- **`html, body { height: 100% }`** now resolves — the CSS reset used by nearly every site starts working
- **`height: N%` on fixed-height parents** resolves correctly
- **`min-height` / `max-height` with `%`** on fixed-height parents resolve
- **ADR-039's `%` height deferral** closed
- **ADR-053's `%` max-height deferral** closed
- **ADR-058's prerequisite** shipped — magnifier chain is now 2 layers from complete
- **Test count 663 -> ~670**

**Accepted limitations:**
- **Auto-height parents still treat `%` as auto** — CSS spec correct, but means many `%` heights on real pages still skip (because most parents are auto-height). The delta is real but narrow.
- **`top: 50%` still doesn't resolve** — ADR-058
- **Magnifier still renders ~10px too high** — unchanged from ADR-056; ADR-058 will fix
- **8 call sites touched** — mechanical but the largest threading surface since ADR-052

**Unchanged:**
- ADR-005 through ADR-056 behavior
- `%` width (unchanged, different field)
- Auto-height parents' `%` skip (CSS spec)
- `<img>` `<img>` layout (no % height resolution path)

**Possible regression risks (verify in Commit 1):**
- **`html, body { height: 100% }` resolving** changes every page's document body height from "content height" to "viewport height". On short pages (docs/demo.html), the body now fills the viewport → body background-color paints across the full viewport. Visible but **correct**.
- **Explicit `height: Npx`** on parents now makes children's `%` heights resolve → some tests may flip.
- **`<html>` special case** needs tag comparison — could accidentally catch `<html>` subclasses (none in HTML spec, but defensive `el.tag() == "html"` comparison).
- **Existing tests at test_box_model.cpp:109-125** will need to flip (currently asserts skip).

## Implementation plan

Two commits:

1. **Threading + `%` resolution + tests**:
   - `style_values.hpp`: add `parent_content_height` to `LengthContext`
   - `style_values.cpp`: add `parse_length_for_height`
   - `layout.hpp`: `layout()` and `layout_with_pseudo()` signatures gain `parent_content_height` (default 0)
   - `layout.cpp`: `layout_element` signature + `ctx.parent_content_height` + `own_declared_content_height` helper + 8 call sites updated + 3 skip-guard removals + `<html>` special case
   - `test_box_model.cpp`: flip the `%` height test + add 4-5 new tests
   - `test_layout.cpp`: verify existing tests still pass

2. **Docs**: ADR-057 -> Implemented, Post-ADR-057 notes in 039 / 053 / 054, README + CHANGELOG

## Verification

**Test flip**: `"height: percentage skipped (parent_content_height deferred)"` becomes `"height: percentage resolves against explicit parent"` — `parent height: 100px, child height: 50%` → child height = 50px.

**New tests**:
- `<html height: 100%>` → resolves to `viewport_h`
- `height: 50%` on auto-height parent → skips (CSS spec)
- `height: 50%` on fixed-height (200px) parent → 100px
- `min-height: 50%` on fixed-height parent → 100px
- `max-height: 50%` on fixed-height parent with tall content → clamps to 100px

**Synthetic fixture**:
`docs/adr057-percent-height-test.html`:
- Row 1: `html, body { height: 100% }` — body fills viewport
- Row 2: `.parent { height: 200px } .child { height: 50% }` — child = 100px
- Row 3: `.parent-auto .child { height: 50% }` — child = content (skip)
- Row 4: `.parent { height: 200px } .child { max-height: 30% }` — clamps to 60px

**Wikipedia production verification**:
Screenshot before/after on HTML article. Expected: the body background (`#fff` from Wikipedia's CSS reset) now covers the full viewport on short pages, and any element with `height: N%` inside a fixed-height parent gets correct sizing. Magnifier position: unchanged (translateY still deferred).

**Layout-tree verification**: log the computed `box.height` for a synthetic `<html height: 100%>` case; should be `viewport_h` (678 at default test setup).

**Test count target**: 663 -> ~670 (+7 tests, 1 flipped).

## Acceptance criteria

ADR-057 is complete when:

- [ ] `LengthContext::parent_content_height` field exists
- [ ] `parse_length_for_height` resolves `%` against `parent_content_height`
- [ ] `layout_element` threads `parent_content_height`
- [ ] All 8 recursive call sites updated
- [ ] 3 skip guards removed (height, min-height, max-height)
- [ ] `<html>` / `<body>` special-case resolves `%` against `viewport_h`
- [ ] ~7 new tests pass (663 -> ~670)
- [ ] Test at test_box_model.cpp:109 flipped
- [ ] Fixture shows rows 1, 2, 4 correctly; row 3 skips (CSS spec)
- [ ] Wikipedia screenshot: `html, body { height: 100% }` takes effect (body fills viewport)
- [ ] Magnifier position unchanged from ADR-056 (~10px high; ADR-058 will center)
- [ ] Post-ADR-057 notes in ADR-039, ADR-053, ADR-054
- [ ] README row + CHANGELOG
- [ ] ADR-057 status -> Implemented

## Implementation

Implemented on `feat/parent-content-height` across two commits
(Commit 1 = threading + % resolution + tests, Commit 2 = this
docs update).

### What shipped

**`style_values.hpp`:**
- `LengthContext::parent_content_height = 0.0f`
- `parse_length_for_height()` declaration

**`style_values.cpp`:**
- `parse_length_for_height()` (~50 lines): trailing `%` resolves
  against `ctx.parent_content_height`; non-% delegates to
  `parse_length`; returns 0 when `parent_content_height == 0`
  (CSS spec: % on auto parent -> auto)

**`layout.hpp`:**
- `layout()` and `layout_with_pseudo()` gain
  `parent_content_height = 0.0f` at the **end** of the param
  list (protects `render_pipeline.cpp`'s positional call)

**`layout.cpp`:**
- `layout_element` signature: `parent_content_height` at
  position 7 (after `parent_content_width`)
- `own_declared_content_height()` helper (~10 lines):
  computes what the element's content height WILL be from its
  own `height:` CSS
- `<html>` special case: `parent_content_height = viewport_h`
  for `<html>` elements (CSS spec: initial containing block)
- 8 recursive `layout_element` call sites updated to pass
  `own_content_height`
- 3 skip guards removed: `height: %`, `min-height: %`,
  `max-height: %` now use `parse_length_for_height`
- `layout()` and `layout_with_pseudo()` **definition bodies**
  gained the param (they were initially missed — the .hpp
  declaration updated but .cpp definition didn't, producing a
  `'parent_content_height' was not declared` build error,
  caught immediately)

**`test_box_model.cpp`:**
- 6 new ADR-057 tests covering the full chain (see Verified
  delta below)

**`docs/adr057-percent-height-test.html`:**
- 4-row fixture: body 100%, fixed-parent 50%, auto-parent 50%
  skip, max-height 30% clamp

**`docs/README.md`:**
- Fixture list updated

### Verified delta (test-verified)

**669 / 2562 all pass** (was 663 / 2550). Six new tests
prove every layer:

| Test | Proves |
|---|---|
| `html height:100%` -> viewport_h (600) | `<html>` special case fires |
| `html,body height:100%` chains -> 600 | Full html->body->x propagation via `own_content_height` |
| `height:50%` on fixed 200px parent -> 100 | `%` resolves against explicit parent height |
| `height:50%` on auto parent -> skips | **CSS spec correctness** -- auto parent yields 0 |
| `max-height:30%` on fixed parent -> 60 | Guard removal + `parse_length_for_height` works |
| `min-height:50%` on fixed parent -> 100 | Same |

**Zero existing test changes.** The test at
`test_box_model.cpp:109` still passes unchanged -- its
`run()` uses default `viewport_height=0`, so `%` correctly
resolves to 0 in that specific setup. (See Honest corrections
below.)

### Honest corrections to the spec

**1. "Test at line 109 flips" was wrong.** The ADR spec
claimed the `"height: percentage skipped"` test would flip
after ADR-057. It doesn't -- its `run()` uses the default
`viewport_height=0`, so `<html>` gets
`parent_content_height = 0`, `height: 100%` resolves to 0,
and the whole chain stays at 0. The test's assertions
(`height > 0`, `height < 100`) still pass unchanged.

**The 6 new tests fill the gap** by passing explicit
`viewport_height=600` and asserting `%` resolution under
real conditions. The old test now documents the
"auto-viewport edge case" behavior; the new ones document
the "real viewport" behavior.

**2. The stale comment at line 109** says "LengthContext has
only parent_content_width, no parent_content_height" -- now
factually false. Corrected in a follow-up edit within this
same docs commit.

**3. Definition bodies needed updating too** (build error
during Commit 1). Same class of split-declaration bug as
ADR-051's `parse_operand` discovery: the .hpp and .cpp
definitions are independent scopes, and my anchor-based
patch only updated one. Caught immediately by the compiler,
fixed before the checkpoint run.

### The circularity constraint applied

**Auto-height parents skip `%`** -- CSS spec correctness,
not a limitation. `layout_element` recurses into children
first, then applies its own explicit `height:`. An
auto-height parent doesn't know its own content height
during child layout, so `parent_content_height` passed to
children is 0. `parse_length_for_height` returns 0 on 0
parent height. The guard skips -- matching Chromium,
Firefox, and WebKit exactly.

**This means the visible delta is narrower than the
deferral list suggests**: many `%` heights on real pages
still skip because most parents are auto-height. The
delta is specifically: `%` on **fixed-height parents**
(including the `<html>` / `<body>` case where the initial
containing block is viewport).

### Visible delta (production)

**`html, body { height: 100% }` now resolves** -- the CSS
reset used by nearly every site starts working. On short
pages, the body background extends to the full viewport
height (visible, correct). On Wikipedia, the article body
height is now `max(content, viewport)` instead of just
`content` -- any element with `background-color` on body
paints across the viewport.

**Magnifier position unchanged**: still ~10px high. This
ADR is the prerequisite for ADR-058's
`translateY(-50%)` chain. The magnifier needs both
`top: 50%` and `translateY(-50%)`, and shipping
`top: 50%` alone would move it from `y=7` to `y=28` (a
regression). So ADR-058 ships them together.

### Test count

- 663 -> 669 cases (+6 new)
- 2550 -> 2562 assertions (+12 new)
- Zero existing tests changed

### Deferred (documented)

- **`top: 50%` resolution** -- ADR-058 (alongside
  translateY, since shipping alone regresses the magnifier)
- **`translateY(-50%)`** -- ADR-058
- **`%` width on inline elements** -- separate concern
- **`%` margin / padding** -- already work against
  `parent_content_width` (untouched)
- **`min-height` / `max-height` on `<html>` / `<body>`** --
  the `<html>` special case applies to explicit `height:`,
  not the clamps. Extending to min/max-height is possible
  but not needed by any current fixture.

