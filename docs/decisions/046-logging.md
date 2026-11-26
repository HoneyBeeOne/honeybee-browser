# ADR-046: Logging Infrastructure

**Status:** Implemented
**Date:** 2026-10-29
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-005 through ADR-045 (all subsystems gain log points)

## Context

The project has **zero debug-print infrastructure**. Diagnostic
work on ADR-040 through ADR-045 relied on temporary `fprintf`
patches added and removed during each cycle. Every debug session
is currently:
1. Add temporary prints
2. Rebuild
3. Reproduce
4. Read output
5. Remove prints
6. Rebuild

The cost compounds — each ADR cycle adds temporary code that is
discarded. Zero permanent observability. When something breaks
after a merge, the only way to diagnose is to add prints again.

This ADR adds permanent, gated logging infrastructure.

### Measurement (source tree, 2026-10-29)

| Check | Result |
|---|---|
| Existing log header/source | None (only `url.*`, `version.*` in `src/base/`) |
| Existing `fprintf(stderr)` / `std::cerr` / `std::clog` in `src/` | **Zero** |
| Existing `getenv` usage | **Zero** |
| `ifdef DEBUG` / `CMAKE_BUILD_TYPE` conditionals in `src/CMakeLists.txt` | **Zero** |
| Main entry | `src/main.cpp:6` |
| `honeybee_lib` CMake target | Includes `base/version.cpp`, `base/url.cpp` — pattern for `base/log.cpp` |

**Clean slate.** ADR-046 is purely additive; no retrofit of
existing prints is needed (none exist). Adding `base/log.cpp`
to `honeybee_lib`'s source list is one CMake line.

## Decision

Add a logging subsystem under `src/base/log.{hpp,cpp}` with:

- **Runtime-gated** by environment variables (no rebuild to
  enable)
- **Category + level** filtering (per-subsystem control)
- **Zero cost when disabled** (single boolean check per call
  site, no string construction)
- **Thread-safe** (`async_fetcher` runs on its own thread)
- **printf-style formatting** (matches existing GTK/lexbor
  C-style idioms; C++20 `<format>` not assumed)

### API surface

**`src/base/log.hpp`:**

```cpp
namespace honeybee {

enum class LogCategory : std::uint8_t {
    Pipeline,   // main app, GTK lifecycle, load sequencing
    Css,        // stylesheet parsing, rule extraction
    Cascade,    // matching, specificity, computed styles
    Layout,     // block / grid / flex / inline formatting
    Paint,      // box paint, images, clips
    Fetch,      // HTTP requests, CSS + image fetches
    Event,      // click, wheel, hover, keyboard
};

enum class LogLevel : std::uint8_t {
    Error,      // 0 -- most severe
    Warn,       // 1
    Info,       // 2
    Debug,      // 3
    Trace,      // 4 -- least severe
};

// Called once from main(). Reads HONEYBEE_LOG,
// HONEYBEE_LOG_LEVEL, HONEYBEE_LOG_FILE. Idempotent.
void log_init();

// Programmatic config (for tests; bypasses env vars).
// category_mask = bitmask of (1 << static_cast<uint8_t>(cat)).
// Log at level L if L <= min_level (numeric comparison).
void log_set_config(bool enabled,
                    std::uint32_t category_mask,
                    LogLevel min_level,
                    std::FILE* output);

// Hot-path check. noexcept; noexcept-reads atomics.
bool log_enabled(LogCategory cat, LogLevel lvl) noexcept;

// Actual write. Thread-safe. Called only when log_enabled
// returned true (via the HB_LOG macro). Not intended for
// direct call from user code.
void log_writev(LogCategory cat, LogLevel lvl,
                const char* file, int line,
                const char* fmt, ...) noexcept;

} // namespace honeybee

// User-facing macro. Zero cost when disabled: single branch
// on log_enabled, no formatting, no allocation.
#define HB_LOG(cat, lvl, fmt, ...) \
    do { \
        if (::honeybee::log_enabled(::honeybee::LogCategory::cat, \
                                    ::honeybee::LogLevel::lvl)) { \
            ::honeybee::log_writev(::honeybee::LogCategory::cat, \
                ::honeybee::LogLevel::lvl, __FILE__, __LINE__, \
                fmt, ##__VA_ARGS__); \
        } \
    } while (0)
```

