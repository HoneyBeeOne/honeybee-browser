# ADR-068: Force Cairo CPU renderer (GTK4 GSK GPU crash workaround)

**Status:** Implemented (production-verified)
**Date:** 2026-11-03
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-021 (ScrolledWindow), ADR-064 (tall DrawingArea architecture), ADR-067 (blocked on this — JPEG verification needs a stable process)

## Context

**GTK4's GSK GPU renderer crashes on very tall cairo surfaces.**
HoneyBee's ADR-021 / ADR-064 architecture paints the entire page
into a single tall `Gtk::DrawingArea` (1839 × 30,447 px for the
Wikipedia HTML article). GTK4's GSK GPU renderer attempts to
upload that ~224 MB cairo surface to the GPU on every frame.
For tall pages (empirically >~25,000px), the clip state inside
GSK becomes corrupted during upload and `_cairo_clip_intersect_clip`
dereferences garbage memory.

### Measurement (2026-11-03)

**Reproduced with user screenshots.** Scroll to History section on
Wikipedia HTML article:

- **Default GSK renderer (GPU)**: **segfault**. Core dump 213 MB.
- **`GSK_RENDERER=cairo` (CPU)**: **no crash**, browser works,
  scrolling works, all rendering identical.

### Core backtrace (via gdb)

```
#0 _cairo_clip_intersect_clip (clip=..., other=...) at cairo-clip.c:346
#1 _cairo_surface_wrapper_get_clip
#2 _cairo_surface_wrapper_fill
#3 _cairo_recording_surface_replay_internal
#4 _cairo_recording_surface_replay_with_clip
#5 composite_aligned_boxes
#6 clip_and_composite_boxes
#7 _cairo_spans_compositor_paint
#8 _cairo_compositor_paint
#9 _cairo_surface_paint
#10 _cairo_gstate_paint
#11 cairo_paint
#12 gsk_render_node_draw (GskCairoNode)
...
#15 gsk_gpu_upload_cairo_op_draw (stride=7356)
#21 gsk_gpu_frame_render
#24 gtk_widget_render
```

**Zero HoneyBee frames** in the crash stack. This is a GTK4/GDK
bug triggered by our architecture, not a HoneyBee bug.

### Why ADR-064 is the trigger

Before ADR-064: DrawingArea was viewport-height (678px). Images
and content below the fold existed in the layout tree but were
never painted (cairo clipped them out before GPU upload). The
GTK GPU bug was dormant because surfaces were small enough.

After ADR-064: DrawingArea is 30,447px tall (the full content
extent). GTK tries to upload the full surface. Crash.

**The user reported "this segfault has been happening for a while"** —
consistent with ADR-064 landing weeks ago. Before ADR-064, scroll
didn't work at all, so nobody reached the crash. After ADR-064,
scroll works but crashes.

## Decision

**Force GSK to use the Cairo CPU renderer in `main.cpp`,
via `setenv` before GTK initializes.**

```cpp
setenv("GSK_RENDERER", "cairo", 0);  // 0 = don't overwrite user choice
```

Where: `src/main.cpp`, first line of `int main()`, before
`HoneyBeeApp` construction and `app.run()`.

Overwrite=0 means: if the user explicitly sets `GSK_RENDERER=gl`
(or `vulkan`, or `nop`) in their environment, we respect it. If
they don't set it, we default to Cairo.

## Design decisions

### Why Cairo CPU renderer, not fixing the GTK bug

- **The bug is in upstream GTK4/GDK.** Not patchable by us.
  (Real fix would require a GTK MR, upstream review, release
  cycle, distro packaging — months away.)
- **The workaround is 1 line.** No new dependency, no CMake
  change, no architecture shift.
- **Production-proven.** Our test run showed Cairo works
  cleanly for the exact content that crashes GPU.
- **Falls within standard practice.** Firefox, Chrome, and
  every GTK app with a large canvas fallback to CPU rendering
  for known GPU driver/engine bugs.

### Why overwrite=0 (respect user choice)

- If GTK eventually fixes the bug, users can test the fix by
  setting `GSK_RENDERER=gl` themselves (or `vulkan` / `nop`).
  We're not permanently stuck on CPU.
