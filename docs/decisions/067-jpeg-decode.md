# ADR-067: JPEG decode via gdk-pixbuf

**Status:** Accepted
**Date:** 2026-11-03
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-034 (SVG decode + image paint), ADR-064 (scroll fix), ADR-066 (float intrinsic, revealed JPEG gap)

## Context

`RenderPipeline::get_image_surface` (render_pipeline.cpp:426)
currently dispatches on format magic bytes:

```cpp
// PNG magic: 89 50 4E 47
if (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
    surf = cairo_image_surface_create_from_png_stream(...);
}
// SVG (bytes contain "<svg" or start with "<?xml")
else if (bytes.find("<svg") != npos || bytes.rfind("<?xml", 0) == 0) {
    surf = rsvg_handle_new_from_data(...);
}
// else: nullptr
```

**No JPEG branch.** JPEG bytes fall through to `nullptr`, which paint
silently skips. No log, no error, no visible image.

### Measurement (ADR-067 diagnostic, 2026-11-03)

Instrumented `get_image_surface` fprintf on Wikipedia HTML article:

| URL | size | first4 | Format | Result |
|---|---|---|---|---|
| Wikipedia wordmark SVG | 9865 | `3c737667` | SVG | ✅ renders |
| Wikipedia tagline SVG | 15219 | `3c737667` | SVG | ✅ renders |
| **Tim Berners-Lee photo** | **16436** | **`ffd8ffe2`** | **JPEG** | **❌ null** |
| HTML5 logo thumbnail | 6427 | `89504e47` | PNG | ✅ renders |
| Category diagram thumbnail | 13298 | `89504e47` | PNG | ✅ renders |
| MediaWiki icons | 220-296 | `3c3f786d` | XML SVG | ✅ renders |
| Data URL icon | 215 | `3c737667` | SVG | ✅ renders |

**URL resolution works correctly.** The Tim photo's URL
`//thumb.wikimedia.org/...` correctly resolved to `https://thumb.wikimedia.org/...`
and 16436 bytes were successfully fetched. The bug is purely
format dispatch — no JPEG decoder.

## Decision

Add JPEG decode branch using gdk-pixbuf, which is:
- Already available system-wide (2.42.10)
- Transitively linked via GTKMM4 (`pkg_check_modules(GTKMM4 REQUIRED IMPORTED_TARGET gtkmm-4.0)` at CMakeLists.txt:47)
- Battle-tested decoder (libjpeg-turbo under the hood)

### The fix (~30 lines in render_pipeline.cpp)

Include `<gdk/gdk.h>` (declares `gdk_cairo_surface_create_from_pixbuf`)
and `<gdk-pixbuf/gdk-pixbuf.h>` (declares `gdk_pixbuf_new_from_stream`).

After the PNG branch, before the SVG branch:

```cpp
// JPEG magic: FF D8 FF (covers JFIF, EXIF, raw)
else if (bytes.size() >= 3 &&
         static_cast<unsigned char>(bytes[0]) == 0xFF &&
         static_cast<unsigned char>(bytes[1]) == 0xD8 &&
         static_cast<unsigned char>(bytes[2]) == 0xFF) {
    GInputStream* stream = g_memory_input_stream_new_from_data(
        bytes.data(), static_cast<gssize>(bytes.size()), nullptr);
    GError* err = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream(stream, nullptr, &err);
    g_object_unref(stream);
    if (err) { g_error_free(err); return nullptr; }
    if (!pixbuf) return nullptr;
    surf = gdk_cairo_surface_create_from_pixbuf(pixbuf, 0, nullptr);
    g_object_unref(pixbuf);
    if (surf && cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        surf = nullptr;
    }
}
```

## Design decisions

### Why gdk-pixbuf, not raw libjpeg

- **gdk-pixbuf is already linked** via GTK4 → GTKMM4. No new
  build dependency, no CMakeLists change.
- **gdk_cairo_surface_create_from_pixbuf** produces exactly the
  Cairo surface format paint expects (`cairo_surface_t*`).