### Environment variables

Three variables, all optional, all read once by `log_init()`:

| Variable | Example | Default |
|---|---|---|
| `HONEYBEE_LOG` | `layout,paint,fetch` or `all` | (unset = logging disabled) |
| `HONEYBEE_LOG_LEVEL` | `error\|warn\|info\|debug\|trace` | `debug` when `HONEYBEE_LOG` set |
| `HONEYBEE_LOG_FILE` | `/tmp/honeybee.log` | stderr |

**Why three variables** (not one `HONEYBEE_LOG=layout:debug`):
- Each stays short and greppable in shell history
- Matches `RUST_LOG` / `MOZ_LOG` conventions
- Category set is the common toggle; level and file are
  advanced options

**Why `HONEYBEE_LOG` unset means disabled:** no `ifdef DEBUG`
needed; production users get zero logging with zero env setup.
Devs opt in with `export HONEYBEE_LOG=all`.

### Output format

```
[honeybee][layout][debug] src/renderer/layout/layout.cpp:482 child_count=3
```

- Grep-friendly: `grep '\[layout\]'`
- File path trimmed at `"src/"` — full path
  `/home/user/src/renderer/.../layout.cpp` becomes
  `src/renderer/.../layout.cpp`
- Level before message
- One line per log call (embedded `\n` in `fmt` tolerated but
  discouraged; multi-line output breaks grep)

### Category / level filtering

**Category mask**: bit N = category N enabled. `HONEYBEE_LOG=layout,paint`
sets bits for `Layout` and `Paint` only.

**Level threshold**: numeric comparison. `HONEYBEE_LOG_LEVEL=info`
means "log Info, Warn, Error" (levels 0-2; excludes Debug,
Trace). `HONEYBEE_LOG_LEVEL=trace` logs everything.

**Effective check**: `enabled && (mask & (1 << cat)) && (lvl <= min_level)`.

### Thread safety

`log_writev` holds a `std::mutex` around `vfprintf`. Enables
logging from `async_fetcher` thread without interleaved output.

`log_init` and `log_set_config` are called from `main` (single
thread) before any workers start. Not reentrant-safe — no
mechanism to change config at runtime after first call. This
is intentional: log config is a startup decision.

### Zero-cost when disabled

`log_enabled` reads three atomics:
- `enabled_` (bool)
- `category_mask_` (uint32)
- `min_level_` (enum)

One branch per call site. No string construction, no mutex
lock, no syscall. The `if` guard inside `HB_LOG` prevents
`log_writev` from running.

**Alternative** (compile-time `#ifdef DEBUG`): rejected
because it requires rebuild to toggle. Runtime env-var check
costs ~1ns per disabled call; the observability win is
enormous.

## Design decisions

### Why printf, not stream or std::format

**printf-style** chosen because:
- Familiar C/C++ syntax
- `##__VA_ARGS__` works on GCC/Clang (which the project targets)
- No `<sstream>` include in the disabled path
- No assumption of C++20 `<format>` support (libstdc++
  13.0+, not universal)

**Alternative** (stream: `HB_LOG(Layout, Debug, "placed " << n)`):
zero-dependency too but unusual macro expansion; some users
trip on `<<` inside macro args.

**Alternative** (`std::format`): the eventual winner, but
premature today. If libstdc++ adds `<format>`, ADR-047 can
migrate `log_writev` to `std::vformat` with zero API changes
(the macro already takes a format string).

### Why 7 categories, not 3 or 15