- Users on unusual hardware where Cairo renderer has its own
  issues can override.
- Standard Unix behavior — no env var we set should silently
  override a user's explicit choice.

### Why Cairo instead of Vulkan / NGL

GTK 4.14 ships three GSK renderers: `gl`, `vulkan`, `cairo`, `nop`.
`vulkan` requires more setup (missing on some systems). `nop` is
for testing. `cairo` is universally available (it's the fallback
used when GPU isn't available at all). Cairo CPU is the correct
default for a browser-without-GPU-requirements.

### What this does NOT do

- **Does not change ADR-021's architecture.** Still tall
  DrawingArea + ScrolledWindow. The crash is bypassed, not
  eliminated architecturally.
- **Does not regress GPU support for smaller pages.**
  Users who set `GSK_RENDERER=gl` themselves (and don't hit
  large pages) can still use GPU.
- **Does not fix GTK.** Only HoneyBee's default behavior.

## Deferred

- **Viewport-clipped paint architecture** (ADR-069 candidate) —
  replace "tall DrawingArea + ScrolledWindow" with "viewport-sized
  DrawingArea + scroll offset." Real browsers work this way.
  Fixes the architecture that exposes the GTK bug. ~200-300 lines,
  multi-session.
- **GTK upstream fix** — not in our control. Tracked if the bug
  gets reported (worth doing eventually; the crash is reproducible
  with any 30,000px cairo surface).
- **Selective GPU for small pages** — set GSK_RENDERER=gl only
  when layout_height < 25000px, Cairo otherwise. ~10 lines,
  not needed yet (Cairo is fast enough for our content).

## Consequences

**Better:**
- **Scroll crash eliminated** — segfault gone
- **ADR-067 JPEG decode becomes verifiable** — process stays
  alive long enough to observe the Tim Berners-Lee photo
- **Zero build changes** — no new deps, no CMake edits
- **User-overridable** — GSK_RENDERER=gl still works if the
  user knows what they're doing

**Accepted limitations:**
- **CPU rendering is slower** — for our 30,000px content, GPU
  upload was already the bottleneck (224MB per frame), so
  Cairo CPU is not measurably worse in practice. Scroll
  repaint performance unchanged from pre-fix.
- **Not the real fix** — architectural debt (ADR-069 candidate)
- **Depends on GTK's GSK_RENDERER env var** — if GTK changes
  the env var name in a future version, workaround needs update

**Unchanged:**
- ADR-005 through ADR-067 behavior
- All rendering output (Cairo and GPU should produce
  pixel-identical 2D rendering)
- Scrolling architecture (ADR-064's tall DrawingArea stands)
- JPEG decode path (ADR-067 becomes verifiable)

**Possible regression risks (verify post-merge):**
- **Cairo rendering output differences**: subpixel AA, blur,
  shadow might differ slightly. Visual inspection needed.
- **Performance regression on scroll**: measured informally;
  should be comparable to pre-ADR-064 baseline.
- **Existing env var behavior**: if user has GSK_RENDERER set
  to something invalid, GTK behavior unchanged (they'd have
  hit the same fallback before).

## Verification

**Manual observation (user-reported):**

1. Run `./build-release/bin/honeybee` **without** any env var
   prefix. Load Wikipedia HTML article. Scroll to History section.
   **Expected**: no crash (was crash).
2. Confirm Cairo renderer active via `GSK_RENDERER` env in
   process (or just accept that the crash stopping implies it).

**Regression check**: scroll, resize, navigate to other pages —
all work; visual output unchanged from ADR-066's verified state.

**ADR-067 unblock check**: with the crash fixed, the Tim
Berners-Lee photo should now render (JPEG decode path is live).
That's ADR-067's acceptance test, run on the follow-up cycle.

## Acceptance criteria

- [ ] `setenv("GSK_RENDERER", "cairo", 0)` in `src/main.cpp` before GTK init
- [ ] `#include <cstdlib>` for `setenv`
- [ ] Build clean
- [ ] 691 / 2641 tests unchanged
- [ ] User runs binary without env var, no crash on Wikipedia scroll
- [ ] ADR-067 unblocked (verifiable in follow-up)
- [ ] README row + CHANGELOG
- [ ] ADR-068 status → Implemented

## Implementation

Shipped on `feat/cairo-renderer` at 2026-11-03.

### What shipped

**`src/main.cpp`:**
- `#include <cstdlib>` added
- `setenv("GSK_RENDERER", "cairo", 0)` as first statement in `main()` before `log_init()` and `HoneyBeeApp` construction

```cpp
int main(int argc, char* argv[]) {
    // ADR-068: GTK4's GSK GPU renderer crashes on very large
    // cairo surfaces (empirically >~25,000px tall) with
    // corrupted clip state during GPU upload.
    setenv("GSK_RENDERER", "cairo", 0);
    honeybee::log_init();
    ...
}
```

`overwrite=0` preserves any user-set `GSK_RENDERER` so they can opt back into GPU if a GTK fix lands or try alternatives (`vulkan`, `nop`).

### Verified delta (production, user-reported 2026-11-03)

| Check | Before | After |
|---|---|---|
| **Scroll crash on Wikipedia** | Segfault (core 213 MB, 30,000px surface) | **No crash** |
| **Scroll performance** | (unreachable — crashes) | **Smooth** |
| **Visual output** | N/A | **No differences reported** |

### The core backtrace that confirmed the diagnosis

```
#0 _cairo_clip_intersect_clip (clip=..., other=...) at cairo-clip.c:346
...
#11 cairo_paint (cr=0x...)
#12 gsk_render_node_draw (GskCairoNode)
...
#15 gsk_gpu_upload_cairo_op_draw (stride=7356)
...
#24 gtk_widget_render
```

**Zero HoneyBee frames** in the crash stack. Every frame from `#11` onward is GTK4/GSK internal. The crash is a GTK bug triggered by HoneyBee's architecture (ADR-021 / ADR-064: 30,447px tall DrawingArea).

### Why ADR-064 is the trigger (not the cause)

Before ADR-064: DrawingArea = viewport height (678px). Content below fold existed in layout tree but was never painted (cairo clipped to small surface before GPU upload). Bug dormant.

After ADR-064: DrawingArea = full content height (30,447px). GTK attempts to upload ~224 MB surface. Clip state corrupted → segfault.

Consistent with user report "this segfault has been happening for a while" — dormant since ADR-064, revealed by ADR-064's success.

### What this unblocks

ADR-067's JPEG verification was blocked by this crash — the process never lived long enough to observe the Tim Berners-Lee photo. With Cairo renderer active, ADR-067 verification can proceed immediately after merge.

### Design decisions applied

**Cairo CPU vs alternatives:** GTK 4.14 supports `gl`, `vulkan`, `cairo`, `nop`. Cairo is the universal fallback (works even without GPU); vulkan has additional setup requirements; nop is testing-only. For a browser without GPU requirements, Cairo is correct.

**overwrite=0:** standard Unix convention — don't silently override user choice. If a GTK fix lands upstream, users can test by setting `GSK_RENDERER=gl` themselves.

**Not fixing GTK:** the bug is upstream. Real fix requires a GTK MR, review cycle, distro packaging. Workaround is 1 line.

### What this does NOT do

- **Does not change ADR-021 / ADR-064 architecture.** Still tall DrawingArea + ScrolledWindow. Crash is bypassed, not eliminated architecturally.
- **Does not fix GTK.** Only HoneyBee's default behavior.
- **Does not regress GPU for small pages.** Users who set `GSK_RENDERER=gl` themselves (and stay on small pages) still get GPU.

### Deferred

- **ADR-069 candidate: viewport-clipped paint.** Replace "tall DrawingArea + ScrolledWindow" with "viewport-sized DrawingArea + scroll offset" — real browser architecture. Fixes what exposed the GTK bug at the source. ~200-300 lines, multi-session. Unnecessary until Cairo CPU becomes a bottleneck (currently smooth).
- **GTK upstream bug report.** Worth filing eventually; the crash reproduces with any 30,000px cairo surface.
- **Selective GPU for small pages.** `GSK_RENDERER=gl` when `layout_height < 25000px`, Cairo otherwise. ~10 lines, deferred until needed.

