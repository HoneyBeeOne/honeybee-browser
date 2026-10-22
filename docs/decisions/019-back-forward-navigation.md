# ADR-019: Back and Forward Navigation

**Status:** Accepted
**Date:** 2026-10-22
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-018 (state pseudo-classes), ADR-007 (DOM ownership)

## Context

After ADR-018, the browser navigates three ways: typing a URL in
the address bar and pressing Enter, clicking a rendered `<a href>`
link, and pressing the Reload button (`↻`). The other two toolbar
buttons -- Back (`←`) and Forward (`→`) -- are stubs.
`on_back_clicked()` and `on_forward_clicked()` print
"غير مُفعَّل بعد" ("not yet implemented") to the status bar.
Clicking them does nothing else.

This is the last obviously-incomplete UI element. Every real
browser since Mosaic 0.10 has supported back/forward; their
absence is a visible gap in the "real browser behavior" story.

ADR-018 left the pieces that make this straightforward:

- `HoneyBeeWindow::navigate_to(url)` is the single fetch path
  shared by the URL bar and link clicks
- `HoneyBeeWindow::m_current_url` tracks the last-successfully-loaded
  URL (used by Reload)
- `RenderPipeline::record_visit(url)` marks URLs as visited for
  `:visited` matching

What's missing is a **history stack**: an ordered list of URLs
with a cursor at the current position. Back moves the cursor
left, Forward moves it right, and a new navigation truncates
any forward history.

## Decision

We add a session-scoped navigation history in the browser layer,
encapsulated in a testable helper class.

### Data model

New file `src/browser/navigation_history.hpp`:

```cpp
class NavigationHistory {
public:
    void push(const std::string& url);
    bool can_back() const;
    bool can_forward() const;
    bool empty() const;
    const std::string& current() const;
    std::optional<std::string> back();
    std::optional<std::string> forward();
    void clear();
    std::size_t size() const;
    std::size_t index() const;

private:
    std::vector<std::string> entries_;
    std::size_t index_ = 0;
};
```

`push(url)` truncates entries after `index_`, appends `url`, and
sets `index_` to the new last position. `back()` and `forward()`
move `index_` and return the newly-current entry; both return
`std::nullopt` when the move is impossible (index at start or
end), so callers can distinguish "moved" from "not possible"
without a separate `can_*` check -- although `can_back()` /
`can_forward()` are provided for button-state queries.

The class has no GTK dependency and no knowledge of fetch or
render -- pure data structure, unit-testable.

### Navigation flow

`HoneyBeeWindow` holds `NavigationHistory m_history` and
dispatches navigation through a single method with a `NavKind`
discriminator:

```cpp
enum class NavKind { New, Back, Forward, Reload };
void navigate_to(const std::string& url, NavKind kind);
```

| `NavKind` | History mutation | Trigger |
|---|---|---|
| `New` | `push(url)` on success | URL bar Enter, click link |
| `Back` | `back()` at entry; index already moved | Back button |
| `Forward` | `forward()` at entry; index already moved | Forward button |
| `Reload` | none | Reload button |

For `Back`/`Forward`, the cursor is moved at the *entry* of
`navigate_to` (by `on_back_clicked()` / `on_forward_clicked()`),
so the URL to fetch is determined before the async call. If
the fetch fails, the move is undone (`back()` followed by
`forward()`, or vice versa), restoring the cursor.

For `New`, the history entry is pushed *after* a successful
fetch, so a failed navigation does not create a history entry.

For `Reload`, history is untouched -- Reload must not accumulate
entries.

All four share the same fetch path: validate -> disable controls
-> async fetch -> on success, `record_visit` -> `render_html` ->
`update_nav_buttons()`.

### Failure handling

- `New` fails: no history change (push was deferred to success)
- `Back` fails: `forward()` to undo the entry-time `back()`
- `Forward` fails: `back()` to undo the entry-time `forward()`
- `Reload` fails: no history change (Reload never touched history)

### Button state

`update_nav_buttons()` is called after every history mutation
and after every fetch completion:

```cpp
m_back_button.set_sensitive(m_history.can_back());
m_forward_button.set_sensitive(m_history.can_forward());
```

Constructor: history empty -> both buttons disabled (changing
from ADR-018's state where both were `set_sensitive(true)` but
non-functional).

During a fetch: both buttons are explicitly disabled to prevent
racing with the in-flight navigation. `update_nav_buttons()`
restores them after fetch completion.

## Design decisions

### Why a helper class, not inline state

Inlining `std::vector<std::string>` + `std::size_t` in
`HoneyBeeWindow` would work but be untestable without a running
GTK window. Extracting `NavigationHistory` allows pure unit
tests for the truncate-on-push, back-at-start, forward-at-end,
and clear edge cases. The window's integration becomes a thin
caller.

### Why push-on-success, not optimistic

Real browsers record a history entry when the navigation commits
(the new document starts loading), not when the user initiates
it. A failed fetch (404, network error, invalid URL) does not
create a history entry. For v1 with a simple async fetch,
"success" is close enough to "commit", and it matches the user's
mental model: history is what actually loaded.

### Why Reload must not touch history

