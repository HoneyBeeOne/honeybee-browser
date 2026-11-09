# Changelog

All notable changes to the HoneyBee Browser are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- **CSS border shorthand expansion** (ADR-040): `border: <width> <style> <color>` and
  per-side forms (`border-top/right/bottom/left`) now expand to
  longhands at parse time. Issue-box borders now appear on
  Wikipedia (1px confirmed via PNG). `border: 5px none red`
  correctly zeroes width per CSS spec SS8.5.3. Zero layout/paint
  changes — pure cascade fix.

  **Deferred**: `h1#firstHeading` underline not appearing (rule
  not reaching cascade; likely media-type gate, tracked as
  separate issue). Search field border blocked on ADR-029 header
  layout.

- **Pseudo-elements parse + cascade** (ADR-041, Commit 1): `::before` / `::after`
  selectors now parse and their declarations enter
  `pseudo_styles` in the cascade result. 14 new tests,
  504 -> 518 cases.

  **Zero visual change in this commit** — pseudo-box synthesis
  (the visible-delta step) is deferred to ADR-042 because the
  primary target requires `position: absolute` (ADR-033 parses
  but doesn't lay out) plus flex-parent pseudo synthesis.
  ADR-041's stated 4 first-viewport changes did not manifest;
  the parse + cascade layer is the real, tested deliverable.

- **Position: relative + absolute** (ADR-042): `position: relative`
  marks a containing block; `position: absolute` children are
  out-of-flow and positioned against their parent's content
  box. All four offsets (top/bottom/left/right) with auto and
  over-constrained rules.

  **Pseudo-element synthesis**: `::before` / `::after` boxes
  are now synthesized into the layout tree from the pseudo
  styles ADR-041 parses. `content: ''` (empty literal)
  supported; literal text, `attr()`, `counter()`, `url()`
  deferred.

  **Visible delta (PNG-verified)**: title "HTML" underline
  appears via `.vector-page-titlebar::after`. This closes
  ADR-040's prediction (via the mechanism ADR-041 identified)
  and ADR-041's deferred visible synthesis.

  Test count: 518 -> 539 cases, 1980 -> 2064 assertions.

- **Box shadow** (ADR-043): non-inset, non-blurred
  box-shadow rendered as offset + spread rectangle, clipped
  per CSS Backgrounds 3 SS7.1 (shadow drawn outside the
  border-box only, clipped inside). Even-odd fill rule handles
  the "L-shaped exterior region."

  Visible delta (PNG-verified): 1px gray line under
  `.vector-page-toolbar-container` ("Article / Talk / Read"
  tabs). Found and fixed one paint bug during verification
  (full shadow rect visible through transparent parent
  background -- CSS-spec clip resolved it).

  Parsed for future: inset, blurred, and multi-shadow forms
  (all currently :focus state or hidden elements in Wikipedia
  CSS -- deferred to when state-driven layout or scroll
  exists).

  Test count: 539 -> 551 cases, 2064 -> 2125 assertions.

- **Border radius** (ADR-044): `border-radius` shorthand
  (single value -> all four corners) + per-corner longhands.
  Rounded-rect paint via Cairo `arc` with two-fill border.
  Zero-radius boxes take existing rect path (regression-free).

  Visual delta: subtle at 2px radius on 100px+ elements,
  not screenshot-verifiable. Behaviorally complete (parse
  verified via 11 tests; paint branch correct; larger radii
  render visibly). Same "Implemented with honest caveat"
  pattern as ADR-041's partial scope.

  Deferred: percentage radius, `inherit`, multi-value
  shorthand beyond first token, elliptical, transitions,
  rounded box-shadow.

  Test count: 551 -> 562 cases, 2125 -> 2188 assertions.

- **Inline background images** (ADR-045): `background-image` on
  inline elements (`<a>`, `<span>`) now renders. Supports
  `background-size` (fixed length), `background-position`
  (keyword pairs, flexible order), `background-repeat`
  (no-repeat only), and inline `padding-right` (fragment
  width extended, subsequent same-line fragments shifted).

  **Visible delta (scroll-PNG verified)**: external link
  icons (16x14px boxed arrows) appear next to external URLs
  in Wikipedia article body -- verified at External Links
  section, ~6-8 scroll heights down. Zero first-viewport
  delta (lead has no external links, expected).

  Honest scoping: measurement reported 242 `.external`
  elements in HTML, but that count included wrappers,
  mobile-only content, and compound classes. Actual visible
  icon delta: ~15-25, concentrated in External Links section
  plus a few inline in body prose.

  Deferred: `mask-image` (13 of 14 background-size rules --
  separate subsystem), `background-size: cover | contain`,
  percentage size/position, `background-repeat: repeat`,
  multi-layer backgrounds, `background-clip` /
  `background-origin`.

  Test count: 562 -> 572 cases, 2188 -> 2224 assertions.

- **Runtime-gated logging infrastructure** (ADR-046): `src/base/log.{hpp,cpp}`
  with `HB_LOG(cat, lvl, fmt, ...)` macro. Zero cost when
  disabled (single atomic bool read + branch). Enabled via
  `HONEYBEE_LOG` env var; category + level filtering;
  optional file redirect via `HONEYBEE_LOG_FILE`.

  Categories: pipeline, css, cascade, layout, paint, fetch,
  event. Levels: error, warn, info, debug, trace.

  Usage:
    export HONEYBEE_LOG=all
    export HONEYBEE_LOG_LEVEL=debug
    export HONEYBEE_LOG_FILE=/tmp/hb.log
    ./build/bin/honeybee

  Output: `[honeybee][layout][debug] src/.../layout.cpp:482 msg`

  14 unit tests. 12 HB_LOG sites across 6 files (main,
  render_pipeline, async_fetcher, cascade, layout, paint).

  Deferred: net/client.cpp fetch log points (fetch() body
  not captured pre-commit); deep-subsystem log points
  (organic additions).

  Test count: 572 -> 586 cases, 2224 -> 2263 assertions.

- **CSS mask-image** (ADR-047): `mask-image` parsing +
  `paint_mask_image` using `cairo_mask`. Applies loaded
  surface's alpha channel over `background-color` fill
  (CSS Masking 1 SS5 alpha semantics).

  Supports `-webkit-mask-image` prefixes. `mask-size` (fixed),
  `mask-position` (keyword pairs), `mask-repeat` (no-repeat).
  Deferred: `mask-mode: luminance`, `mask-composite`,
  `mask-border`, `mask-clip`, percentage size/position,
  repeat variants.

  Visual delta: ZERO first-viewport. All 28 cascade
  elements with `mask-image` are `.vector-icon` inside
  `display:none` / `visibility:hidden` ancestors. ADR-032
  culls them before paint reaches `paint_mask_image`.
  Infrastructure correct and complete; awaits consumer
  (ADR-029 header flex layout or dropdown-open feature).

  Two bugs caught by unit tests before commit: (1)
  use-after-free from returning `const std::string*` into a
  temporary optional (SIGSEGV on first parse); (2)
  string-replace collision between ADR-045 and ADR-047
  loops.

  Test count: 586 -> 598 cases, 2263 -> 2307 assertions.

- **Flex wrap (multi-line)** (ADR-048): `flex-wrap: wrap` for
  row-direction flex containers. Items break into lines by
  greedy fit test on accumulated basis + gap vs content_w;
  each line resolved independently via extracted
  `resolve_flex_line` helper; lines stack with row-gap
  (gap fallback).

  Closes ADR-029's biggest deferral. 13 synthetic-DOM tests
  verify multi-line behavior.

  Visual delta: ZERO first-viewport. `.mw-header` (the only
  target of `flex-wrap: wrap` in Wikipedia CSS) is not in
  the current fetched HTML, and at viewport >= 1680px uses
  `display: grid` instead. Infrastructure correct and
  complete; awaits a Wikipedia snapshot with the header
  element AND a viewport < 1680px.

  Deferred: `align-content`, `wrap-reverse`, column + wrap,
  cross-axis shrink.

  Test count: 598 -> 611 cases, 2307 -> 2386 assertions.

- **@supports conditional rules** (ADR-050): `@supports`
  blocks are now parsed and evaluated against a hardcoded
  capability table. 73 blocks in Wikipedia CSS reach the
  cascade for the first time. Includes `not`, `or`, `and`,
  `selector(...)`, and unsupported-value checks (`round`,
  `dotted`, `dashed`).

  Critical correctness: `@supports not (mask-image)` blocks
  contain `background-image` fallbacks that would double-render
  icons with ADR-047's mask rules. Evaluator correctly excludes
  them.

  Layout-tree verified: `.vector-pinned-container` now
  `display: block` (was `none`). First-viewport PNG delta
  deferred pending ancestor culling.

  Test count: 611 -> 626 cases, 2386 -> 2407 assertions.

- **Viewport units** (ADR-051): `vh`/`vw`/`vmin`/`vmax` now
  parse in both `parse_length` (declarative values) and
  `parse_operand` (math expressions inside `calc()` / `min()` /
  `max()` / `clamp()`). `LengthContext` gains `viewport_w` and
  `viewport_h`; viewport dims threaded through `layout()` /
  `layout_with_pseudo()` / `layout_element()`.

  Unblocks `calc(100vh - 48px)` (the sticky sidebar's
  `max-height` rule that ADR-050 surfaced as still-unresolved).
  In production, `viewport_height` is passed as 0 (GTK height
  not yet threaded) -- vh/vw resolve to 0, same visible behavior
  as pre-ADR-051.

  18 new tests: direct vh/vw/vmin/vmax, `calc()` combinations,
  padding/border/margin with viewport units, existing
  `%`/`px`/`em`/`rem` regressions, `min()` of vh + px.

  Deferred: `svh`/`lvh`/`dvh`, percentage `max-height`,
  `max-height`/`max-width` honoring in layout, GTK height
  threading.

  Test count: 626 -> 644 cases, 2407 -> 2465 assertions.

- **GTK viewport height threading** (ADR-052): `m_scroll_window.get_height()`
  (visible viewport) now flows through `render()` /
  `render_with_css()` / `relayout()` into `layout_with_pseudo()`
  into `LengthContext.viewport_h`. `on_draw` tracks height in
  `m_last_height`, relayouts on either width or height change.

  Closes ADR-051's Deferred item "GTK allocated height
  threading". `HONEYBEE_LOG=layout` before/after:
  `viewport=1024x0` -> `viewport=1024x678`.

  Zero visible PNG delta (max-height still ignored by layout;
  sticky sidebar still ancestor-culled). Same category as
  ADR-047/048: correct infrastructure before consumer.

  Test count: 644 cases, 2465 assertions (unchanged -- default
  viewport_height=0.0f protects existing test callers).

- **Honor viewport units in max-width/max-height clamps** (ADR-053): 
  the ADR-037-era guard that skipped `vw`/`vh` values in the
  `max-width` and `max-height` clamps is removed. ADR-051 made
  `parse_length` resolve viewport units; ADR-052 made
  `viewport_h` nonzero in production; the guard was stale.

  `max-width: 100vw`, `max-height: 100vh`, and
  `max-height: calc(100vh - 48px)` now clamp -- the exact rule
  on `.vector-sticky-pinned-container`.

  `%` guard on `max-height` preserved (parent_content_height
  threading is separate). `> 0.0f` safety checks preserved
  (initial render with viewport dims 0 skips the clamp, no
  collapse).

  One existing test flipped in place: "max-width: containing
  vw is skipped (not collapsed)" -> "now clamps", expecting
  100 (was 400). This test was explicitly written to guard the
  old behavior.

  3 new tests: 100vw / 100vh / calc(100vh - 48px) clamps.

  Zero visible PNG delta still (`.vector-sticky-pinned-container`
  is h=0 from ancestor culling). Same category as ADR-047/048.

  Test count: 647 cases, 2471 assertions (+3 cases / +6
  assertions).

- **CSS transform: rotate()** (ADR-054): 
  `parse_angle` (deg/rad/turn/grad) + whitelist `transform`
  + `LayoutBox::transform_rotate_deg` + cairo save/rotate/
  restore wrapper around the whole `paint_box` body
  (transform-origin = box center).

  First cycle shipped under the new "synthetic-fixture
  verified" category. Permanent fixture at
  `docs/adr054-rotate-test.html` renders: upright orange
  control square, `rotate(45deg)` orange diamond,
  `rotate(30deg)` tilted blue rectangle -- all three
  confirmed via screenshot 2026-11-01.

  Zero production PNG delta: Wikipedia's only transform
  reaching cascade is `translateY(-50%)` on the search
  magnifier, which the rotate parser skips (field stays 0,
  no paint change).

  New finding documented: CSS shorthand `background: #d35400`
  and `border: 2px solid #333` do not parse yet -- only
  longhand forms render. The fixture was rewritten with
  longhand. Pre-existing parser gap, out of ADR-054 scope.

  4 new tests: parse_angle units, rotate(45deg) layout,
  no-transform layout, translateY(-50%) regression safety.

  Test count: 651 cases, 2490 assertions (+4 cases / +19
  assertions).

- **CSS shorthand `background:` (color forms)** (ADR-055): 
  `try_expand_background` in `expand_shorthand` covers
  `background: #fff`, `#2980b9`, `none` (→ transparent),
  `!important`, and `var(--x, #fff)` (resolved by cascade
  before parser). URL forms, gradients, and multi-layer
  (comma) drop entirely — half-parsing would produce a
  worse state than dropping.

  Verified via permanent fixture at
  `docs/adr055-background-shorthand-test.html`: 5 rows
  covering longhand bg, shorthand bg, shorthand border,
  both shorthands, and cascade order. Screenshot
  2026-11-01 confirmed rows 2 and 4 now fill (blue/purple,
  was white pre-ADR-055); row 5's `none` correctly
  overrides earlier `#c0392b` (was red, now transparent).

  Wikipedia impact: 7 of 11 `background:` forms now
  render (the most common chrome backgrounds) — first
  real production delta since ADR-045. 4 url/gradient
  forms still drop entirely (no regression).

  **Corrects ADR-054's imprecise finding**: the original
  ADR-054 claim "shorthand background: and border: don't
  parse yet" was wrong — only `background:` was failing.
  `border:` shorthand has been working since ADR-040;
  ADR-054's fixture changed five variables at once and
  produced a misleading result.

  7 new tests: color forms, none, !important, url drop,
  gradient drop, multi-layer drop.

  Test count: 658 cases, 2514 assertions (+7 cases / +24
  assertions).

- **CSS `background:` URL forms** (ADR-056): 
  `try_expand_background` now handles the full shorthand
  grammar `[color || image || repeat || position [/ size]]*`
  (order-free per CSS spec). 5 helpers: `is_color_token`,
  `is_repeat_token`, `is_position_token`, `is_url_token`,
  `is_length_token`.

  `LayoutBox` + 5 fields mirroring ADR-047 mask:
  `background_size_w/h`, `background_pos_x/y`,
  `background_no_repeat`. `layout_element` reads
  `background-size`, `-position`, `-repeat`.
  `paint_image` split into `paint_img_element` (unchanged
  `<img>` stretch) + `paint_background_image` (intrinsic
  size, explicit override, position keywords, repeat via
  `CAIRO_EXTEND_REPEAT`, clipped to box).

  **Production delta (screenshot 2026-11-01, Wikipedia HTML
  article): search magnifier renders (was invisible),
  external link icons render (was invisible), Wikipedia
  logo unchanged (img split held), article layout
  unchanged (zero regression).**

  **11/11 `background:` forms parse** (10 render; the
  linear-gradient form still drops for lack of gradient
  support). First cycle since ADR-045 with production-
  verified visual delta on the target page.

  **Magnifier caveat**: renders ~10px too high. Needs
  `translateY(-50%)` (deferred on `parent_content_height`,
  same blocker as ADR-053/054). After the
  `parent_content_height` cycle, the icon will center.

  Fixture: `docs/adr056-background-url-test.html` — 5 rows,
  one per Wikipedia form, self-contained inline
  `data:image/svg+xml` URLs.

  Test count: 663 cases, 2550 assertions (+5 cases / +36
  assertions net; 1 test flipped, 5 new).

- **`parent_content_height` threading + `%` height/max-height** (ADR-057): 
  `LengthContext` gains `parent_content_height`;
  `parse_length_for_height()` resolves trailing `%` against
  parent height (or `viewport_h` for `<html>` / `<body>` via
  the initial containing block rule). `layout_element` threads
  the field through 8 recursive call sites, mirroring
  `parent_content_width`'s shape.

  Three skip guards removed from `layout.cpp`: `height: %`,
  `min-height: %`, `max-height: %` now resolve via
  `parse_length_for_height`. Closes ADR-039's and ADR-053's
  `%` deferrals.

  `<html>` special case: `<html>`'s containing block is the
  viewport, so `html { height: 100% }` -- the near-universal
  CSS reset -- resolves against `viewport_h`. `<body>` picks
  it up as its parent height, and children inherit the chain.

  Auto-parent skip preserved: `%` on an auto-height parent
  resolves to `auto` (CSS spec correctness, not a limitation).

  Verified via 6 new tests: html 100% -> viewport_h,
  html/body/grandchild chain, 50% on fixed 200px parent -> 100,
  50% on auto parent -> skip, max-height 30% -> 60,
  min-height 50% -> 100. Fixture:
  `docs/adr057-percent-height-test.html` (4 rows).

  Visible delta: `html, body { height: 100% }` now works;
  body background extends to full viewport on short pages.

  Enables ADR-058 (translateY + top: 50%) -- the magnifier
  centering chain is now 2 layers from complete.

  Test count: 669 cases, 2562 assertions (+6 cases / +12
  assertions).

- **`top: 50%` + `transform: translateY(-50%)`** (ADR-058): 
  Absolute `top: %` resolves against containing block height
  via `abs_ctx.parent_content_height` + split `parse_off_h`
  / `parse_off_v` lambdas. `transform: translateY()` resolves
  against **own** box height after `box.height` finalized,
  stored in new `LayoutBox::transform_translate_y` field.
  Paint wrapper generalizes `did_rotate` -> `did_transform`
  (rotate OR translate). `bottom: %` works for free.

  **The 5-ADR magnifier chain closes.** Screenshot 2026-11-02:
  search magnifier at `y=18` (was `y=7`), perfectly centered
  in the search box. Chain: 054 (diagnose) -> 055 (bg color)
  -> 056 (bg URL, visible at y=7) -> 057
  (`parent_content_height` prerequisite) -> 058 (centered).

  1 test flipped (`translateY(-50%)` now sets `translate_y`),
  6 new ADR-058 tests covering all layers: translateY px/%,
  rotate+translate, absolute top:50%, top:50%+translateY
  centering, bottom:50%.

  `docs/adr058-top-translate-test.html`: 4-row fixture
  demonstrating the two-%-reference distinction (parent for
  top, own for translateY).

  **Also ships `PERF-CASCADE` / `PERF-LAYOUT` / `PERF-TOTAL`
  timing in `relayout()`** (diagnostic-only). First Release
  measurement on Wikipedia: cascade 1058ms (31%), layout
  2379ms (69%), total 3437ms. Both O(n²)-ish; ADR-059 (perf
  cycle) uses this as Context.

  Test count: 675 cases, 2586 assertions (+6 cases / +24
  assertions).

- **Selector index for cascade performance** (ADR-059): 
  `RuleIndex` buckets rules by rightmost-simple anchor
  (`AnchorBucket`: Id / Class / Type / Universal) at parse
  time. `anchor_for(Selector)` extracts the most selective
  mandatory-match simple from the rightmost compound.
  `candidates_for(id, classes, tag)` returns sorted +
  deduped rule indices, DOM-free.

  `cascade()` replaced 6 rule-iteration loops with 2
  candidate groups (UA + author). Candidates computed
  once per element, reused across 3 pseudo-target passes.
  Stores `std::size_t` rule indices (not `Rule*` pointers)
  -- preserves source-order tiebreaking for specificity.

  **5.6× cascade speedup, measured stable across 3 runs
  (Release, Wikipedia):**
  ```
  Run 1: PERF-CASCADE: 200.3ms
  Run 2: PERF-CASCADE: 188.7ms
  Run 3: PERF-CASCADE: 189.6ms
  ```
  Baseline was 1057.9ms (ADR-058). Saves ~870ms per page
  load. Cascade share of render time drops from 31% to
  6-7%.

  **Layout variance discovered during measurement**: layout
  (unchanged by ADR-059) showed 3× variance across the same
  3 runs (2415ms - 7156ms). Environmental noise on the 8GB
  test machine. **Methodology lesson for ADR-060**: layout
  perf requires median-of-N multi-run sampling, not single
  runs.

  5 new tests: bucket landings + multi-selector dedupe.
  Zero flips on existing ADR-022 threshold tests (`< 100`
  bound accommodates both pre- and post-indexing baselines).

  Test count: 680 cases, 2608 assertions (+5 cases /
  +22 assertions).

- **Eliminate string-copy overhead in layout hot path** (ADR-060): 
  `ComputedStyle::get_view(string_view)` returns
  `optional<string_view>` (read-only, no copy). Transparent
  `StringHash` / `StringEq` on the `declarations` map enable
  heterogeneous lookup (no key allocation).
  `fill_sides` rewritten to use direct `find` + reference
  access to `CascadeValue::value` -- eliminates 3 allocations
  per call × 12 calls per element × 5000 Wikipedia elements.

  **12.0% layout win, median-of-3 measured (Release, Wikipedia):**
  ```
  Pre:  PERF-LAYOUT: 2678ms (median)
  Post: PERF-LAYOUT: 2357ms (median of 2476/2328/2357)
  ```
  321ms saved per page load. Two of three post runs clustered
  at 2328-2357 -- structural, not variance. Cascade unchanged
  (~200ms) as expected.

  **Session-wide perf progression** (ADR-058 baseline →
  ADR-059 → ADR-060): 3437ms → 2950ms → ~2555ms = **25.5%
  total win across two perf cycles.** Cascade dropped 31%→7%
  of render time; layout dropped 12% structurally.

  Remaining layout cost (ADR-061 candidate): typed
  `ComputedStyle` fields to eliminate the residual
  `unordered_map::find` hash/compare (~25% potential,
  ~200 lines).

  3 new `get_view` tests. Zero existing test regressions.

  Test count: 683 cases, 2614 assertions (+3 cases / +6
  assertions).

- **Layout perf diagnostic — typed-field hypothesis disproven** (ADR-061): 
  **Decision record (no code shipped).**

  Post-ADR-060 re-profile (1135 samples, `perf record
  -g --call-graph dwarf`, Wikipedia HTML article) disproved
  the ADR-061 typed-`ComputedStyle`-fields hypothesis. The
  exact lookup typed fields would eliminate
  (`unordered_map<string, CascadeValue>::find`) is **0.25%
  of total self-time, not the ~25% projected** from ADR-060's
  pre-fix profile. ADR-060's copy elimination had already
  captured ~99% of the map-lookup cost by removing the
  string allocations around it.

  Three alternate targets individually disproven:
  - **String→string `unordered_map::find`** — traced to
    `Stylesheet::variables` (cascade-side, not layout; after
    ADR-059 cascade is ~7% of total)
  - **`parse_length` fast path** — not in top-10 self-time
    post-ADR-060
  - **`contains_word`** — real (5.43% self-time) but
    maximum possible win is 3-5% of total; deferred to a
    bundled cascade-perf cycle rather than a standalone
    polish ADR

  The post-ADR-060 profile is **flat**: no function exceeds
  ~5% self-time and the top-10 covers ~15% total. That's the
  correct shape after a successful optimization cycle — the
  big fish has been caught.

  **Perf stream conclusion** (ADR-058→059→060→061):
  - ADR-058: split-timer shipped, measured baseline
    cascade 1058ms / layout 2379ms
  - ADR-059: cascade index, **5.6× cascade win**
  - ADR-060: copy elimination, **12% layout win**
  - ADR-061: typed fields hypothesis **disproven by
    re-profile**
  - **Combined ADR-059+060: 25.5% total Wikipedia render
    win (3437ms → ~2555ms)**

  Methodology validated: measure → fix → re-measure before
  next cycle → ship the win or record the disproval. All
  three outcomes demonstrated in this stream. No cycle
  optimized without measuring.

  Test count unchanged at 683 cases, 2614 assertions
  (decision record only, zero code changes).

- **`linear-gradient()` background support** (ADR-062): 
  Parser preserves `linear-gradient(...)` as
  `background-image: <raw>`; layout parses into structured
  form (`background_is_gradient`, angle, stops); paint uses
  Cairo linear pattern per CSS Images 3 SS3.1.1 (direction
  `(sin θ, -cos θ)`, length `W·|sin θ| + H·|cos θ|`).

  **`rgb()` / `rgba()` support added to `parse_color`**
  (~50 lines): integer (0-255), percentage (0-100%), mixed,
  alpha via float or percent. Uncovered prerequisite
  (Wikipedia's gradient uses `rgba(255,255,255,0)`).

  **Synthetic-fixture-verified** (5 rows on
  `docs/adr062-gradient-test.html`): solid control, default
  vertical, 45° diagonal, 3-stop `to right`, Wikipedia's
  rgba form. All 5 render correctly.

  **Zero Wikipedia production delta** -- target is
  triple-blocked (pseudo-element, ancestor-culled parent,
  sticky deferred). Wikipedia screenshot identical pre/post.
  Same category as ADR-054's rotate.

  **Last 1/11 `background:` grammar gap closed.**
  11/11 forms parse; 10/11 render (linear-gradient parses +
  paints but target is ancestor-culled).

  Three bugs caught pre-shipping:
    * `parse_color` had no rgb/rgba (diagnostic catch)
    * `split_tokens` not in layout.cpp scope (build catch)
    * Angle math 90° rotated (fixture screenshot catch)

  1 test flipped, 4 new tests. Test count: 687 cases, 2627
  assertions (+4 cases / +13 assertions).

- **Scroll height regression fix** (ADR-064): `RenderPipeline::layout_height()`
  rewritten to walk the layout tree for the deepest Box's `y + height` instead
  of `layout_root_.height`. ADR-057's `html { height: 100% }` clamp was setting
  `<html>`'s box to viewport height (678px on Wikipedia) so `sync_drawing_area_size`
  set the DrawingArea to 678px despite 30,000+px of content -- ScrolledWindow saw
  no overflow, no scrollbar appeared.

  Fix: iterative Box-tree walk finds true content bottom. Verified via
  instrumented `SYNC-SIZE` fprintf (`layout_height=678` before, `~30000` after)
  plus production screenshots (scrollbar visible, wheel scroll works, bottom
  reachable).

  Regression intro: ADR-057 (`parent_content_height` + `html { height: 100% }`).
  Regression detector: user report mid-session. Fix effort: 3 compiler
  iterations, 25 lines of code.

  Unblocks ADR-063's deferred visual verification (float layout assessment on
  real Wikipedia content) -- see ADR-065 for the narrowing diagnostic.

  Test count unchanged at 687 / 2627.

- **Intrinsic width for auto-width floats** (ADR-066): float probe
  re-laid-out at `<img>` child width instead of parent content width. Includes
  HTML `width="..."` attribute fallback (Layer 1) and half-parent cap (Layer 2).

  Fixes ADR-065 diagnostic finding: Wikipedia figure floats measured at 953.6px
  (fills parent, no intrinsic width), collapsing following text to 22.4px
  (1-3 words per line). Now figure space is ~220-250px, text ~756px readable.

  Verification: production screenshots (2026-11-03) confirm readable text beside
  History section figure, HTML5 logo, HTML category diagram. Tim Berners-Lee
  JPEG still doesn't paint -- separate concern (ADR-067).

  ADR-063's deferred visual verification now closed -- float layout produces
  usable output at Wikipedia scale.

  4 new tests: 2 initial (auto-width > 700px, explicit 600-720px), 2 layers
  (HTML attr override, half-parent cap).

  Test count: 687 -> 691 cases, 2627 -> 2641 assertions.

- **Force Cairo CPU renderer** (ADR-068): `setenv("GSK_RENDERER", "cairo", 0)` in `main.cpp` before GTK init. GTK4's GSK GPU renderer crashes on very large cairo surfaces (empirically >~25,000px tall) during GPU upload -- corrupted clip state in `_cairo_clip_intersect_clip`. HoneyBee paints the full ~30,000px Wikipedia article into a single tall DrawingArea (ADR-021 / ADR-064), triggering the bug.

  Core dump backtrace showed zero HoneyBee frames from frame #11 onward -- pure GTK4/GSK internal. Cairo CPU renderer bypasses GPU texture upload and works cleanly for the same content.

  Production-verified 2026-11-03: no crash on Wikipedia scroll (was segfault, 213 MB core), smooth scroll performance, no visual regressions. `overwrite=0` preserves user-set GSK_RENDERER for opt-in GPU testing.

  Unblocks ADR-067 (JPEG verification needed a stable process).

  Test count unchanged at 691 / 2641.

- **JPEG decode via gdk-pixbuf** (ADR-067): JPEG magic-byte branch (3 bytes `FF D8 FF`, covers JFIF / EXIF / raw) added between PNG and SVG dispatch. Decode via `gdk_pixbuf_new_from_stream` + manual RGBA->ARGB32 pixel copy with R<->B channel swap. Keeps `honeybee_lib` GTK-free (gdk-pixbuf is transitively linked via GTKMM4, but GDK4 is not — only the executable links GTK4).

  Production-verified (fixture screenshot 2026-11-04): Tim Berners-Lee JPEG renders with correct colors (natural skin, blue tie), HTML5 logo PNG unchanged, Wikipedia wordmark SVG unchanged, 100px-scale JPEG correct size.

  Closes the last visible Wikipedia gap: Tim photo now renders in History section alongside the already-readable text (ADR-066) and working scroll (ADR-068).

  Layering finding: `gdk_cairo_surface_create_from_pixbuf` (GDK4) unavailable in `honeybee_lib` — manual pixel copy required to keep test binary GTK-free.

  Stub incident (honest): a `// DIAG-067: temporarily disabled` + `return nullptr;` from ADR-068's crash isolation was left in the JPEG branch. Chased two phantom causes (missing loader, missing symbol) before `grep 'DIAG-067'` located it. **Lesson**: `grep` for diagnostic markers after every stash/branch-switch cycle.

  Test count unchanged at 691 / 2641 (no new tests; fixture verification is the primary proof).

- **Viewport-clipped paint architecture** (ADR-069): DrawingArea is always viewport-sized (never ~30,000px). ScrolledWindow's child is now a VBox with [top_spacer(scroll_y), DrawingArea(viewport_h), bottom_spacer(content - scroll_y - viewport_h)]. On scroll adjustment change, spacers resize and DrawingArea repaints with cr->translate(0, -scroll_y) + visible range passed to paint.

  **Culling lesson** (the actual learning): first attempt skipped Box children whose layout y-bound was off-screen -- but a Box's y/height does NOT bound its descendants (absolute positioning, floats, overflow:visible). Wikipedia's infobox disappeared mid-scroll because visible children lived inside an off-screen-at-y-bound ancestor. **Fix:** leaf nodes (Text / InlineContext) are culled at the parent's dispatch; Box nodes are always recursed and gate only their own paint ops on box_on_screen.

  **Production-verified 2026-11-04**: Wikipedia renders correctly with culling (content visible at all scroll positions), scroll smooth, no crash, all image formats render, hover/click work. **GSK_RENDERER=gl also works now** -- the architectural payoff: ADR-068's Cairo workaround bypassed the crash, ADR-069 eliminates its root cause. ADR-068's setenv("GSK_RENDERER", "cairo", 0) retained as defense-in-depth.

  **Diagnostic chain**: iteration 1 = culling bug; iteration 2 = fprintf proved tetris math correct (top+draw+bot=h_px at every tick); iteration 3 = HONEYBEE_NO_CULL=1 confirmed culling was the culprit.

  Test count unchanged at 691 / 2641.

- **Inline Routing & Culling Regression Fix** (ADR-071): Fixed Wikipedia section headings disappearing and blank-page-on-scroll.
  - **Inline Routing:** `layout_block_children` now routes `display: inline` children to `inline_run`. `flush_inline_run()` is called at the end of the loop to catch trailing inline elements (like `<h2>`).
  - **Culling:** Restored ADR-069 rule that *only leaf nodes* (Text/InlineContext) are culled. `Box` nodes are always recursed. This fixes `<body>` (clamped to 641px) being culled when scrolling to `y=1045px`, which dropped the entire 30440px article body.
  - **Viewport Guard:** `on_draw` skips layout if `viewport_h == 0` to prevent initial collapse.
  - **Async Logging:** Upgraded ADR-046 logging to a background thread (non-blocking). Added `LayoutDetail` and `PaintDetail` categories. Hooked `log_shutdown()` into `main.cpp`.

  **Production-verified 2026-11-08:** History, Markup, and HTML5 headings are visible. Scrolling is completely smooth with no blank pages.

- **ADR-071 regression tests** (ADR-072): Added 3 unit tests that fail if the ADR-071 fixes are reverted. Without these, future refactors of `layout_block_children` or `paint_children` could silently reintroduce the Wikipedia headings-invisible and blank-page-on-scroll bugs.

  Tests:
  - `test_layout.cpp`: inline `<h2>` with `display:inline` becomes `InlineContext`, not a `LayoutBox` at `y=0` (guards the inline-routing fix).
  - `test_paint.cpp`: `Box` parent with `height=641px` containing descendants at `y=1500px` is not culled by its own height (guards the leaf-only culling rule; reproduces the `<body>`=641 / content=30440 Wikipedia case).
  - `test_paint.cpp`: `Text` leaf far outside visible range is still culled (guards the perf property — if a future refactor disables culling entirely, this test fails).

  Suite: 691 → 694 test cases, 2641 → 2646 assertions. All green.

  **Documented gap:** these tests catch structural regressions, not visual ones. Wikipedia-scale visual fidelity remains a manual verification step per ADR.

- **Permanent diagnostic tracing** (ADR-073): `HB_LOG` traces are now permanent infrastructure — never removed, only re-classified or reformatted. Guard pattern established: expensive argument construction (std::string, get_attribute, c_str on temporary) wrapped in explicit `log_enabled()` check, so traces are genuinely zero-cost when disabled.

  13 permanent trace points across 4 files:
  - LayoutDetail: `layout_element enter/EXIT`, `block_children child`, `InlineContext created`
  - PaintDetail: `paint_children`, `CULLED Text/Inline`, `painting InlineContext`
  - Paint: `LINE`, `FRAG`, `SCROLL`, `PAINT START`
  - Layout: `on_draw`, `sync`

  Two-tier category scheme: normal (`Layout`, `Paint`, etc.) for coarse-grained debug; detail (`LayoutDetail`, `PaintDetail`) for per-element granularity. Enable via `HONEYBEE_LOG=layout,paint` or `HONEYBEE_LOG=layout_detail,paint_detail HONEYBEE_LOG_LEVEL=trace`.

  **Diagnostic-only iteration ends.** Future bug hunts use existing traces instead of adding new ones.

  **Also discovers (but defers):** Wikipedia `<h2>History` is placed at `y=2210` (default) instead of `y≈500`. Root cause is `float: right` infobox not being wrapped by intro paragraphs — a `layout_block_children` bug in ADR-031 float handling. Deferred to ADR-074.

  Test count unchanged at 694 / 2646 (traces are guarded, no behavior change).

### Added

- **Box model completion** (ADR-039 Commit 1, delivered):
  `height` now applied to all boxes (was `<img>`-only) —
  closes ADR-037's deferred height gap (search icon
  `w=20 h=0` → `w=20 h=20`, verified via tree walk + PNG).
  `min-width` / `max-width` / `min-height` / `max-height`
  clamps applied after resolution (vw/vh in value skips the
  clamp, ADR-037 defers those units; percentage heights also
  skipped pending `parent_content_height` threading).
  `overflow: hidden` sets `LayoutBox::clip_children`; paint
  wraps children in Cairo clip. Removed duplicated ADR-035
  img sizing block (dead code). **Three honest corrections
  in the ADR's record**: (1) the "185px gap" claim in
  Commit 1's message was measured from a stale pre-fix PNG;
  actual state has title y=128 (better than baseline y=145),
  no gap; (2) `box-sizing: border-box` shipped in Commit 2
  then reverted in #201 after merged render showed visual
  regression (gray strip, icon shift) — process failure (no
  PNG inspection before merge), root cause unmeasured,
  re-attempt deferred with proper discipline; (3) `border`
  shorthand not expanded by ADR-012, surfaces as pre-existing
  limitation (Wikipedia borders currently invisible),
  deferred to small future ADR.
