# ADR-025: External CSS Loading

**Status:** Accepted
**Date:** 2026-10-25
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-009 (CSS parser), ADR-014 (UA stylesheet), ADR-018 (pseudo-classes), ADR-020 (page cache), ADR-021 (restyle), ADR-024 (transition check)

## Context

HoneyBee renders real-world pages with UA stylesheet defaults plus
any inline `<style>` CSS the page contains. External stylesheets --
delivered via `<link rel="stylesheet" href="...">` -- are completely
ignored. This is the single largest gap between "HoneyBee renders
HTML structure" and "HoneyBee renders what a browser would render."

### Measured on Wikipedia's HTML article (2026-10-25)

**Fetch input**:
- HTML: 799,547 bytes
- 2 `<link rel="stylesheet">` tags
- External CSS: 222,927 bytes total (216,046 + 6,881)
- ~1,443 CSS rules

**Parser acceptance**: 27.0% (390 of 1,443 rules).

**What the 73% rejection consists of**:

| Blocker | Rules blocked | Parser gap |
|---|---|---|
| `:not(...)` | 391 | Functional pseudo-class |
| `:lang(...)` | 410 | Functional pseudo-class |
| `::before`/`::after` | 98 | Pseudo-element render pipeline |
| `@media` blocks | 72 | Viewport query evaluation |
| `@supports` blocks | 73 | Feature query evaluation |
| `[~=]`/`[^=]`/`[$=]`/`[*=]` | ~150 | Attribute operators beyond `=` |

**What the 27% acceptance is worth in practice**: of the 390
accepted rules, **255 use `var()` in their declarations** (CSS
variables -- unresolved in our implementation, so their declarations
become no-ops). Only ~135 rules have both a parseable selector AND
declarations that resolve to something visible.

**Skeleton coverage** (does external CSS reach the structural
classes?):

- **FOUND** (would apply): `.mw-parser-output` internals (references,
  tables), `.infobox` internals, `body` partial.
- **MISSING** (would not apply): `.vector-menu`, `.vector-body`,
  `.vector-header`, `.vector-toc`, `#bodyContent`, `#toc`, `.navbox`.

**Conclusion**: external CSS at 27% acceptance improves article
content styling (references, infobox internals, code blocks). It
does NOT deliver "Wikipedia looks like Wikipedia" -- the sidebar,
header, TOC, and overall layout remain unstyled. Those require
`:root`+`var()`, `@media`, and `:not()` support, which become
ADR-026 through ADR-028.

**Entity-decoding gotcha**: Wikipedia's `<link href>` values are
HTML-entity-encoded (`&amp;` rather than `&`). Fetching with the
raw entity-encoded URL returned a 196-byte error response; fetching
with the decoded URL returned 216KB of CSS. Any external-CSS
pipeline MUST verify attribute-entity handling.

## Decision

Load external CSS and merge it with inline `<style>` CSS before
parsing.

### Extraction: `collect_css_sources`

New helper in `renderer/html/css_sources.hpp/cpp`:

```cpp
struct CssSource {
    bool inline_style;
    std::string content;  // <style>: text; <link>: href
};

std::vector<CssSource> collect_css_sources(const dom::Document& doc);
```

Depth-first DOM walk returning `<style>` and `<link rel="stylesheet">`
in **document order**. Skips:
- `<link rel="alternate stylesheet">` -- would require UI to activate
- `<link rel="preload" as="style">` -- we don't preload

**Entity decoding**: `get_attribute("href")` from lexbor likely
returns the decoded value. Verify during implementation; if not,
add explicit HTML-entity decode in `collect_css_sources`.

### Fetch: sequential, in the navigation callback

In `HoneyBeeWindow`'s `navigate_to` callback (main thread), after
HTML arrives:

```cpp
auto doc = renderer::html::parse(r.body);
auto sources = collect_css_sources(*doc);

std::string merged_css;
auto deadline = steady_clock::now() + 5s;
int fetched = 0;
for (const auto& src : sources) {
    if (src.inline_style) {
        merged_css += src.content + "\n";
        continue;
    }
    if (fetched >= 10 || steady_clock::now() > deadline) continue;
    std::string css_url = honeybee::resolve_url(src.content, url);
    auto css_response = honeybee::net::fetch(css_url);
    if (css_response.success()) {
        merged_css += css_response.body + "\n";
        ++fetched;
    }
    // failure: skip, continue with what we have
}

m_pipeline.render_with_css(std::move(doc), merged_css, width);
```