- **Handles PNG, JPEG, GIF, WebP, TIFF** uniformly. If we hit
  another format in the future (GIF, WebP from Wikimedia), same
  API works. No format-specific code.
- **libjpeg direct** would need `#include <jpeglib.h>`, manual
  `jpeg_read_header` / `jpeg_start_decompress` / memory
  management / stride handling. ~60 lines and error-prone.
- **gdk-pixbuf is the standard answer** for GTK apps decoding
  raster images. It's what every GTK image viewer uses.

### Why memory-stream, not file

`bytes` is already a `std::string` in memory. Writing to a temp
file to read back via `gdk_pixbuf_new_from_file` would add disk
I/O for no benefit. `g_memory_input_stream_new_from_data` gives
gdk-pixbuf a zero-copy view of the existing buffer.

**Safety note**: `g_memory_input_stream_new_from_data` with
`destroy_fn = nullptr` means the stream does NOT own the data.
But `bytes` is a local `std::string` and `gdk_pixbuf_new_from_stream`
is synchronous (reads before returning), so `bytes` outlives the
decode call. Safe.

### JPEG magic — 3 bytes not 4

Real-world JPEG headers:
- `FF D8 FF E0` — JFIF (baseline / progressive)
- `FF D8 FF E1` — EXIF
- `FF D8 FF E2` — other marker (the Tim photo)
- `FF D8 FF DB` — raw quantization table

Three bytes (`FF D8 FF`) matches all valid JPEG. Fourth byte
distinguishes variant but doesn't matter for decode dispatch.

### No new CMake link needed (verified)

`pkg_check_modules(GTKMM4 REQUIRED IMPORTED_TARGET gtkmm-4.0)`
already pulls in gtk4, gdk-4, and gdk-pixbuf-2.0 as transitive
dependencies. Their headers and symbols are in the include/library
paths the compiler uses.