- **14 pseudo-classes** (ADR-038): the selector engine's
  pseudo-class support grew from 4 (`:link`/`:visited`/`:hover`/
  `:root`) to 18. **Structural (10)**: `:first-child`,
  `:last-child`, `:only-child`, `:empty`, `:first-of-type`,
  `:last-of-type`, `:only-of-type`, plus the full `:nth-child(An+B)`
  family (odd/even/N/An/An+B/-An+B grammar). **Logical (3)**:
  `:not()`, `:is()`, `:where()` with compound-arg comma lists
  and CSS4 specificity (`:where()` = `0,0,0`; `:not()` / `:is()` =
  max of args). **Language (1)**: `:lang(tag)` with ancestor-chain
  walk and BCP-47 case-insensitive prefix match. On Wikipedia,
  ~840 previously-dropped rules now participate in the cascade
  (`:lang` 410, `:not` 374, `:where` 16, `:is` 2, structural 38).
  Scope expanded from original "structural only" to cover the
  three categories with measurable usage (structural alone would
  unlock only ~38 rules; `:nth-child()` in particular has zero
  uses in the target CSS). Spec's 3-commit plan converged to one
  integrated commit (documented in Implementation section).
  Deferred to ADR-039+: `:has()`, `:dir()`, form-state pseudos
  (`:focus`, `:active`, `:checked`, `:enabled`, `:disabled`),
  combinator args inside `:not()`/`:is()`/`:where()`.
