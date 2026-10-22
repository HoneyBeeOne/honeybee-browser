# ADR-020: Page Caching for Back/Forward Navigation

**Status:** Accepted
**Date:** 2026-10-22
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-019 (back/forward navigation), ADR-018 (pseudo-classes)

## Context

After ADR-019, Back and Forward work functionally: the toolbar
buttons traverse a session-scoped history stack, correctly
enabling and disabling based on the cursor position. But every
Back/Forward re-fetches the target page from the server.

On localhost this is invisible -- the fetch completes in
milliseconds. On a real network, clicking Back waits on the
same round-trip a fresh navigation would. That contradicts the
universal expectation of Back: it is supposed to be instant,
because the browser is showing you what you already had.

Every real browser caches HTML for exactly this reason. The
`BackForwardCache` (bfcache) is a separate mechanism that
preserves even the rendered DOM and JS state; the ordinary
HTTP cache covers the "page was loaded 10 seconds ago, don't
refetch" case. We are implementing the latter -- session-scoped,
in-memory, no persistence.

ADR-019 established the pieces this ADR needs:

- `NavigationHistory` distinguishes `New` / `Back` / `Forward`
  / `Reload` navigations via the `NavKind` enum.
- `navigate_to(url, kind)` is the single fetch path.
- The fetch success callback is the single point where every
  loaded page passes through (`record_visit(url)`,
  `render_html(r.body)`).

What's missing is a keyed store of previously-fetched HTML,
populated on every successful fetch and consulted on Back/Forward.

## Decision

We add a session-scoped, in-memory HTML cache, keyed by URL.

### Data model

New file `src/browser/page_cache.hpp`:

```cpp
class PageCache {
public:
    void put(const std::string& url, std::string html);
    std::optional<std::string> get(const std::string& url) const;
    bool contains(const std::string& url) const;
    void clear();
    std::size_t size() const;
    std::size_t bytes() const;

private:
    std::unordered_map<std::string, std::string> entries_;
};
```

Header-only, no GTK, no fetch. Same design shape as
`NavigationHistory` -- a pure data structure that
`HoneyBeeWindow` holds as a member and drives from
`navigate_to`.

`bytes()` is the sum of `html.size()` across entries -- cheap,
useful for future eviction diagnostics and for the
`[page-cache]` unit tests.

### Cache key

The fully-resolved URL string that `navigate_to` receives. This
is byte-identical to what `m_history` stores and what
`m_pipeline.record_visit` records. Back/Forward lookups use the
`m_history` entry directly, so no re-resolution at lookup time.

### Populate: every successful fetch

In `navigate_to`'s success callback (after `r.success()`):

```cpp
m_cache.put(url, r.body);
```

This runs for `New`, `Back`, `Forward`, and `Reload` successes
uniformly. Back/Forward on cache-hit bypass the fetch entirely
(see next section) -- the `put` only fires on cache-miss
Back/Forward, which is impossible in v1 given unbounded cache
(see "Elegant consequence" below), but is harmless if it ever
does.

Overwriting is intentional: a Reload's success overwrites the
entry with the fresh body. A subsequent Back to the same URL
returns the refreshed content.

### Lookup: Back/Forward only

At the top of `navigate_to`, after URL validation and before
the disable-controls step:

```cpp
if (kind == NavKind::Back || kind == NavKind::Forward) {
    if (auto cached = m_cache.get(url)) {
        m_current_url = url;
        m_pipeline.record_visit(url);
        update_nav_buttons();
        render_html(*cached);
        return;
    }
}
```

Key points:

- **No "loading" state.** Controls are not disabled during a
  cache hit. Back/Forward feels instant because it is instant.
- **`record_visit` still fires.** `RenderState::base_url` must
  be updated before render (affects relative-href resolution),
  and `visited_urls` re-insertion is idempotent.
- **`update_nav_buttons()` still fires.** The cursor already
  moved (caller side), so button state must reflect it. Same
  function as the async success path uses.
- **`m_current_url` still updates.** Reload relies on it.

### Why New and Reload bypass the cache

- **New**: typing a URL, clicking a link, or an external
  navigation is a user's explicit request for content. Serving
  a possibly-stale cached copy would be surprising without
  HTTP cache-headers to justify the freshness decision. When
  we add `Cache-Control` / `ETag` support (deferred), the New
  path will consult those.
- **Reload**: the button's entire purpose is "fetch fresh,
  bypass any cache". Serving cached content on Reload would
  make it a no-op.

Both paths still *populate* the cache on success, so a page
loaded via New or Reload is available for subsequent Back.

### Elegant consequence: Back/Forward never cache-miss in v1

ADR-019 pushes a history entry only on successful fetch.
ADR-020 caches only on successful fetch. Therefore:

    Every entry in m_history  ⟹  in m_cache

Back/Forward always hit. ADR-019's "rollback on failure"
handling for `Back`/`Forward` becomes unreachable in v1. We
leave it in place: the invariant breaks the day we add
eviction, and the safety is cheap.

## Design decisions

### Why a helper class, not inline in HoneyBeeWindow

Same reasoning as `NavigationHistory`: pure data structure is
testable without a running GTK window. The window's job becomes
"wire the cache to the fetch lifecycle", not "implement a hash
map with edge cases".

### Why header-only

Small enough (~40 lines) that a `.cpp` split adds file-plumbing
cost without benefit. `NavigationHistory` is also header-only;
symmetric.

### Why std::unordered_map, not std::map

No ordering needed. `unordered_map` gives O(1) lookup, and
duplicate entry prevention is inherent. `std::map`'s lexicographic
ordering would be wasted.

### Why store raw HTML string, not parsed Document

