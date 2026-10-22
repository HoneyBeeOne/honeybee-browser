# HoneyBee Documentation

Quick index of the material in this directory.

## Guides

- [Getting Started](getting-started.md) -- build and run
- [Architecture](architecture.md) -- how the pieces fit together
- [Current State](current-state.md) -- what works today
- [Companions](companions.md) -- the ecosystem around the browser

## Decisions

[`decisions/`](decisions/) -- the Architecture Decision Records. They
read as a coherent design record; read them in order to follow the
design arc from "vendor a parser" to "render a page with CSS".
The index is in [`decisions/README.md`](decisions/README.md).

## Learning

[`learning/`](learning/) -- educational material on how browsers work.

## Recruitment

[`recruitment/`](recruitment/) -- hiring material.

## Manual test fixtures

Several HTML files in this directory are manual visual test pages:

- `dir-test.html` -- RTL and LTR text direction handling (ADR-016)
- `inline-test.html` -- inline formatting with styled spans (ADR-016)
- `inline-color-test.html` -- inline color inheritance (ADR-016)
- `wrap-test.html` -- word-level line breaking (ADR-015)
- `link-test-a.html`, `link-test-b.html`, `link-test-c.html` --
  cross-linked pages for verifying `:link`, `:visited`, and `:hover`
  rendering (ADR-018)

Serve them locally and point the browser at them:

```bash
python3 -m http.server 8000 --bind 127.0.0.1 --directory docs
```

Then navigate to `http://127.0.0.1:8000/<name>.html`. They verify
behavior that unit tests cannot cover -- Pango shaping for Arabic,
hover/visited link colors, whitespace collapse in inline flow. Each
was created alongside the ADR that introduced the behavior it
verifies; the ADR's Implementation section names them where relevant.