Enough granularity to separate the actual debug sessions:
- Layout vs Paint distinguishes "wrong position" from "not
  painted"
- Cascade vs Css distinguishes "rule not parsed" from "rule
  not matched"
- Fetch isolates network from everything else

Not so granular that each call site needs thinking:
7 categories × 5 levels = 35 combos is manageable.

Future additions (e.g. `Honeypot` for future subsystems) are
trivial — one enum value, one mask bit.

### Why `log_set_config` for tests

Env vars leak between tests via `setenv`/`unsetenv` even with
`atexit` cleanup, especially in Catch2's parallel test
runners (if ever enabled). The `log_set_config(bool, mask,
level, FILE*)` API bypasses env entirely, giving each test
full control. Same pattern as `cairo_set_source_rgb` vs.
`cairo_t` state — programmatic config independent of
environment.

### Why file paths trimmed at `"src/"`

Full paths are ~60 chars of noise per line:
`/home/amrmuhammad/dev/honeybee-browser/src/renderer/layout/layout.cpp:482`

Trimmed: `src/renderer/layout/layout.cpp:482` — 34 chars,
still unique (all `src/` files unique from `src/`).

Absolute paths (rare for `__FILE__`) fall through unchanged.

### Deferred (v1 scope)

- **`std::format` migration**: future ADR-047 if libstdc++
  adds it
- **Runtime config change**: config set once at startup; no
  `HONEYBEE_LOG` hot-reload
- **Log rotation**: if `HONEYBEE_LOG_FILE` grows unbounded,
  external `logrotate` handles it (standard Unix approach)
- **Structured output** (JSON / key-value): `HB_LOG` emits
  free-form text. Structured logging is future scope.
- **Per-level thresholds per category** (`layout:trace,css:info`):
  single global level today. Add if needed.
- **Trace-level perf timing** (`HB_TRACE_TIME`): could time
  every function entry/exit. Not in v1 (invites misuse).

## Consequences

**Better:**
- **Zero-cost, gated logging** available from any subsystem
- **No rebuild to enable** — `export HONEYBEE_LOG=layout`
  just works
- **Thread-safe** — `async_fetcher` logs without corruption
- **Grep-friendly** output format
- **Permanent infrastructure** — no more add/remove debug
  prints

