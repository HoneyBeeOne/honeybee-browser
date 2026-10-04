// =============================================================
// File: document.cpp
// Purpose: Implementation of the lexbor HTML document wrapper.
//
// This is the only file in the project that includes lexbor
// headers. Everything else goes through document.hpp.
// =============================================================

#include "renderer/html/document.hpp"

#include <cstring>
#include <utility>

extern "C" {
#include <lexbor/html/parser.h>
#include <lexbor/html/interfaces/document.h>
#include <lexbor/html/interfaces/element.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/text.h>
}

namespace honeybee::renderer::html {

namespace {

// Recursively append the text of every text node under `node`.
void collect_text(lxb_dom_node_t* node, std::string& out) {
    if (node == nullptr) {
        return;
    }
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        auto* text = lxb_dom_interface_text(node);
        if (text->char_data.data.data != nullptr &&
            text->char_data.data.length > 0) {
            out.append(
                reinterpret_cast<const char*>(text->char_data.data.data),
                text->char_data.data.length);
        }
    }
    for (lxb_dom_node_t* child = node->first_child; child != nullptr;
         child = child->next) {
        collect_text(child, out);
    }
}

// Case-insensitive comparison between a lexbor tag name and a C string.
bool name_equals_ci(const lxb_char_t* name, size_t len, const char* target) {
    const size_t target_len = std::strlen(target);
    if (len != target_len) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        char a = static_cast<char>(name[i]);
        char b = target[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) {
            return false;
        }
    }
    return true;
}

// Depth-first search for the first element with the given tag name.
lxb_dom_node_t* find_element_by_name(lxb_dom_node_t* node, const char* name) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        auto* el = lxb_dom_interface_element(node);
        size_t name_len = 0;
        const lxb_char_t* local = lxb_dom_element_local_name(el, &name_len);
        if (local != nullptr && name_equals_ci(local, name_len, name)) {
            return node;
        }
    }
    for (lxb_dom_node_t* child = node->first_child; child != nullptr;
         child = child->next) {
        if (auto* found = find_element_by_name(child, name)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

Document::Document(lxb_html_document* doc) noexcept : doc_(doc) {}

Document::~Document() {
    if (doc_ != nullptr) {
        lxb_html_document_destroy(doc_);
        doc_ = nullptr;
    }
}

Document::Document(Document&& other) noexcept
    : doc_(std::exchange(other.doc_, nullptr)) {}

Document& Document::operator=(Document&& other) noexcept {
    if (this != &other) {
        if (doc_ != nullptr) {
            lxb_html_document_destroy(doc_);
        }
        doc_ = std::exchange(other.doc_, nullptr);
    }
    return *this;
}

std::unique_ptr<Document> Document::parse(std::string_view html) {
    lxb_html_document_t* doc = lxb_html_document_create();
    if (doc == nullptr) {
        return nullptr;
    }

    const lxb_status_t status = lxb_html_document_parse(
        doc,
        reinterpret_cast<const lxb_char_t*>(html.data()),
        html.size());

    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return nullptr;
    }

    return std::unique_ptr<Document>(new Document(doc));
}

std::string Document::title() const {
    if (doc_ == nullptr || doc_->head == nullptr) {
        return {};
    }
    lxb_dom_node_t* head = lxb_dom_interface_node(doc_->head);
    lxb_dom_node_t* title_el = find_element_by_name(head, "title");
    if (title_el == nullptr) {
        return {};
    }
    std::string out;
    collect_text(title_el, out);
    return out;
}

std::string Document::text_content() const {
    if (doc_ == nullptr || doc_->body == nullptr) {
        return {};
    }
    lxb_dom_node_t* body = lxb_dom_interface_node(doc_->body);
    std::string out;
    collect_text(body, out);
    return out;
}

} // namespace honeybee::renderer::html