**If build fails with "gdk_cairo_surface_create_from_pixbuf not
found"**: add `pkg_check_modules(GDKPIXBUF REQUIRED IMPORTED_TARGET
gdk-pixbuf-2.0)` to CMakeLists.txt and link it explicitly. Not
expected — GTKMM4 does this transitively.

## Deferred

- **GIF / WebP / TIFF** — gdk-pixbuf supports them but no
  current consumer needs them. Same branch would work if needed
  (just extend the magic-byte check or remove format dispatch
  entirely and let gdk-pixbuf sniff).
- **Format auto-detection** — future ADR could drop the
  manual magic-byte dispatch entirely and route **every**
  non-SVG through gdk-pixbuf (it handles PNG too). V1 keeps
  explicit branches for clarity.
- **Image resize on decode** — gdk-pixbuf has `_at_scale`
  variants. Not needed; paint layer's `cairo_scale` handles
  sizing (ADR-034 behavior).

## Consequences

**Better:**
- **Tim Berners-Lee photo renders** (was blank space since ADR-054)
- **All JPEG images from any source render** (not just Wikipedia)
- **Wikimedia raster thumbnails work** — most of what Wikipedia
  serves for `/thumb/` paths
- **ADR-066's figure float now paints** — photo is visible,
  float narrows text beside it (no more empty band)

**Accepted limitations:**
- **GIF / WebP / TIFF** — no current consumer needs them
- **EXIF orientation not applied** — gdk-pixbuf reads raw
  pixels; images with rotation EXIF render rotated wrong.
  Not needed for Tim photo (correctly oriented at source).
  Separate concern if it comes up.

**Unchanged:**
- ADR-005 through ADR-066 behavior
- PNG and SVG decode paths
- Layout, cascade, paint hierarchy

**Possible regression risks (verify in Commit 1):**
- **Memory safety**: buffer lifetime is safe (synchronous decode)
- **gdk-pixbuf availability**: transitive link should just
  work — build verifies
- **Existing SVG/PNG tests**: unaffected (dispatch is
  format-specific)

## Verification

**Fixture** (`docs/adr067-jpeg-test.html`): data-URL JPEG at
100x80, `<img>` with explicit width=100 height=80.

**Unit test**: none (no `get_image_surface` test infrastructure;
same as ADR-034's SVG decoder shipped without unit test).

**Production verification**: user screenshot of Wikipedia
HTML article. Tim Berners-Lee photo should appear (was empty
band). Compare to prior screenshot where band was blank.

**Regression check**: PNG + SVG images continue to render
(screenshots already show them working; re-verify visually).

## Acceptance criteria

- [ ] JPEG magic check + gdk-pixbuf decode branch added
- [ ] `<gdk/gdk.h>` + `<gdk-pixbuf/gdk-pixbuf.h>` included
- [ ] ADR-067-DIAG fprintf removed
- [ ] Build clean (no CMake change expected)
- [ ] 691 / 2641 tests still pass
- [ ] Wikipedia screenshot: Tim Berners-Lee photo appears
- [ ] PNG / SVG images still render (no regression)
- [ ] Fixture `docs/adr067-jpeg-test.html` with data-URL JPEG
- [ ] README row + CHANGELOG
- [ ] ADR-067 status → Implemented

## Implementation

Shipped on `feat/jpeg-decode` at 2026-11-03.

### What shipped

`render_pipeline.cpp`:
- Added includes: `<gdk-pixbuf/gdk-pixbuf.h>` (NOT `<gdk/gdk.h>` -- see layering note)
- JPEG magic-byte branch (3 bytes `FF D8 FF`, covers JFIF / EXIF / raw)
  inserted between PNG and SVG dispatch
- Decode via `gdk_pixbuf_new_from_stream`
- Manual RGBA->ARGB32 pixel copy with R<->B channel swap
- Removed `ADR-067-DIAG` fprintf

`docs/adr067-jpeg-test.html`:
- Fixture referencing the same Wikimedia Tim Berners-Lee JPEG URL

### The layering finding (honest correction to the plan)

The ADR-067 spec's plan said to use `gdk_cairo_surface_create_from_pixbuf`
from `<gdk/gdk.h>` (GDK4). **Build failed**: `gdk/gdk.h: No such file
or directory`.

Reason: `honeybee_lib` (where `render_pipeline.cpp` lives) links gdk-pixbuf
transitively via GTKMM4's `pkg_check_modules(..., gtkmm-4.0)` -- but does NOT
link GTK4/GDK4. Only the `honeybee` **executable** links GTK4. The test
binary builds without GTK. This is correct architecture; the fix had to
respect it.

Corrected approach: gdk-pixbuf yields RGBA; cairo `CAIRO_FORMAT_ARGB32` on
little-endian expects BGRA in memory. ~40 lines of manual pixel copy with an
explicit R<->B swap. Keeps `honeybee_lib` GTK-free.

**Channel swap is critical**: without it, JPEGs would render with red/blue
swapped (pink faces, blue fires). gdk-pixbuf RGBA is R G B A; cairo's ARGB32
LE expects B G R A. The code explicitly swaps bytes 0 and 2.

### What the diagnostic proved

ADR-067 diagnostic instrumented `get_image_surface`. Output on Wikipedia:

- **Tim Berners-Lee photo**: `first4=ffd8ffe2` (JPEG magic), 16436 bytes,
  URL `//thumb.wikimedia.org/...` correctly resolved to
  `https://thumb.wikimedia.org/...`
- **HTML5 logo**: `first4=89504e47` (PNG), 6427 bytes, renders via Cairo PNG decoder
- **Category diagram**: `first4=89504e47` (PNG), renders
- **Wikipedia wordmark/tagline**: `first4=3c737667` (SVG), renders via librsvg

Format dispatch was the sole gap. PNG + SVG paths already worked.

### Verified delta (pending user screenshot)

Build clean, 691 / 2641 tests unchanged. The JPEG path is new code
unexercised by tests -- acceptance criterion is the Wikipedia screenshot.

**Expected on Wikipedia HTML article History section**: Tim Berners-Lee
photo now renders inside the ~220px figure band (was blank space per
ADR-066's screenshot). If not, we have a channel-swap or decoder bug.