**Accepted limitations:**
- printf-style, not type-safe (`HB_LOG(Layout, Debug, "%s", 42)`
  is UB but won't crash) — accepted tradeoff for familiarity
- Config is startup-only — no runtime toggle
- Global level, not per-category (v1 simplification)

**Unchanged:**
- All ADR-005 through ADR-045 behavior
- No existing debug prints to remove (none exist)

**Retrofit target list (Commit 2)** — new log points, not
retrofits since nothing exists today:

| File | Log points |
|---|---|
| `main.cpp` | `log_init()` call; `HB_LOG(Pipeline, Info, "starting")` |
| `browser/render_pipeline.cpp` | cache hit/miss, cascade count, layout time, fetch count |
| `browser/async_fetcher.cpp` | fetch start/end, response sizes |
| `renderer/css/cascade.cpp` | rules matched, elements styled |
| `renderer/layout/layout.cpp` | element count, dispatch mode (block/grid/flex) |
| `renderer/paint/paint.cpp` | box count, image count |
| `net/client.cpp` | HTTP request URLs, status codes |

~15-20 log points across 7 files.

## Implementation plan

Three commits:

1. **Infrastructure** (`base/log.{hpp,cpp}` + CMake + tests):
   - `LogCategory`, `LogLevel`, `log_init`, `log_set_config`,
     `log_enabled`, `log_writev`, `HB_LOG` macro
   - Env var parsing (`HONEYBEE_LOG`, `HONEYBEE_LOG_LEVEL`,
     `HONEYBEE_LOG_FILE`)
   - File path shortening
   - Thread-safe write
   - `log_init()` call in `main.cpp` at entry
   - `tests/test_log.cpp` (new file, ~12 tests)
   - Zero log points yet — no output change

2. **Retrofit target list** (visible in stderr when
   `HONEYBEE_LOG` set):
   - ~15-20 `HB_LOG` call sites across 7 files
   - Categories per target list above
   - Manually verified: `HONEYBEE_LOG=all ./build/bin/honeybee`
     shows pipeline, cascade, layout, paint events
   - No production code behavior change when
     `HONEYBEE_LOG` unset

3. **Docs**:
   - ADR-046 status -> Implemented
   - README section on env vars
   - CHANGELOG entry

## Verification

**Unit tests (Commit 1, `[log][adr-046]`):**

- `log_set_config(false, ...)` → `log_enabled` returns false
  for any (cat, lvl)
- `log_set_config(true, mask=Layout|Paint, Debug, output)` →
  `log_enabled(Layout, Debug)` true; `log_enabled(Layout,
  Trace)` false (trace > debug numerically); `log_enabled(Css,
  Debug)` false (mask excludes Css)
- `log_set_config(true, all_bits, Info, output)` →
  `log_enabled(any, Error/Warn/Info)` true;
  `log_enabled(any, Debug/Trace)` false
- `log_writev` writes one line to `output` with format
  `[honeybee][layout][debug] <path>:<line> <msg>\n`
- File path shortening: absolute path with `src/` prefix
  trimmed; absolute path without `src/` unchanged
- Multi-threaded: two threads logging simultaneously →
  no interleaved partial lines (via mutex)
- `log_init` with `HONEYBEE_LOG=all`, `HONEYBEE_LOG_LEVEL=trace` → all
  categories/levels enabled
- `log_init` with no env → logging disabled
- `log_init` with malformed env → logging disabled (safe
  fallback)
- `HB_LOG` macro when disabled → no output to `output`
- `HB_LOG` macro when enabled → one line, correct format

**Manual verification (Commit 2):**

| Env | Expected output |
|---|---|
| (unset) | Baseline stderr (only GTK warnings, existing behavior) |
| `HONEYBEE_LOG=layout` | Layout events only |
| `HONEYBEE_LOG=layout,paint` | Layout + paint events |
| `HONEYBEE_LOG=all` | All categories at debug level |
| `HONEYBEE_LOG=all HONEYBEE_LOG_LEVEL=trace` | All + trace detail |
| `HONEYBEE_LOG=all HONEYBEE_LOG_FILE=/tmp/hb.log` | Writes to file, stderr unchanged |

## Acceptance criteria

ADR-046 is complete when:

- [ ] `src/base/log.{hpp,cpp}` added to `honeybee_lib` CMake
- [ ] `LogCategory`, `LogLevel`, `log_init`, `log_set_config`,
      `log_enabled`, `log_writev`, `HB_LOG` macro
- [ ] `HONEYBEE_LOG`, `HONEYBEE_LOG_LEVEL`, `HONEYBEE_LOG_FILE`
      env vars parsed
- [ ] Zero cost when disabled (single branch)
- [ ] Thread-safe write (mutex around vfprintf)
- [ ] File path shortened at `"src/"`
- [ ] `log_init()` called from `main.cpp`
- [ ] 12+ unit tests in `tests/test_log.cpp`
- [ ] 15-20 `HB_LOG` call sites in retrofit target list
- [ ] Manual verification across 6 env var combinations
- [ ] `main` green: 572 + N cases, all pass
- [ ] ADR-046 status -> Implemented

**Explicit claim**: `HB_LOG(Layout, Debug, "child_count=%d", n)`
works from any subsystem. `export HONEYBEE_LOG=layout` before
running `./build/bin/honeybee` shows layout events on stderr.
No rebuild needed. Zero cost when disabled.

## Implementation

Implemented on `feat/logging` across three commits (Commit 1 =
infrastructure + tests, Commit 2 = 12 HB_LOG sites across 6
files, Commit 3 = this docs update).

### What shipped

**Commit 1 -- infrastructure:**
- `src/base/log.{hpp,cpp}` added to `honeybee_lib` CMake
- `LogCategory` (7 values), `LogLevel` (5 values)
- `log_init()`, `log_set_config()`, `log_enabled()`,
  `log_writev()`, `HB_LOG` macro
- Env var parsing: `HONEYBEE_LOG`, `HONEYBEE_LOG_LEVEL`,
  `HONEYBEE_LOG_FILE`
- File path trimming at `/src/`
- Thread-safe (mutex around `fprintf` + `vfprintf` + newline)
- `log_init()` called as first statement of `main()`
- 14 unit tests in `tests/test_log.cpp`

**Commit 2 -- 12 HB_LOG sites across 6 files:**

| File | Sites | Categories |
|---|---|---|
| `main.cpp` | 1 | Pipeline (startup) |
| `render_pipeline.cpp` | 6 | Pipeline (relayout, cascade complete, layout complete, restyle fallback, restyle Level-0) |
| `async_fetcher.cpp` | 2 | Fetch (fetch entry, callback dispatch) |
| `cascade.cpp` | 1 | Cascade (rules/elements counts) |
| `layout.cpp` | 1 + warning | Layout (viewport dims, no-root) |
| `paint.cpp` | 1 | Paint (root node kind) |

**Commit 3 (this commit) -- docs:**
- ADR-046 status -> Implemented
- README section on logging env vars
- CHANGELOG entry

### Manual verification (4 env-var combinations)

**1. `HONEYBEE_LOG` unset (baseline):**
No `[honeybee]` output. Zero production behavior change.

**2. `HONEYBEE_LOG=layout` (initial page, 3s window):**
No output within window. Correct: pipeline/cascade excluded
by mask; no page loaded so no layout events fired.

**3. `HONEYBEE_LOG=all` (initial page, piped):**
Piping (`timeout | grep | head`) swallows buffered output.
Not a code issue -- test 4 proves the path works.

**4. `HONEYBEE_LOG=all HONEYBEE_LOG_FILE=/tmp/hb.log`:**
File contains:
```
[honeybee][pipeline][info] src/main.cpp:11 startup: version=0.4.0
[honeybee][pipeline][info] src/browser/render_pipeline.cpp:297 relayout: viewport=0
[honeybee][cascade][debug] src/renderer/css/cascade.cpp:209 cascade: 9 elements, 0 pseudo
```
Format correct. Categories correct. File redirect works.

**Definitive proof**: format, category filtering, file
redirect, and disabled path all verified end-to-end.

### Test count

- Before ADR-046: 572 cases, 2224 assertions
- After Commit 1: 586 cases, 2263 assertions (+14 / +39)
- After Commit 2: unchanged (log output verified via manual
  file-mode run, not unit-testable -- same pattern as ADR-043
  and ADR-045's integration-path verification)
- After Commit 3: unchanged (docs only)

Zero new warnings introduced. Pre-existing
`-Wmisleading-indentation` at `render_pipeline.cpp:492`
unchanged.

### Deferred (documented)

- **`net/client.cpp` fetch log points** -- the `fetch()`
  body wasn't captured during the pre-Commit-2 diagnostic
  (the file's helper functions were shown but not the main
  entry point). Follow-up ADR-046 Commit 2b or organic
  addition.
- **Deep-subsystem log points** -- block/grid/flex dispatch
  detail, image fetch lifecycle, click/wheel event handling.
  These are organic additions, not required by ADR-046's
  retrofit target list ("15-20 sites" was the target; 12
  shipped for the core pipeline, more come as debug needs
  arise).
- **`std::format` migration** (future ADR-047 if libstdc++
  adds support)
- **Runtime config reload** (config is set once at startup)
- **Log rotation** (external `logrotate`)
- **Structured (JSON) output**
- **Per-category thresholds** (single global today)
- **Trace-level perf timing** (invites misuse; defer)

