// =============================================================
// Smoke test: prove that lexbor is linked and can parse a
// minimal HTML string.
//
// This test is intentionally trivial. Its only job is to verify
// that:
//   1. lexbor's headers are reachable from our build.
//   2. lexbor_static is linked correctly.
//   3. The library can parse a document and expose a DOM tree.
//
// Once this passes, Phase 1 (a real parser wrapper) can begin.
// =============================================================

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

extern "C" {
#include <lexbor/html/parser.h>
#include <lexbor/html/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/text.h>
}

namespace {

// Recursively collect the text content of a node and its siblings.
std::string collect_text(lxb_dom_node_t *node) {
    std::string out;
    while (node != nullptr) {
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            auto *text = lxb_dom_interface_text(node);
            out.append(
                reinterpret_cast<const char *>(text->char_data.data.data),
                text->char_data.data.length);
        } else if (node->first_child != nullptr) {
            out += collect_text(node->first_child);
        }
        node = node->next;
    }
    return out;
}

} // namespace

TEST_CASE("lexbor parses a minimal HTML document", "[lexbor][smoke]") {
    const char *html = "<html><body><h1>Hello</h1></body></html>";
    const auto  len  = std::strlen(html);

    lxb_html_document_t *doc = lxb_html_document_create();
    REQUIRE(doc != nullptr);

    const lxb_status_t status = lxb_html_document_parse(
        doc,
        reinterpret_cast<const lxb_char_t *>(html),
        len);
    REQUIRE(status == LXB_STATUS_OK);

    lxb_dom_node_t *body = lxb_dom_interface_node(doc->body);
    REQUIRE(body != nullptr);

    const std::string text = collect_text(body);
    REQUIRE(text.find("Hello") != std::string::npos);

    lxb_html_document_destroy(doc);
}
