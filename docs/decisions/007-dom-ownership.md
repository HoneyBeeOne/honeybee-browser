# ADR-007: DOM Ownership Model

**Status:** Accepted
**Date:** 2026-10-04
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-001 (technology stack), ADR-006 (return to ADR-001)

## Context

After ADR-006, we are building a real rendering pipeline from the ground up. The first layer above the HTML parser is the **Document Object Model** (DOM).

We now have lexbor as the HTML parser (Phase 0, Phase 1). lexbor produces its own tree (`lxb_html_document_t` with `lxb_dom_node_t` children). The question this ADR answers is:

**What is our DOM's relationship to lexbor's tree?**

Two approaches are on the table:

### Approach A: Borrow from lexbor

Our `Node`, `Element`, and `TextNode` classes are thin type-safe wrappers holding pointers into lexbor's tree. We never copy anything. Our DOM is a view over lexbor.

### Approach B: Own the tree

After parsing, we walk lexbor's tree once and construct our own nodes. Our nodes own their own children (via `std::unique_ptr<Node>`), their own text (`std::string`), and their own attributes (`std::unordered_map<std::string, std::string>`). Once conversion is complete, lexbor's document can be destroyed.

## Decision

**We own the tree.** Our DOM is fully independent of lexbor. lexbor's only role is to parse HTML bytes into a tree that we then translate into our own representation.

## Rationale

### 1. Long-term architecture

Every production browser (Chromium, Firefox, WebKit) owns its DOM. The HTML parser is a producer. When the parser and the DOM are the same thing, you are architecturally stuck — every future addition (styles, layout, scripting) has to fight the parser's data model.

### 2. Extension fields

We will attach non-HTML data to nodes in later phases:

- **Phase 4:** computed styles (font-size, color, margin, display)
- **Phase 5:** layout boxes (x, y, width, height)
- **Phase 6:** paint commands and clipping regions
- **Future:** event listeners, focus state, JavaScript bindings

lexbor's node structs are fixed-size C structs. They cannot be extended. Our own node classes can carry whatever fields we need.

### 3. Stable lifetime

Our DOM must outlive lexbor's document. If we ever implement DOM mutation (e.g., from script, or from a future dev tools panel), the tree must be designed for it. lexbor's tree is designed for parsing, not for arbitrary runtime modification.

### 4. Decoupling

After Phase 3, only `renderer/html/*.cpp` includes lexbor headers. The rest of the renderer talks to our DOM only. This is the same isolation pattern we established in Phase 1 with `Document`.

### 5. Memory cost is acceptable

Our DOM duplicates the tree structure and text content. For a typical HTML page (say, 500 KB of markup), that's on the order of 1–5 MB of extra memory. Negligible for a desktop browser.

## Alternatives Considered

### Borrow from lexbor

**Rejected.** It couples the entire renderer to lexbor's data layout, prevents per-node extension, and makes any future mutation require a rewrite. The memory savings are not worth the architectural cost.

### Hybrid: own nodes, borrow text

**Rejected.** This is the worst of both worlds. Text is the largest memory component, so we'd save little. And we'd still have lifetime coupling: the `std::string_view`s pointing into lexbor's text would become dangling if lexbor's document were destroyed.

## Consequences

- Our DOM owns memory via `std::unique_ptr<Node>` children.
- Text nodes store their content as `std::string`.
- Elements store tag name as a `std::string` and attributes as `std::unordered_map<std::string, std::string>`.
- A conversion function (in `renderer/html/`) walks lexbor's tree once and produces our DOM.
- The `renderer::html::Document` type — currently a wrapper that exposes `title()` and `text_content()` — is refactored so that `renderer::html` becomes a *producer* of DOM documents, not a document type itself.
- Callers (e.g., `window.cpp`, `preview.cpp`) switch to using the new `renderer::dom::Document` type.

## Open Question: CSS Selector Matching

lexbor ships a selectors module that operates on lexbor's tree. Once we own our DOM, we have two options for CSS matching in Phase 4:

1. **Convert back to lexbor on demand** for each selector query. Slow, complex, and defeats the decoupling.
2. **Implement selectors ourselves in Phase 4.** More work, but we need the logic anyway, and it aligns with ADR-006's philosophy of building the interesting parts ourselves.

**Recommendation:** implement selectors ourselves. This is deferred to a future ADR for the CSS engine, but noted here for context.

## Record of Changes

- **2026-10-04:** Initial decision.