- **CSS value functions** (ADR-037): `calc()`, `min()`,
  `max()`, `clamp()` now evaluate. Recursive-descent parser
  with standard precedence, nested functions, unary negation,
  mixed-unit arithmetic (em + px, % + px, rem + px) via
  convert-to-px-at-parse-time. Units: px/em/rem/%/pt (vh/vw
  deferred). `var()` resolution extended from whole-value-only
  to **any embedded position** (calc, composite values,
  nested) — fixes ADR-026's deferred limitation and unblocks
  `calc(var(--size) + 4px)`. **Partial visible delta**:
  Wikipedia search icon width 2 -> 20 (correct), height 0
  (unchanged — `height` on non-img boxes still unapplied,
  deferred to ADR-040 box model). First spec-driven ADR after
  the pivot from "fix Wikipedia" to "implement missing CSS
  spec features."
- **`clear` property** (ADR-036): floats with `clear:right |
  left | both` now jump below prior same-direction floats
  instead of overlapping at the same edge; non-float blocks
  with `clear` jump below active floats. Zero visible delta
  on current pages (byte-identical PNG, layout height
  unchanged at 62295), but defensively correct for articles
  with consecutive same-side thumbnails and heading-with-
  `clear:both` section patterns. **Fifth misdiagnosis in the
  arc's correction record**: ADR-036's original spec
  hypothesized attribute selectors and floats were broken;
  diagnostic proved both already worked (attribute selectors
  since ADR-008, floats since ADR-031). The amendment (#188)
  and Implementation section preserve the correction
  alongside the original claim. **Nav overlap at x=324**
  (visible since ADR-029) identified as a separate,
  currently-unidentified element, deferred to its own
  diagnostic cycle.
- **Layout-integrated `<img>`** (ADR-035): `<img>` elements now
  reserve layout space from intrinsic dimensions via a new
  `ImageDimLookup` callback threaded through `layout()` (default
  `nullptr`, zero churn for existing tests). Explicit CSS width
  or height wins; the other axis is aspect-derived. Neither set
  → intrinsic dims (CSS spec fallback). Cache hit = zero cost;
  first render pays the 8 sync fetches at layout time (~10%
  slower first paint, cached after). **Wikipedia wordmark
  (248x39) and tagline (248x19) render correctly in the header**,
  header grows from ~50px to ~94px to fit both, tagline unhides
  (paint-layer workaround from ADR-034 removed — zero
  workarounds left in `paint.cpp`). Footer SVGs render at
  intrinsic (200x200, 100x100). Below-fold thumbnail heights are
  in the tree but don't propagate down (float v1 limitation,
  ADR-036 fixes). JPEG decode still fails (deferred since
  ADR-034). Three honest findings recorded: thumbnail
  propagation through float containers, pre-existing
  navbox/sidebar class token mismatch, JPEG decode dependency.