**Fetch strategy is synchronous** -- blocks the main thread.
Rationale in Design decisions.

### Pipeline API

`RenderPipeline::render_with_css(std::unique_ptr<Document> doc,
                                  std::string merged_css, float width)`.

The existing `render(html, width)` signature is preserved for tests
that don't exercise external CSS; it delegates to
`render_with_css(parse(html), "", width)`.

`relayout()` uses `merged_css_` instead of `extract_inline_css(*doc_)`
-- the CSS was already merged by the window (inline + external).

### Cache extension

ADR-020's `PageCache` (`unordered_map<string, string>`, URL -> HTML)
extends to:

```cpp
struct CacheEntry { std::string html; std::string external_css; };
std::unordered_map<std::string, CacheEntry>
```

Back/Forward re-renders use `(html, external_css)` from cache --
no re-fetch. Reload re-fetches (invalidates entry).

### Failure handling

- **CSS fetch fails** (network, timeout, 404): skip, continue with
  remaining CSS. If all fail, fall back to inline-only
  (pre-ADR-025 behavior).
- **CSS parse produces zero rules**: no visible change, no crash.
- **Caps exceeded** (10 stylesheets, 5s total): stop fetching, use
  what loaded.

## Design decisions

### Why synchronous fetch, not async

Async would require chaining a second `AsyncFetcher` after HTML,
restructuring `navigate_to`'s callback, and handling partial-failure
across parallel fetches. ~80 extra lines of async plumbing for v1.

Sync fetch blocks the main thread. But `render_html` already blocks
1-3s on Wikipedia -- a further 200-500ms CSS fetch is in the same
UX tier ("frozen during navigation"). Combined cap of 5s prevents
pathological freezes.

Deferred: async CSS fetch as future ADR if UX becomes unacceptable.

### Why cache HTML + CSS together

ADR-020's promise: Back/Forward are instant. If Back re-fetches CSS
on every navigation, that breaks the promise. Cache both, re-render
from cache. Cache extension is minimal (struct replaces string).
Reload explicitly re-fetches CSS (bypasses cache).

### Why merge into one string, not parse multi-sheet

CSS cascade treats multiple `<link rel="stylesheet">` + inline
`<style>` as one logical sequence -- source order determines
precedence. Concatenation (in document order) preserves that
semantics exactly. Multi-sheet would require parallel `Stylesheet`
handling, `cascade()` accepting multiple sheets, and tests for
cross-sheet specificity. All functionally equivalent for v1;
concatenation is ~15 lines vs. ~40.

Future ADR (media-query-scoped sheets) will need multi-sheet
support. Deferred.

### Why not fetch external CSS in the pipeline

The pipeline (`RenderPipeline`) is currently pure-transform: HTML in,
layout tree out. No network I/O. Keeping it that way simplifies
testing (no mock server) and matches ADR-020's caching model (cache
sits outside the pipeline). Fetch lives in `window.cpp`, pipeline
receives pre-fetched CSS.

### Why fail gracefully, not abort

A page with 5 stylesheets where 4 load and 1 fails should still
render -- 4 sheets of styles is better than a blank page. Strict-fail
would be user-hostile.

## Deferred

- **`:root` + `var()`** -- 255 accepted rules become visible once
  CSS variables resolve (ADR-026)
- **`@media` blocks** -- 72 blocks of rules inactive; likely wraps
  the Vector skin layout rules (ADR-027)
- **`:not()`** -- 391 rules inactive (ADR-028)
- **Attribute operators** `~=`, `^=`, `$=`, `*=` -- ~150 rules
  inactive (ADR-028)
- **`::before`/`::after`** -- 98 rules inactive; requires
  pseudo-element render pipeline (larger ADR)
- **`@supports` blocks** -- 73 blocks inactive; low priority
- **`@import`** -- requires recursive fetch + cycle detection; rare
  in modern CSS; deferred indefinitely
