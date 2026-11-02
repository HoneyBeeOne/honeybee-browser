# ADR-062: `linear-gradient()` background support

**Status:** Accepted
**Date:** 2026-11-02
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-034 (image paint), ADR-041 (pseudo-element synthesis), ADR-050 (ancestor culling diagnostic), ADR-055 / 056 (background shorthand + URL forms)

## Context

ADR-055 shipped `background:` color forms. ADR-056 shipped URL forms. **`linear-gradient()` remains the last 1 of 11 Wikipedia `background:` forms still dropped entirely** (`background: linear-gradient(...)` → declaration discarded).

### Wikipedia usage (measured 2026-11-02)

The only `linear-gradient` in the site.styles bundle:

```css
.vector-sticky-pinned-container::after {
    background: linear-gradient(rgba(255,255,255,0),var(--background-color-base,#fff));
    position: sticky;
    ...
}
```

A fade-out overlay (transparent → white) on the bottom of the TOC sidebar.

### The triple-blocking finding (critical for verification category)

**The Wikipedia gradient target is invisible today for three independent reasons outside ADR-062's scope:**

| Layer | Status | Blocking ADR |
|---|---|---|
| `.vector-sticky-pinned-container` ancestor | `h=0` (culled) | ADR-050 diagnostic; separate culling ADR |
| `::after` pseudo-element | Synthesized (ADR-041) but parent is `h=0` | Nothing to see |
| `position: sticky` | Deferred | Separate scroll-aware layout ADR |