- **First image rendering** (ADR-034): `<img>` elements now
  paint. PNG via Cairo (`cairo_image_surface_create_from_png_stream`),
  SVG via librsvg (`rsvg_handle_new_from_data` +
  `rsvg_handle_render_document`), format-dispatched on magic
  bytes. Decoded surfaces cached per URL, cleared on page load.
  **The Wikipedia wordmark renders as the first `<img>` in
  HoneyBee's history**, at the top-left of the header, using
  viewBox aspect preservation for the 25th-anniversary
  path-only SVG asset. `background-image` url() also extracted
  and painted (search icon infrastructure works, deferred due
  to `calc()` size). `position: absolute` completed from
  ADR-033. Two SVG bugs found and documented: intrinsic size
  is 1x1 (fixed via viewBox fallback), protocol-relative URLs
  (`//host/path`) resolve incorrectly (deferred). Tagline hidden
  as documented workaround until ADR-035 integrates image
  layout.
- **Position layout: relative + fixed** (ADR-033): `Position`
  enum on `LayoutBox` (Static / Relative / Absolute / Fixed /
  Sticky). `layout_element` parses `position` from ComputedStyle.
  `layout_block_children` honors `position: fixed` -- out-of-flow,
  viewport-anchored at `(top, left)`. `relative` is a no-op (v1;
  context-only declarations in Wikipedia's CSS have zero offsets).
  `absolute` / `sticky` are stored but treated as `static` in v1;
  proper handling in ADR-034 (containing-block tracking).
  **Zero visible delta on Wikipedia at desktop 1774** --
  PNG byte-identical to pre-ADR-033, exactly as the spec
  predicted. Structural prep for ADR-034 (absolute) and future
  `z-index`.
- **Visibility + opacity paint culling** (ADR-032): `LayoutBox`
  gains a `visible` flag set during layout from `visibility:
  hidden | collapse` or `opacity: 0`. `paint_box()` early-returns
  when false. Paint is now authoritative about visibility --
  defense-in-depth against paint drawing a subtree that layout
  skipped. Structural precondition for future `:checked` dropdown
  support. Also disproved ADR-031's apparent "paint overlap"
  (diagnostic showed only legitimate article content at x<500,
  y<900; the perceived overlap was the HTML article's intro
  paragraphs mentioning HTML5/XHTML/WHATWG, misread as nav-style
  content). See ADR-031's Post-ADR-032 correction.
- **Feature-class flip table + skip-link hide** (ADR-030): extends
  ADR-029's single-flip helper into a `constexpr` table (entries
  for `main-menu-pinned` and `page-tools-pinned`) and injects
  `.mw-jump-link{display:none !important}` into the desktop viewport
  CSS. On Wikipedia this collapses the Tools panel (687px to 21px),
  moves the article intro from y=834 to y=145, and hides the "Jump
  to content" skip link. Combined with ADR-029's `#p-lang-btn`
  workaround, the first viewport now shows the compact header,
  article title "HTML" at y=84, the Article/Talk tabs with Read/View
  source/View history inline, and the intro paragraph. Principled
  fix for the skip link (`.mw-jump-link:not(:focus)` + `position:
  absolute` + `clip`) and for click-to-open dropdowns (`:checked`
  + `position: absolute`) is ADR-031.