- **Async CSS fetch** -- if the sync block becomes a UX problem
- **CORS** -- most `<link>` tags don't require CORS; if a stylesheet
  fails due to CORS, treat as fetch failure (deferred)
- **Media-type filtering** -- `<link rel="stylesheet" media="print">`
  currently loads; should be filtered (small fix, deferred)

## Consequences

**Better:**
- External CSS from real-world pages is loaded, parsed, and applied.
- Article-content styling improves measurably on Wikipedia:
  references, infobox internals, code blocks.
- Fetch pipeline is now present; subsequent ADRs (026-028) each
  unlock a bigger slice of visual fidelity without any further
  infrastructure work.

**Accepted limitations:**
- Structural styling (sidebar, header, TOC, article layout) remains
  UA-default because the rules are gated by `@media`, `var()`,
  `:not()`. Visible delta is honest but partial -- concrete
  sub-checks measured in Commit 2, documented in Commit 3.
- Synchronous fetch adds 200-500ms to navigation on pages with
  external CSS.
- Max 10 stylesheets / 5s total caps may drop late stylesheets on
  pathological pages.

**Unchanged:**
- Cascade semantics, layout, paint.
- ADR-018/019/020/021/022/023/024 behavior.
- Test suite grows by the new unit tests.

## Implementation plan

Three commits on `feat/external-css-loading`:

1. **`collect_css_sources` + `PageCache` extension** -- helper,
   tests, `CacheEntry` shape. Independent verification.
2. **`RenderPipeline::render_with_css` + window integration** --
   pipeline API, sync fetch loop in callback, page-cache roundtrip.
   Commit message includes the measured acceptance rate on merged
   CSS + visual sub-checks (which effects landed).
3. **Docs** -- ADR-025 status -> Implemented, README + CHANGELOG
   with measured delta in Implementation section.

## Verification

**Unit tests**:

- `collect_css_sources`: order preservation across
  `<link><style><link>` mix
- `collect_css_sources`: `<link rel="alternate stylesheet">` and
  `<link rel="preload">` skipped
- `collect_css_sources`: empty doc returns empty
- `render_with_css`: external CSS appears in computed styles (a
  rule like `.foo { color: rgb(1, 2, 3) }` reflected in cascade
  output)
- `render_with_css`: inline `<style>` still works when no external
  CSS provided
- PageCache: roundtrip `{html, css}` together

**Integration test**:

- Fetch Wikipedia HTML article, extract sources, fetch CSS
  (subject to caps), parse merged. Assertion: no crash; acceptance
  rate on merged CSS >= 27% (external alone).

**Visual verification** (measured in Commit 2):

- Load Wikipedia HTML article before and after ADR-025.
- Compare visible viewport screenshots.
- Concrete checks:

| Sub-check | Expected |
|---|---|
| References section (bulleted list, indentation) | **YES** |
| Article `<pre>` code blocks (background) | **YES** |
| Sidebar navigation (single unstyled blob) | **NO CHANGE** (blocked by `@media`/`:root`) |
| Header (unstyled row of text) | **NO CHANGE** |
| TOC (unstyled list) | **NO CHANGE** |

If >=2 of the 3 "expected YES" checks pass -> visible delta
confirmed. If <2 pass -> ADR-025 ships anyway (fetch pipeline is
real value) but the ADR's Implementation section documents the
smaller-than-expected delta and identifies which blocker is
dominant.

## Acceptance criteria

ADR-025 is complete when:

- [ ] `collect_css_sources` implemented + tested (3+ cases)
- [ ] `PageCache` extended to `CacheEntry { html, external_css }`
      + tests
- [ ] `RenderPipeline::render_with_css` implemented + tested
      (2+ cases)
- [ ] Window integration: fetch loop with caps, wired to
      `render_with_css`
- [ ] Cache hit on Back/Forward re-renders from cached
      `(html, css)` -- no re-fetch
- [ ] Measured acceptance rate on merged CSS from Wikipedia
      (Commit 2 message)
- [ ] Visual checks documented in Commit 3's ADR Implementation
      section
- [ ] `main` is green: 271 + N cases, all pass, zero warnings
- [ ] ADR-025 status -> Implemented in Commit 3