**Consequence**: even after ADR-062 ships, the Wikipedia gradient target will still not render. The verification category is **synthetic-fixture-verified only** (same as ADR-054's rotate), not production-verified.

**This is not a reason to skip the work** — the CSS parser gap is real (1/11 forms dropped), the paint infrastructure is reusable for any future visible gradient (buttons, headers, dialogs), and closing the last `background:` form gaps the grammar regardless of current Wikipedia visibility.

### What exists that supports this

| Component | Status | Reusable |
|---|---|---|
| `parse_color(sv)` | Shipped (ADR-013) | For gradient stops |
| `split_commas` + `trim_ws` | Shipped (ADR-037, in style_values.cpp) | For gradient argument splitting |
| `try_expand_background` (ADR-056) | Shipped | Where the gradient detection goes |
| `paint_background_image` (ADR-056) | Shipped | Sibling function site |
| Cairo `cairo_pattern_create_linear` + `cairo_pattern_add_color_stop_rgba` | Available (system cairo) | For gradient fill |
| `LayoutBox` background fields | 6 fields (size/pos/repeat) | Extend with gradient metadata |

## Decision

Ship `linear-gradient()` support across parser + layout + paint. **Synthetic-fixture verified only** (Wikipedia target is triple-blocked; document this honestly).

### Grammar scope (exactly what Wikipedia's CSS requires + degenerate cases)

| Form | Example | Status |
|---|---|---|
| **Default direction** | `linear-gradient(red, blue)` | 180deg (top→bottom) |
| **Angle** | `linear-gradient(45deg, red, blue)` | Any `Ndeg` |
| **Keyword direction** | `linear-gradient(to bottom, ...)`, `to top/left/right` | Included |
| **Corner direction** | `linear-gradient(to bottom right, ...)` | **Included** (diagonal corners) |
| **2+ stops** | `linear-gradient(red, green, blue)` | Included |
| **Explicit stop positions** | `linear-gradient(red 0%, blue 100%)` | **Deferred** (parse accepted, positions ignored — uniform interpolation) |
| **`rgba()` / `var()` colors** | Wikipedia's exact form | Included (`var()` resolved by cascade before parser) |
| **Radial / conic / repeating** | `radial-gradient(...)` | **Deferred** (drop entirely) |
| **Color-hint syntax** | `red, 50%, blue` | **Deferred** (drop entirely) |
| **Multiple stop positions** | `red 20% 40%` | **Deferred** |
| **Interpolation spaces** | `in oklab` | **Deferred** |

**Explicit forms to accept**: default, `Ndeg`, `to <keyword>`, `to <keyword> <keyword>`. Stop colors via `parse_color` (supports `#hex`, named, `rgb(a)`, `transparent`). Positions: any `<N>%` or `<N>px` — parsed but only `0%` and `100%` produce correct uniform interpolation; intermediate positions treated as uniform.

### Data model

`layout_box.hpp` gets 4 new fields:

```cpp
// ADR-062: linear-gradient background.
struct BackgroundGradientStop {
    Color color;
    float position;  // 0.0 .. 1.0
};
bool background_is_gradient = false;
float background_gradient_angle_deg = 180.0f;  // default: top -> bottom
std::vector<BackgroundGradientStop> background_gradient_stops;
```

### Parser (`stylesheet.cpp`)

`try_expand_background` (line 617 area) currently drops the whole declaration when it contains `gradient(`:

```cpp
if (value.find("gradient(") != std::string_view::npos) return false;
```

**Replace with**:

```cpp
// ADR-062: linear-gradient -- preserve as background-image: <raw>.
// Layout parses the structured form from the raw string.
if (value.find("linear-gradient(") != std::string_view::npos) {
    // Extract the linear-gradient(...) token (respect nested parens
    // for var() / rgba() inside stops). Emit as background-image.
    // Any comma outside the gradient's own parens still drops
    // (multi-layer).
    auto open = value.find("linear-gradient(");
    int depth = 0;
    std::string_view grad;
    for (size_t i = open; i < value.size(); ++i) {
        if (value[i] == '(') ++depth;
        else if (value[i] == ')') {
            if (--depth == 0) {
                grad = value.substr(open, i - open + 1);
                break;
            }
        }
    }
    if (grad.empty()) return false;
    // Check for stray commas outside the gradient (multi-layer).
    for (size_t i = 0; i < value.size(); ) {
        if (i == open) { i += grad.size(); continue; }
        if (value[i] == ',') return false;
        ++i;
    }
    out.push_back({"background-image", std::string(grad), important});
    return true;
}
if (value.find("gradient(") != std::string_view::npos) return false;
```

**Deliberately minimal parser work**: the shorthand just preserves the raw `linear-gradient(...)` as `background-image`. **Structured parsing happens in layout**, where we already have `parse_color` + `split_commas` + full context.

### Layout (`layout.cpp`)

In `layout_element` (line 302-324), after `background_image_url` extraction:

```cpp
// ADR-062: parse linear-gradient into structured form.
if (style) {
    if (auto bg = style->get("background-image")) {
        if (bg->find("linear-gradient(") != std::string::npos) {
            parse_background_gradient(*bg, box);
        }
    }
}
```

Where `parse_background_gradient(sv, box)`:
1. Strip `linear-gradient(` prefix and `)` suffix
2. Split on top-level commas (respect nested `()` for `rgba()` / `var()`)
3. First token: if it starts with `to ` or ends with `deg` → direction; else → first stop color
4. Remaining tokens: each is `<color>` or `<color> <position>`
5. Position: `<N>%` → `N/100`; `<N>px` → `<N> / box.width` (or height, depending on angle)
6. Store structured form in `box.background_is_gradient` / `box.background_gradient_*`

Helper `parse_background_gradient` lives in `layout.cpp`'s anonymous namespace (near `fill_sides`).

### Paint (`paint.cpp`)

New `paint_background_gradient` next to `paint_background_image`:

```cpp
void paint_background_gradient(cairo_t* cr,
                               const layout::LayoutBox& box) {
    if (!box.background_is_gradient) return;
    if (box.background_gradient_stops.size() < 2) return;

    // Compute line endpoints from angle + box rect (CSS linear-gradient
    // spec formula: https://drafts.csswg.org/css-images-3/#linear-gradients)
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    const float rad = box.background_gradient_angle_deg
                    * 0.017453292519943295f;
    const float ca = std::cos(rad);
    const float sa = std::sin(rad);
    // Half-diagonal length: (w*|cos| + h*|sin|) / 2
    const float hl = (box.width * std::abs(ca)
                    + box.height * std::abs(sa)) * 0.5f;

    const float x0 = cx - ca * hl;
    const float y0 = cy - sa * hl;
    const float x1 = cx + ca * hl;
    const float y1 = cy + sa * hl;

    cairo_save(cr);
    cairo_pattern_t* pat = cairo_pattern_create_linear(x0, y0, x1, y1);
    for (const auto& stop : box.background_gradient_stops) {
        cairo_pattern_add_color_stop_rgba(pat, stop.position,
            stop.color.r, stop.color.g, stop.color.b, stop.color.a);
    }
    cairo_set_source(cr, pat);
    cairo_rectangle(cr, box.x, box.y, box.width, box.height);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
}
```

**Call site** in `paint_box`:

```cpp
// ADR-062: gradient background painted first (it's a fill,
// not an image), then any URL background image on top.
if (box.background_is_gradient) {
    paint_background_gradient(cr, box);
}
if (images && !box.background_image_url.empty()) {
    paint_background_image(cr, box, images);
}
```

This allows gradient + url() to coexist (layered) in the rare CSS `background: url(x), linear-gradient(...)` case — though that form drops in the parser (multi-layer) for v1.

## Design decisions

### Why parse structured form in layout (not in css parser)

`css/` cannot call `layout::parse_color` (layering: `css/` has no `layout/` dependency, established in ADR-055). And `parse_color` is the canonical color parser — reimplementing it in `css/` would duplicate 100 lines.

**Cleaner**: the css parser just preserves the raw string as `background-image`. Layout parses into structured form where it already has `parse_color` + `LengthContext` + box dimensions for `px` position resolution.

Same pattern as ADR-056's `url()` handling: raw string stored, layout resolves.

### Why separate `paint_background_gradient` (not merged into `paint_background_image`)

Different Cairo primitives: `cairo_pattern_create_for_surface` vs. `cairo_pattern_create_linear`. Different lifecycles. Merging would need type dispatch on `background_is_gradient` at the top — adding complexity to the image path (which is hot, called per box per frame).

**Separate functions, dispatch at the call site** (2 lines). The gradient path is cold (most boxes are solid color); the image path stays untouched.

### Why the angle formula uses half-diagonal (`hl`)

CSS linear-gradient spec §3.1.1: the gradient line passes through the box center, length = distance from center to corner in the gradient direction. For angle θ:

```
hl = (w * |cos θ| + h * |sin θ|) / 2
```

For a zero-width or zero-height box (image without intrinsic dims), we skip paint — but the paint function guards on `stops.size() < 2` and `background_is_gradient` before entering, so degenerate boxes don't reach the formula.

### Why include `to <corner>` forms (e.g. `to bottom right`)

Diagonal corners are common in the real web (`linear-gradient(to bottom right, ...)`). The angle arithmetic:

| `to <keyword>` | Effective angle |
|---|---|
| `to bottom` | 180° |
| `to top` | 0° |
| `to right` | 90° |
| `to left` | 270° |
| `to bottom right` | 135° |
| `to top left` | 315° |

Trivial to include (8-line switch), no cost, and makes the parser useful for future pages. **Include them**.

### Why NOT ship stop position parsing in v1

`linear-gradient(red 0%, blue 100%)` — the positions are `0%` and `100%`, which are the CSS defaults. Real gradient-with-positions:

```
linear-gradient(red, yellow 20%, blue)
```

Here `yellow` sits at 20%, and the browser auto-fills `red` at 0% and `blue` at 100%. This requires a "position fill algorithm" (positions clamp and re-spread) that's ~60 lines of non-obvious logic.

**Wikipedia's exact form uses no positions.** Every other form in the corpus either has default `0%, 100%` or no positions at all. So v1 parses positions but **ignores them** (uniform interpolation between stops). If a stop is `red 20%`, we paint it at `1/(N-1)` in the uniform spacing, not at 20%.

**Honest limitation**: documented. If positions matter later, it's a ~60-line ADR on top of this.

### Why not radial / conic / repeating

No Wikipedia consumer. Same discipline as ADR-056 url forms — ship what Wikipedia needs, defer the rest.

## Deferred

- **Explicit stop positions** (`red 20%`) -- parsed but treated as uniform
- **Radial / conic gradients** -- no consumer
- **Repeating gradients** -- no consumer
- **Multi-layer** (`url(x), linear-gradient(...)`) -- drops entirely (parser comma check)
- **Color interpolation spaces** (`in oklab`) -- not parsed
- **`::before` / `::after` pseudo-element gradient render** -- possible (cascade populates pseudo styles via ADR-041, `layout_element` runs on pseudo boxes via ADR-042) but the Wikipedia target is ancestor-culled (see Context); synthetic fixture will verify the paint works, not pseudo integration
- **`position: sticky`** -- the Wikipedia target's third blocker, deferred

## Consequences

**Better:**
- **Last remaining `background:` form parses** (11/11 forms now handled: 10 render, 1 = linear-gradient which parses + paints but is ancestor-culled on Wikipedia)
- **New paint primitive** (`paint_background_gradient`) available for any future visible gradient
- **`BackgroundGradientStop` struct** reusable for radial / conic if they land
- **Test count 683 -> ~689** (6 new tests)

**Accepted limitations:**
- **Synthetic-fixture-verified only** -- the Wikipedia gradient target is ancestor-culled (`h=0` on `.vector-sticky-pinned-container` per ADR-050). This is the same class as ADR-054's rotate (renders correctly, no production visual delta).
- **Stop positions ignored** -- uniform interpolation. Wikipedia's form is unaffected (uses defaults).
- **Radial / conic / repeating deferred.**
- **Gradient + URL coexistence deferred** (multi-layer drops).

**Unchanged:**
- ADR-005 through ADR-061 behavior
- All existing `background:` color/url forms
- Paint dispatch for solid color and URL backgrounds

**Possible regression risks (verify in Commit 1):**
- **Parser**: gradient detection must not break URL forms with `background: url(x) repeat` — the gradient branch fires first (before url detection? or after?). **Order matters**: check gradient first, then url. If a value has both, drop (multi-layer).
- **Layout**: `parse_background_gradient` must reset fields when called (in case of relayout overwriting); or be called only on fresh `LayoutBox` construction (which is the case — `layout_element` builds one box per element per pass).
- **Paint**: call order (gradient first, image second) — no current test exercises the coexistence, so behavior is undocumented but correct.
- **Existing background tests**: 125 in test_stylesheet.cpp — should pass unchanged since gradient was previously dropped, now preserved. But the `"gradient dropped entirely"` test (if it exists) needs flipping.

## Implementation plan

Two commits:

1. **Parser + LayoutBox + layout parse + paint + tests**:
   - `stylesheet.cpp`: gradient branch in `try_expand_background`
   - `layout_box.hpp`: `BackgroundGradientStop` struct + 4 fields
   - `layout.cpp`: `parse_background_gradient` helper + call after URL extraction
   - `paint.cpp`: `paint_background_gradient` + call site in `paint_box`
   - `test_stylesheet.cpp`: parser tests (gradient preserved as background-image)
   - `test_layout.cpp`: layout tests (structured form populated)
   - Fixture: `docs/adr062-gradient-test.html`

2. **Docs**: ADR-062 -> Implemented, Post-ADR-062 notes in ADR-034 / 056, README + CHANGELOG

## Verification

**Unit tests (~6):**
- Parser: `linear-gradient(red, blue)` -> `background-image: linear-gradient(red, blue)` (raw preserved)
- Parser: `linear-gradient(45deg, red, blue)` -> preserved
- Parser: `linear-gradient(to bottom right, red, blue)` -> preserved
- Parser: `radial-gradient(...)` -> dropped (deferred)
- Layout: gradient string -> `background_is_gradient = true`, 2+ stops populated
- Layout: `linear-gradient(red, blue)` -> stops `[red@0.0, blue@1.0]`

**Synthetic fixture verification:**
`docs/adr062-gradient-test.html` -- 5 rows:
- Row 1: solid color control
- Row 2: `linear-gradient(red, blue)` -- vertical default
- Row 3: `linear-gradient(45deg, red, blue)` -- diagonal
- Row 4: `linear-gradient(to right, red, white, blue)` -- horizontal 3 stops
- Row 5: `linear-gradient(rgba(255,255,255,0), white)` -- Wikipedia's exact form

**Screenshot verification** -- visual confirmation that gradients render (this is the only proof since production is ancestor-culled).

**Wikipedia regression check:** screenshot before/after ADR-062 -- **expect identical** (gradient target remains invisible due to ancestor culling; only the parser changes).

**Verification category: synthetic-fixture-verified.**

## Acceptance criteria

ADR-062 is complete when:

- [ ] `try_expand_background` preserves `linear-gradient(...)` as `background-image`
- [ ] `LayoutBox` has `background_is_gradient` + angle + stops fields
- [ ] `parse_background_gradient` populates structured form in `layout_element`
- [ ] `paint_background_gradient` fills with Cairo linear pattern
- [ ] ~6 new tests pass (683 -> ~689)
- [ ] Fixture shows 5 rows correctly (gradients render)
- [ ] Wikipedia screenshot: unchanged (target ancestor-culled; parser-only change)
- [ ] Post-ADR-062 notes in ADR-034 / 056
- [ ] README row + CHANGELOG
- [ ] ADR-062 status -> Implemented