The pipeline parses in milliseconds. Re-parsing on every Back
also means a cached Back exercises the *current* parse/render
code path -- if we've improved whitespace handling since the
page was cached, a cached Back picks up the improvement. A
cached parsed DOM would freeze old behavior.

There is a real cost to this choice: layout is re-run, and the
entire cascade re-executes (including `:visited` matching
against the current visited set, which is correct). Both are
fast enough that the cost is invisible.

### Why no eviction

Unbounded in a session. For a local browser, a session rarely
exceeds dozens of pages, and even a hundred pages of typical
HTML is under 10MB. Adding an LRU cap would require an
eviction policy ADR and interacts with the "Back always hits"
invariant from above -- once we evict, Back can miss, which
means ADR-019's rollback path becomes reachable again. That's
a meaningful design shift, not a small optimization. Defer to
its own ADR.

### Interaction with :visited

`record_visit` fires on cache hit and cache miss alike.
`visited_urls` is a set; re-insertion is idempotent. `:visited`
rendering reflects what has been loaded, regardless of whether
the current render came from fetch or cache.

### Interaction with navigation history

Cache never mutates history. History never mutates cache. The
two are independent stores with a 1:1 relationship in v1
(guaranteed by the "push-on-success / cache-on-success"
coupling), and remain independent when that relationship breaks
(eviction, persistent history, cross-session restoration).

## Deferred

- **Eviction** (LRU, size cap). Unbounded in v1.
- **Persistence** across sessions. Cache is process-memory only.
- **HTTP cache semantics** (`Cache-Control`, `ETag`, `Expires`,
  `Vary`). The New navigation path will use these when they
  exist.
- **Conditional GETs** (`If-Modified-Since`, `If-None-Match`) --
  revalidation instead of re-fetch.
- **Memory cap / eviction metrics surface.** `bytes()` exists
  for diagnostics, not user-facing memory limits.
- **Page state beyond HTML** -- form field values, JS state,
  scroll position. Currently Back restores content but loses
  interactive state. That is bfcache territory, not HTTP-cache
  territory.
- **Cache invalidation on POST.** v1 has no POST.

## Consequences

**Better:**

- Back/Forward become effectively instant in the running
  browser. The perceived latency of "go back" drops from
  network round-trip to re-parse time, which is well under
  a frame at typical page sizes.
- Session integrity: killing the HTTP server mid-session
  leaves Back/Forward functional for any page already visited.
  Reload fails (correct -- it must fetch); Back succeeds.
  This is the definitive manual verification of the cache.
- No user-visible behavior changes otherwise. Pseudo-classes,
  click-navigation, history traversal all behave identically.

**Accepted limitations:**

- Stale content on Back if the server changed the page since
  the initial fetch. Reload refreshes.
- Unbounded memory growth per session. Not a concern at local
  scale; becomes one with dozens of sessions of use in one
  process (not a use case we support).
- New navigations to previously-cached URLs still re-fetch.
  Without HTTP cache headers this is the correct default but
  means an obvious optimization (`New` to a fresh page just
  fetched) isn't taken.

**Unchanged:**

- `:link`, `:visited`, `:hover` rendering -- no matcher changes.
- Click navigation -- still `NavKind::New`, still bypasses cache.
- History traversal semantics -- `push` / `back` / `forward` /
  truncate unchanged from ADR-019.
- The `hovered_element` clear-on-render fix from ADR-018 --
  runs on both fetch and cache-hit render paths.

## Implementation plan

Three commits on `feat/page-cache`:

1. **`PageCache` class + unit tests** -- `src/browser/page_cache.hpp`
   (header-only), `tests/test_page_cache.cpp` (8+ cases), CMake
   update. Not yet used by `HoneyBeeWindow`.
2. **`HoneyBeeWindow` integration** -- `m_cache` member,
   cache-populate in fetch success, cache-lookup at top of
   `navigate_to` for Back/Forward. Visual verification
   including the kill-server test.
3. **Docs update** -- ADR-020 status -> Implemented,
   `README.md` features row, `CHANGELOG.md` `[Unreleased]`.

## Verification

Unit tests (`tests/test_page_cache.cpp`):

- `put` then `get` returns the stored body
- `get` on empty cache returns `std::nullopt`
- `get` for unknown URL returns `nullopt` (empty and non-empty cache)
- `put` overwrite: second `put` replaces first
- `contains` matches `get` state
- `size` counts unique URLs
- `bytes` sums `html.size()` across entries
- `clear` empties everything

Visual verification (screenshots):

- Load A, load B -> both fetched (status bar shows byte count)
- Click Back -> A loads without a fetch (status bar shows "تم الجلب" byte count from initial fetch, not a new one -- or verify via timing)
- **Kill HTTP server**, then Click Back again -> still navigates to A (proof of cache)
- With server down, click Reload on A -> fetch fails (correct -- Reload bypasses cache)
- With server down, click a link on page A -> fetch fails (correct -- New bypasses cache)
- Restart server, click Reload -> succeeds, cache refreshed
- Click Back to B, then Forward to A (both cached, both instant)

## Acceptance criteria

ADR-020 is complete when:

- [ ] `PageCache` unit tests pass (8+ cases)
- [ ] `HoneyBeeWindow` populates cache on every successful fetch
- [ ] Back/Forward use the cache and skip the fetch on hit
- [ ] New and Reload always fetch (bypass cache read; still populate on success)
- [ ] Reload refreshes the cache entry
- [ ] Kill-server test: Back works with server down; Reload fails
- [ ] `main` is green: 245 + N cases, all pass, zero warnings
- [ ] Screenshots confirm all the above in the running browser
