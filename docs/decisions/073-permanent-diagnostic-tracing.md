# ADR-073: Permanent diagnostic tracing

**Status:** Implemented
**Date:** 2026-11-09
**Related:** ADR-046 (logging infrastructure), ADR-071 (async logger), ADR-072 (regression tests)

## Context

During the ADR-071 debugging session, we repeatedly added and removed `fprintf` traces to isolate the invisible-heading and blank-page-on-scroll bugs. Each add-remove cycle had costs:

1. **Perf regressions:** Synchronous `fprintf` during paint froze the compositor enough to introduce Heisenbugs (the blank-page symptom was actually caused by a diagnostic, not the bug being diagnosed, in one iteration).
2. **Wasted recompiles:** Each trace required a full rebuild, ~30-60 seconds, times dozens of cycles.
3. **Inconsistency:** Different traces used different formats, different categories, different log levels.
4. **The temptation to remove:** Every time we "finished" debugging, we deleted the traces — losing the ability to re-invoke them when a related bug appeared a few commits later.
5. **Hidden perf bugs:** Some traces constructed `std::string` arguments *outside* the `HB_LOG` guard, allocating on the hot path even when logging was disabled.

The user's explicit request during ADR-071 debugging: *"instead of removing the heavy logging, can we just add it as a level, that is enabled/disabled according to the value of the env. var."* This ADR formalizes that request.

## Decision

**Permanent `HB_LOG` traces remain in the codebase, guarded so they are truly zero-cost when disabled.**

### The rules

1. **Traces are never removed.** Once added, a trace stays. It gets re-classified (e.g., `Layout` vs `LayoutDetail`) or reformatted, but not deleted.

2. **Expensive argument construction is guarded by `log_enabled()`.**
   ```cpp
   // WRONG — allocates always, even when logging disabled
   std::string label = expensive_call();
   HB_LOG(LayoutDetail, Trace, "label=%s", label.c_str());

   // CORRECT — allocates only when logging enabled
   if (log_enabled(LogCategory::LayoutDetail, LogLevel::Trace)) {
       std::string label = expensive_call();
       HB_LOG(LayoutDetail, Trace, "label=%s", label.c_str());
   }
   ```

   The `HB_LOG` macro already guards the `log_writev` call, but the C++ standard does not guarantee argument evaluation is deferred past the guard. Any `std::string` construction, `get_attribute()`, `c_str()` on a temporary, or similar must be inside an explicit `log_enabled()` check.

3. **Categories follow a two-tier scheme:**
   - **Normal categories** (`Pipeline`, `Css`, `Cascade`, `Layout`, `Paint`, `Fetch`, `Event`): coarse-grained, one trace per phase boundary. Zero-cost argument evaluation is easy since args are primitives (ints, floats, enum values). Enable with `HONEYBEE_LOG=layout`.
   - **Detail categories** (`LayoutDetail`, `PaintDetail`): fine-grained, per-element or per-fragment. Args often require string construction. Enable with `HONEYBEE_LOG=layout_detail,paint_detail HONEYBEE_LOG_LEVEL=trace`.

   The default recommendation during debugging is *start with normal categories*, escalate to detail only when you need per-element granularity.

4. **Trace format is stable and greppable.** Every trace looks like:
   ```
   [honeybee][category][level] path:line message
   ```
   This is preserved by the `log_writev` implementation and tested in `test_log.cpp`.

5. **The async logger thread drains on exit.** `log_init()` starts a background thread that pops from a queue; `log_shutdown()` (hooked in `main.cpp`) flushes and joins. Both production and test paths are covered — `log_set_config()` forces synchronous mode for test determinism.

### Permanent traces added

| Trace point | Category | Purpose |
|---|---|---|
| `layout_element: enter <tag> id='...' display='...'` | LayoutDetail | Which elements are being laid out |
| `layout_element: EXIT <tag> id='...' x=... y=... w=... h=...` | LayoutDetail | Final computed geometry per element |
| `block_children: child <tag> display_enum=N` | LayoutDetail | Routing decision for each block child |
| `InlineContext created: y=... h=... text='...'` | LayoutDetail | Inline run boundaries + first text snippet |
| `paint_children: kind=N y=... h=...` | PaintDetail | Every child visited by paint |
| `CULLED Text/Inline (below)` | PaintDetail | Every cull decision |
| `painting InlineContext y=... h=... text='...'` | PaintDetail | Which InlineContexts actually paint |
| `LINE y=... text='...'` | Paint | Individual line fragment content |
| `FRAG: text='...' x=... y=... color=(...)` | Paint | Pango draw call for each fragment |
| `SCROLL: m_scroll_y=...` | Paint | Every scroll event |
| `on_draw: width=... viewport_h=...` | Layout | Every paint cycle entry |
| `sync: viewport_h=... scroll_y=...` | Layout | Every tetris sync |
| `PAINT START: visible_top=... visible_bottom=...` | Paint | Paint root entry with cull range |

Total: ~13 permanent trace points across 4 files. Zero runtime cost when `HONEYBEE_LOG` is unset.

## The float-wrap bug (discovered, deferred to ADR-074)

While setting up ADR-073's permanent traces, we discovered a **separate bug** unrelated to tracing or culling:

- Body `<h2>History` is placed at `y=2210` (default window) or `y=925` (maximized)
- Expected position: `y=500-800` (based on Firefox's rendering of the same page)
- **Root cause hypothesis:** Wikipedia's `float: right` infobox (with the HTML logo and Tim Berners-Lee photo) is not being wrapped correctly by the paragraph text beside it. Our layout engine likely renders the intro paragraphs at full width beside the float, then places the following block content below the float's full height — instead of wrapping text *inside* the float's vertical extent.

This is not a culling bug. The heading *is* painted when it enters the viewport — it just enters the viewport 1400px later than it should. Fix belongs in `layout_block_children`'s float handling (ADR-031 territory).

**Deferred to ADR-074.** Not in scope for ADR-073.

## What this ADR does NOT do

- **Does not fix the float-wrap layout bug.** That's ADR-074.
- **Does not add new trace categories beyond `LayoutDetail` / `PaintDetail`.** If future debugging needs `CascadeDetail`, `DomDetail`, etc., add them the same way.
- **Does not change the async logger architecture.** ADR-071's async thread remains; ADR-073 only documents the trace retention policy.

## References

- ADR-046 — logging infrastructure
- ADR-071 — async logger + dual-mode (sync for tests, async for production)
- ADR-072 — regression tests guarding ADR-071 fixes
- User request during ADR-071 debugging: *"instead of removing the heavy logging, can we just add it as a level"*
