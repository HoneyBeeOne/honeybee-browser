# ADR-001: Technology Stack

**Status:** Accepted
**Date:** 2026-09-20
**Deciders:** Amr Muhammad (maintainer)

## Context

HoneyBee Browser is a from-scratch web browser built by Cairo University students. We need a technology stack that:

1. Matches the skills students already have from their CU coursework
2. Enables contributors to start writing real browser code quickly
3. Produces skills that transfer directly to industry jobs
4. Is maintainable by a distributed student team
5. Can grow from a minimal browser to a multi-process architecture over time

## Decision

We will use:

| Component | Choice | Rationale |
|---|---|---|
| **Language** | C++20 | CU curriculum teaches C++. Industry-standard for browsers (Chromium, Firefox, WebKit all use C++). |
| **Build system** | CMake 3.20+ | De facto standard for C++ projects. Appears on every C++ job description. Cross-platform. |
| **Windowing / UI** | GTK 4 (gtkmm-4) | Linux-native. Clean C++ bindings. Lightweight. Standard for Linux-first projects. |
| **Networking** | libcurl | Battle-tested HTTP/1.1, HTTP/2, HTTPS. Saves months of re-implementation. |
| **HTML parsing** | lexbor (vendored) | Fast HTML5 parser with CSS selector support. Used by Nordstjernen browser. |
| **JavaScript** | QuickJS (vendored) | Small, embeddable ES2020 interpreter. No JIT (safer, simpler). |
| **Image decoding** | Wuffs (vendored) | Memory-safe image decoders. |
| **Rendering** | Cairo + Pango | Mature 2D graphics and text layout. GTK 4 native. |

## Alternatives considered

### Rust + winit + reqwest

**Rejected because:** Rust is not taught in CU's Computer Engineering curriculum. The ownership/borrowing learning curve would delay every new contributor by weeks. While Rust is the better long-term engineering choice, a project that 5 students can contribute to today beats one that 0 students can contribute to because they'd have to learn Rust first.

### C + GTK 4 (Nordstjernen approach)

**Rejected because:** C++ provides better abstraction (classes, RAII, templates) for a project of this scope. Nordstjernen's C approach works for a single-developer project with strict audit requirements; HoneyBee is a student community project where readability and onboarding speed matter more than minimal surface area.

## Consequences

- All source code in `src/` will be C++20.
- Build system is CMake. `CMakeLists.txt` at root and in subdirectories.
- GTK 4 is the UI toolkit. `gtkmm-4` provides C++ bindings.
- Third-party libraries are vendored in `third_party/` or found via CMake's `find_package`.
- Contributors must have a C++20 compiler (GCC 10+ or Clang 12+) and CMake 3.20+.