- **Flex layout** (ADR-029): `display: flex` and `display: inline-flex`
  now parse and lay out with a single-line flex algorithm --
  `flex-direction` (row | column), `justify-content`, `align-items`,
  `flex-grow` / `flex-shrink` / `flex-basis`, and `gap` /
  `row-gap` / `column-gap`. `flex` and `flex-flow` shorthands
  expand at parse time (matching ADR-012 / ADR-028 conventions).
- **Viewport feature-class flip** (ADR-029): `RenderPipeline` now
  rewrites `vector-feature-main-menu-pinned-disabled` to
  `-enabled` on `<html>` at viewports >= 1120, mimicking what
  Wikipedia's inline JS does at page load. Without this flip,
  Wikipedia's `.vector-main-menu-landmark` renders at 711px
  (mobile hamburger menu stacked as blocks), pushing the entire
  grid container down by 165px. With the flip, the header
  collapses from 727px to 36px and the article content sits
  above the fold for the first time.
- **Language dropdown hidden at desktop** (ADR-029 follow-up): `#p-lang-btn`
  (the "In other languages" 140-item list) is a `:checked` /
  `position: absolute` dropdown in real Wikipedia, visible only on
  hover/click. Without those features, its 140 `<li>` children render
  as visible blocks consuming 4766px in the article column, pushing
  the article body below the fold. `RenderPipeline::relayout()`
  injects `#p-lang-btn{display:none}` at viewports >= 1120, matching
  the visible behavior of real Wikipedia with JS. Principled fix is
  ADR-030 (`position: absolute` + `:checked`).
