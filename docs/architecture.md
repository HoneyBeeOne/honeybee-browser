# HoneyBee Browser — Architecture

**Status:** Living document
**Last updated:** 2026-09-20
**Related:** [ADR-001: Technology Stack](decisions/001-tech-stack.md)

This document describes the high-level architecture of HoneyBee Browser. It is written for contributors who want to understand how the pieces fit together before reading or writing code.

## Guiding principles

1. **A working subset beats an unfinished superset.** We ship vertical slices that run, not horizontal layers that don't.
2. **Every task ends in something runnable.** No PR is merged unless the build succeeds and the browser starts.
3. **Vendor hard problems, write browser logic.** HTML parsing, JavaScript interpretation, and image decoding are solved problems. We vendor proven libraries and write the browser-specific parts ourselves.
4. **Single process first, multi-process later.** Isolation is a Phase 4 concern. Phase 1 is single-process and simple.

## High-level architecture

The browser is organized into these components, from outer to inner:

    ┌─────────────────────────────────────────────────────────────┐
    │                      Browser Shell (UI)                     │
    │  Window · Tabs · URL bar · Navigation buttons · Status bar  │
    │                    GTK 4 (gtkmm-4)                          │
    └────────────────────────────┬────────────────────────────────┘
                                 │
    ┌────────────────────────────▼────────────────────────────────┐
    │                    Browser Engine Core                      │
    │                                                             │
    │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐   │
    │  │Networking│  │  HTML    │  │  CSS     │  │ JavaScript │   │
    │  │(libcurl) │─▶│ Parser   │─▶│ Engine   │─▶│ (QuickJS)  │   │
    │  │          │  │ (lexbor) │  │          │  │            │   │
    │  └──────────┘  └────┬─────┘  └────┬─────┘  └─────┬──────┘   │
    │                     │             │              │          │
    │                     ▼             ▼              ▼          │
    │              ┌─────────────────────────────────────────┐    │
    │              │            DOM + CSSOM Tree             │    │
    │              └──────────────────┬──────────────────────┘    │
    │                                 │                           │
    │                                 ▼                           │
    │              ┌─────────────────────────────────────────┐    │
    │              │          Layout Engine                  │    │
    │              │   Box model · Block/inline flow · Flex  │    │
    │              └──────────────────┬──────────────────────┘    │
    │                                 │                           │
    │                                 ▼                           │
    │              ┌─────────────────────────────────────────┐    │
    │              │         Paint / Compositor              │    │
    │              │         (Cairo + Pango)                 │    │
    │              └─────────────────────────────────────────┘    │
    └─────────────────────────────────────────────────────────────┘

## Component descriptions

### Browser Shell (UI)

The GTK 4 application window. Owns tabs, the URL bar, navigation buttons, and the status bar. Communicates with the engine core through a thin interface. In Phase 1, this is a single window with a single tab.

**Source:** `src/browser/`

### Networking

Fetches resources over HTTP/HTTPS using libcurl. Handles redirects, cookies, and connection pooling. Exposes a simple API:

    Response fetch(const Url& url);

**Source:** `src/net/`

### HTML Parser

Uses lexbor to tokenize and parse HTML5 into a DOM tree. lexbor handles the WHATWG spec compliance; our code wraps it in a C++ interface.

**Source:** `src/renderer/html/`

### CSS Engine

Parses CSS, builds the CSSOM, and computes the cascade. Selector matching is our own code — this is a browser-specific problem worth writing.

**Source:** `src/renderer/css/`

### JavaScript

Embeds QuickJS to execute page scripts. Exposes DOM bindings so scripts can manipulate the page.

**Source:** `src/renderer/js/`

### DOM + CSSOM

The combined tree that represents the parsed document and its computed styles. Lives in memory while a page is loaded.

**Source:** `src/renderer/dom/`

### Layout Engine

Computes the position and size of every box in the render tree. Implements the CSS box model, block/inline flow, and (later) flexbox.

**Source:** `src/renderer/layout/`

### Paint / Compositor

Rasterizes the layout tree onto the screen using Cairo for drawing and Pango for text. Later phases add compositing for GPU acceleration.

**Source:** `src/renderer/paint/`

### IPC (Phase 4)

Inter-process communication between the browser process and renderer processes. Not implemented in Phase 1. The architecture is designed so this can be added later without rewriting the engine.

**Source:** `src/ipc/` (created in Phase 4)

## Process model

**Phase 1–3:** Single process. The browser shell and the renderer run in the same process. This is simpler, faster to develop, and sufficient for a minimal browser.

**Phase 4:** Multi-process. Each tab gets its own renderer process. The browser process manages windows and IPC. This provides crash isolation (one tab crashing doesn't kill the browser) and security (renderers are sandboxed).

Chromium's process model documentation is the reference for how multi-process browsers are structured.

## Directory structure

    honeybee-browser/
    ├── CMakeLists.txt              # Root build file
    ├── src/
    │   ├── CMakeLists.txt          # Source build file
    │   ├── main.cpp                # Entry point
    │   ├── browser/                # UI shell (GTK 4)
    │   ├── net/                    # Networking (libcurl)
    │   ├── renderer/
    │   │   ├── html/               # HTML parsing (lexbor)
    │   │   ├── css/                # CSS engine
    │   │   ├── js/                 # JavaScript (QuickJS)
    │   │   ├── dom/                # DOM + CSSOM
    │   │   ├── layout/             # Layout engine
    │   │   └── paint/              # Paint / compositor
    │   ├── ipc/                    # IPC (Phase 4)
    │   └── base/                   # Utilities
    ├── third_party/                # Vendored libraries
    ├── tests/
    ├── docs/
    └── assets/

## What we are NOT building

- **Our own HTTP stack.** libcurl handles it.
- **Our own HTML parser.** lexbor handles it.
- **Our own JavaScript engine.** QuickJS handles it.
- **Our own image decoders.** Wuffs handles it.
- **Our own text rendering.** Pango handles it.

We build the browser: the CSS cascade, the layout engine, the paint pipeline, the DOM bindings, the multi-process model, and the UI. These are the browser-specific problems worth solving.

## Further reading

- [How Browsers Work (Web.dev)](https://web.dev/articles/howbrowserswork)
- [MDN: How browsers work](https://developer.mozilla.org/en-US/docs/Web/Performance/How_browsers_work)
- [Chromium Process Model](https://www.chromium.org/developers/design-documents/process-models)
- [Nordstjernen Browser](https://github.com/nordstjernen-web/nordstjernen-browser)