Reload semantics: re-fetch the current URL, replace the current
entry. If Reload pushed a new entry, pressing Reload N times
would create N history entries, and Back would require N presses
to leave the page. Real browsers don't do this. `NavKind::Reload`
bypasses both `push` and cursor movement.

### Why no page cache

Back to a page that loaded successfully, then Back again, means
re-fetching the HTML from the server. A real browser caches the
HTML and DOM, so Back is instant. That's a bigger change: cache
key, eviction policy, integration with `RenderPipeline`. Out of
scope for ADR-019; documented as deferred.

### Interaction with `:visited`

`RenderPipeline::record_visit(url)` fires on every successful
load, including Back/Forward/Reload. `visited_urls` is a
`std::unordered_set<std::string>`, so re-insertion is idempotent.
No changes needed. `:visited` reflects what has been loaded,
regardless of how.

## Deferred

- **Page caching**: Back re-fetches. A future ADR will add an
  in-memory HTML cache keyed by URL.
- **Scroll position restoration**: Back restores content but
  scroll returns to top.
- **History persistence**: history is session-scoped; closing
  the browser loses it.
- **History length cap**: unbounded in a session. Real browsers
  cap at ~50; a local browser rarely hits this.
- **History UI**: no dropdown, no right-click, no keyboard
  shortcuts (Alt+Left / Alt+Right).
- **Cross-tab history**: not applicable (one tab).
- **Failed-fetch cleanup**: if a page enters history, the server
  then goes down, and Back to it fails, the entry stays in
  history. Retrying Back will fail until the URL is reachable.

## Consequences

**Better:**
- Back/Forward buttons become functional. The last
  visibly-incomplete UI element is complete.
- `:visited` becomes more meaningful: going back to a page
  renders its own links with correct visited state.
- `NavigationHistory` is a self-contained class, extendable
  later (cache, persistence) without touching `HoneyBeeWindow`.

**Accepted limitations:**
- Back re-fetches: slow networks see a delay, not an instant
  restore.
- No scroll restoration: returning to a long page lands at the
  top.
- History lost on close.

**Unchanged:**
- `:link`, `:visited`, `:hover` behavior -- no new pseudo-classes
  or matcher changes.
- Click navigation -- becomes a `NavKind::New` caller, no
  behavior change.
- The `hovered_element` fix from ADR-018 -- history rollback
  doesn't touch `hovered_element`; `render()` still clears it on
  every load.

## Implementation plan

Four commits on `feat/back-forward-navigation`:

1. **ADR-019 document** (this file, `Status: Accepted`)
2. **`NavigationHistory` class + unit tests** -- pure data
   structure. New file `src/browser/navigation_history.hpp`,
   tests in `tests/test_navigation_history.cpp`. Not yet used
   by `HoneyBeeWindow` -- verification is isolated.
3. **`HoneyBeeWindow` integration** -- hold `NavigationHistory`,
   add `NavKind`, extend `navigate_to`, implement
   `on_back_clicked()` / `on_forward_clicked()` /
   `on_reload_clicked()` with real logic, add
   `update_nav_buttons()`, initialize buttons disabled in
   constructor. Visual verification included.
4. **Docs update** -- ADR-019 status -> Implemented,
   `README.md` features table adds a "Back / forward
   navigation" row, `CHANGELOG.md` `[Unreleased]` gains
   entries.

## Verification

Unit tests (`tests/test_navigation_history.cpp`) cover:

- `push` on empty: size 1, index 0, `!can_back`, `!can_forward`
- `push` twice: size 2, index 1, `can_back`, `!can_forward`
- `back` once: index 0, `!can_back`, `can_forward`;
  `current()` is first URL
- `forward` once: index 1, `can_back`, `!can_forward`;
  `current()` is second URL
- `push` after `back`: truncates forward entry (size stays 2,
  index 1, `!can_forward`)
- `back` at start: `std::nullopt`, state unchanged
- `forward` at end: `std::nullopt`, state unchanged
- `clear`: size 0, index 0

Visual verification:

- Load A -> Back/Forward both disabled
- Navigate to B (URL bar) -> Back enabled, Forward disabled
- Click Back -> A loads; Back disabled, Forward enabled
- Click Forward -> B loads; Back enabled, Forward disabled
- Click Back, then navigate to C -> forward history truncated
  (Forward disabled)
- Load page with links (e.g. link-test-a), click a link, then
  Back -> return links are purple (integration with `:visited`)
- Reload on B does not create a history entry (Back goes to A,
  not B)
- Click Back while already at start: button disabled, cannot
  click
- Click Forward while at end: button disabled, cannot click

## Acceptance criteria

ADR-019 is complete when:

- [ ] `NavigationHistory` unit tests pass (8+ cases)
- [ ] `HoneyBeeWindow` wires back/forward/reload through
      `navigate_to(url, NavKind)`
- [ ] Back/Forward buttons enable/disable correctly
- [ ] Failed fetches do not pollute history
- [ ] Reload does not touch history
- [ ] Visual verification (screenshots) confirms the above in
      the running browser
- [ ] `main` is green: 237 + N cases, all pass, zero warnings