- **State pseudo-classes** (ADR-018): `:link`, `:visited`, and `:hover`
  now parse, match, and render. `<a href>` links render blue when
  unvisited, purple when visited, and lighter blue when hovered --
  matching the behavior of every mainstream browser since the mid-1990s.
- **Click navigation** (ADR-018): clicking a rendered `<a href>` now
  navigates to it. Before this, links were styled but inert and the
  only way to load a page was typing in the URL bar.
- **`honeybee::resolve_url`** in `base/url.{hpp,cpp}` -- one canonical
  implementation used by both the CSS matcher (to decide whether a
  link's target is visited) and the browser layer (to fetch on click),
  guaranteeing the string marked visited is byte-identical to the
  string fetched.
- **`renderer::layout::hit_test`** -- maps mouse coordinates to the
  deepest DOM element under the point, walking the layout tree and
  inspecting `LineFragment::element` for flattened inline text.
- **`LineFragment::element`** -- carries source-element identity
  through inline layout so `<a>` inside `<p>` remains identifiable
  for hit-testing and future per-element features (text selection,
  tooltips).
- **`RenderState`** threaded through the cascade -- `visited_urls`,
  `hovered_element`, `base_url` all travel with `cascade()` so the
  selector matcher can consult them.
- `tests/fixtures/` not yet introduced; manual test HTML files
  currently live in `docs/` (see `docs/README.md`).

- **Back and forward navigation** (ADR-019): toolbar `<-` and `->`
  buttons are now functional. History is session-scoped: every
  successful navigation pushes a URL, Back/Forward move a cursor
  through the list, and a new navigation truncates any forward
  history (the branching case). Reload does not create history
  entries -- pressing it N times still requires one Back to leave.
- **`honeybee::browser::NavigationHistory`** in
  `src/browser/navigation_history.hpp` -- header-only history
  stack with `push` / `back` / `forward` / `can_back` /
  `can_forward`. No GTK dependency; unit-tested.

- **Page caching for Back/Forward** (ADR-020): the browser now
  caches HTML per URL in a session-scoped in-memory store.
  Back/Forward serve from cache without a network round-trip;
  the status bar shows "من الذاكرة" (from memory) to distinguish
  cache hits from fetches. New navigations and Reload always
  fetch fresh -- Reload additionally refreshes the cache entry.
- **`honeybee::browser::PageCache`** in
  `src/browser/page_cache.hpp` -- header-only URL -> HTML map
  with O(1) `bytes()` via an incrementally-maintained counter.
  No GTK dependency; unit-tested.

- **Large-page scrolling** (ADR-021): the view area is wrapped
  in `Gtk::ScrolledWindow`; content taller than the viewport is
  scrollable with the mouse wheel, trackpad, scrollbar, and
  Page Up/Down. The `DrawingArea`'s size request grows to the
  full layout height (44,000 CSS pixels for a Wikipedia article),
  letting ScrolledWindow manage the scroll naturally.
- **Incremental restyle for hover** (ADR-021): hover changes
  re-run only the cascade and update paint colors in place,
  skipping layout entirely. Hover latency on Wikipedia's 800KB
  article drops from ~2-3s (previous full-relayout path) to
  ~700ms. Still not sub-frame on very large pages -- a followup
  ADR (targeted cascade, ADR-022) is proposed to reach that.

- **Targeted cascade for hover** (ADR-022): hover changes
  re-cascade only the elements whose state changed, keeping the
  rest of the ComputedStyleMap cached. Measured on Wikipedia's
  799KB article (debug build): link hovers drop from ~700ms
  (ADR-021) to **14-25ms** (~30-50x improvement); medium element
  hovers to 55-67ms. Hover on large structural elements remains
  ~900ms (~6% of observed transitions) -- same cost as before,
  because their subtrees get re-cascaded entirely. A followup
  ADR (ADR-023, skip-subtree optimization) is proposed to close
  that gap.

- **Skip-subtree optimization for hover** (ADR-023): wrapper-element hovers
  now cascade only hover-sensitive descendants, not every element
  in the subtree. Measured on Wikipedia (debug build): structural-
  element hovers drop from ~900ms (ADR-022) to **~400ms** (~2.2x
  faster). Level detection works correctly (all 63 observed
  transitions were Level 1 -- UA `a:hover` only). Link hovers
  unchanged at ~20ms. A followup ADR (ADR-024, hover-match
  transition check) is proposed to close the remaining gap --
  the ADR-023 spec predicted ~20ms for wrapper hovers; actual is
  ~400ms because real pages have ~2,500 `<a>` per wrapper div,
  not the ~10 in the synthetic test. Same scale-estimate failure
  mode as ADR-021 -> ADR-022's prediction miss.

- **Hover-match transition check** (ADR-024): `restyle()` now cascades
  only elements whose `:hover` match state actually changed
  between old and new hovered element. Measured on Wikipedia
  (debug build): wrapper-element hovers drop from ~400ms (ADR-023)
  to **~8ms** (~50x). Link hovers unchanged at ~18ms. Cumulative
  across ADR-022 -> 023 -> 024: wrapper hover 900ms -> 8ms (~120x).
  The measured result beat ADR-024's ~25ms prediction by ~3x,
  because the pre-existing `did_work` early-return skips propagate
  and restyle_walk when zero elements cascade. Documented as
  Meta-lesson-v2: predictions need a complete code-path model,
  not just component measurements.

- **External CSS loading** (ADR-025): external stylesheets
  (`<link rel="stylesheet">`) are now fetched, parsed, and
  applied alongside inline `<style>` CSS. Verified visually on
  Wikipedia's HTML article: article-body typography, heading
  hierarchy, inline link styling, citation markers, and
  references formatting are all transformed; sidebar link colors
  partially improved. Sidebar/header/TOC **layout** remains
  UA-default (blocked by `@media` and `:root`; ADR-026/027 close
  those gaps). Page cache now stores `(html, external_css)` per
  URL, so Back/Forward re-render without re-fetching.

  Notable finding: 27% parser acceptance translated to ~40-50%
  visual improvement (higher than expected). The parseable rules
  concentrated in content-typography; the unparseable 73% is
  dominated by layout rules. Acceptance rates alone don't predict
  user-visible impact -- visual verification is what revealed the
  concentration.

  Deferred (three-ADR continuation): ADR-026 (`:root` + `var()`),
  ADR-027 (`@media`), ADR-028 (`:not()` + attribute operators).

- **CSS variable resolution** (ADR-026): `:root` selector
  support + parse-time `var(--x[, fallback])` resolution. On
  Wikipedia's CSS: 542 `var()` declarations; 341 are whole-value
  (299 of those target properties our layout engine consumes);
  194 are partial-value (left unchanged, deferred). The 299
  consumed-property resolutions are real and cascade-integrated,
  but **visible delta over ADR-025 is negligible** -- the rules
  containing these declarations either lose the cascade on
  specificity, are gated by `@media` (ADR-027), or produce
  values visually similar to what ADR-025 already applies.

  Meta-lesson-v3 documented: "consumed-property coverage is
  necessary, not sufficient, for visible change. The rule
  containing the resolved declaration must also win the cascade
  after parsing." Same shape as ADR-023/024's earlier Meta-
  lessons.

  Also fixes a pre-existing `display: none` gap in
  `flatten_inline_to_words`: `<style>` tags nested inside inline
  elements were rendering their CSS text as visible article
  content (Wikipedia has 18 such tags in `<body>`, several
  inline-nested). Cascade correctly computed `display: none`;
  the inline-flattening recursion simply wasn't checking it.
  Surfaced by ADR-026's visual verification. One-line fix +
  regression test.

- **@media block support** (ADR-027): @media blocks are
  now parsed (with simple `calc(Npx ± Mpx)` support) and
  cascade-filtered by viewport. At viewport 1024, 33 of
  Wikipedia's 64 @media blocks fire (52%) -- including the
  `max-width: calc(1120px - 1px)` tablet-layout blocks that
  calc support specifically unlocks. Unsupported queries
  (`print`, `prefers-*`, `aspect-ratio`, `orientation`,
  `resolution`, `hover`, `pointer`, range syntax) drop their
  rules entirely, per CSS spec.

  **Visible delta over ADR-026: partial.** Wikipedia's bottom
  navbox now renders with structured section headers and
  colored backgrounds -- @media `background-color` declarations
  winning cascade and consumed by paint.

  **Not delivered: sidebar / header / TOC layout transform.**
  The 106 layout-property declarations inside @media
  (`grid-template-columns`, `flex-*`, `column-*`, `float`,
  `position`) enter the cascade correctly but our layout
  engine ignores those properties (ADR-011 scoped layout to
  block + inline flow). The structural transform requires a
  future layout-engine ADR.

  **Meta-lesson-v3 refinement**: predicting visible delta from
  any CSS subsystem requires tracing three paths -- does it
  parse, does it win the cascade, does its winning property get
  consumed by paint/layout? ADR-027 measured all three, so the
  prediction was closer than ADR-025/026's.

- **CSS grid layout** (ADR-028): `display: grid`
  now parses and lays out. Fixed, `fr`, and `minmax()` track
  sizing; named grid areas via `grid-template-areas` and
  per-child `grid-area`; `column-gap`. On Wikipedia at viewports
  >= 1120px: sidebar becomes a ~325px left column, article
  content indents to the right. Article's vertical position
  within its cell is off by ~660px (deferred).

  **Hidden deliverable**: fixing a root-relative URL resolution
  bug (`base/url.cpp`) that had been silently 404ing every
  external CSS fetch since ADR-025. Post-fix, ADR-025's
  external CSS loading, ADR-026's var() resolution, and
  ADR-027's @media rules all reach the cascade on Wikipedia
  for the first time. Their measured deltas were inline-only;
  post-fix deltas are larger.

### Fixed

- **Use-after-free segfault on click-navigation** (ADR-018): after
  clicking a link while hovering over it, the browser crashed when
  the new page finished loading. Root cause: `RenderState::hovered_element`
  pointed into the previous DOM; `RenderPipeline::render()` replaced
  `doc_` without clearing the pointer, and the next cascade's
  `PseudoHover` matcher dereferenced freed memory. Fixed at the source
  in `render()`, where `doc_` is the only place replaced.

### Changed

- UA stylesheet rule `a { color: #0000EE }` replaced by three
  per-pseudo-class rules (`a:link`, `a:visited`, `a:hover`). Side
  effect: `<a>` without an `href` no longer renders blue (correct --
  it is not a link).
- `resolve_url` relocated from `selectors.cpp`'s anonymous namespace
  to `base/url.cpp` so both the CSS layer and the browser layer share
  one implementation.

- Back/Forward buttons start disabled (history empty) and
  transition to enabled/disabled as the history cursor moves.
  Previously both were enabled but non-functional (stubs that
  printed "غير مُفعَّل بعد" to the status bar).
- Reload button now calls `navigate_to(url, NavKind::Reload)`
  rather than re-dispatching through `on_url_activated()`, so
  history is preserved.

- Back/Forward are effectively instantaneous in normal use.
  On localhost the improvement is invisible (a fetch was
  already milliseconds), but on any real network the difference
  between a round-trip and a re-parse is significant.
- Session integrity: killing the HTTP server mid-session
  leaves Back/Forward functional for any page already visited.
  Reload and New correctly fail (they must fetch).

- `render_pipeline.cpp` and `pango_text.cpp` moved from the
  `honeybee` executable target to `honeybee_lib`. Both are
  GTK-free and testable; the move unblocks unit testing of
  `RenderPipeline` (needed by the new restyle dimension-invariant
  test). `PANGOCAIRO` pkg-config added to top-level dependencies.
- Hover on Wikipedia-scale pages is responsive within ~700ms
  (previously 2-3s freeze). See "Large-page scrolling + hover"
  under Added; targeted cascade (ADR-022) will address the
  remaining latency.

- Hover latency on large pages: link hovers are now ~20ms
  (median) instead of ~700ms. Structural-element hovers are
  unchanged at ~900ms and documented as a known limitation
  (ADR-023). User-visible: hovering links on Wikipedia feels
  instant; hovering a large wrapper element still lags.

- Hover on Wikipedia-scale pages: link hovers unchanged (~20ms).
  Structural-element hovers improved from ~900ms to ~400ms (2.2x),
  but short of the ~20ms ADR-023 predicted. Root cause: real pages
  have far more `<a>` descendants per wrapper div than the synthetic
  test document assumed. ADR-024 proposes the transition-check fix.

- Hover performance final state: link hovers ~18ms, wrapper-element
  hovers ~8ms on Wikipedia-scale pages. Full hover responsiveness
  is now sub-perceptual on all element types, closing the thread
  ADR-021 opened. The three-ADR arc (022 -> 023 -> 024) went from
  ~900ms to ~8ms on wrapper hovers (~120x total).

## [0.4.0]

The baseline release at the time this changelog was created. Prior
history lives in the git log and in `docs/decisions/` (ADRs 001-017
implemented before this file existed).

