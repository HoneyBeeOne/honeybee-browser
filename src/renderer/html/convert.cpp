// =============================================================
// File: convert.cpp
// Purpose: lexbor -> owned DOM converter.
//
// This is the ONLY file in the project that includes lexbor
// headers (per ADR-007). The rest of the renderer talks to our
// owned DOM only.
// =============================================================

#include "renderer/html/convert.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "renderer/dom/node.hpp"

extern "C" {
#include <lexbor/html/parser.h>
#include <lexbor/html/interfaces/document.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/text.h>
#include <lexbor/dom/interfaces/attr.h>
}

namespace honeybee::renderer::html {

namespace {

std::string read_lexbor_str(const lxb_char_t* data, size_t len) {
    if (data == nullptr || len == 0) return {};
    return std::string(reinterpret_cast<const char*>(data), len);
}

std::unique_ptr<dom::Node> convert_node(lxb_dom_node_t* src);

void convert_children(lxb_dom_node_t* src_parent, dom::Node& dst_parent) {
    for (lxb_dom_node_t* child = src_parent->first_child;
         child != nullptr;
         child = child->next) {
        if (auto converted = convert_node(child)) {
            dst_parent.append_child(std::move(converted));
        }
    }
}

std::unique_ptr<dom::Node> convert_node(lxb_dom_node_t* src) {
    if (src == nullptr) return nullptr;

    if (src->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        auto* el = lxb_dom_interface_element(src);
        size_t tag_len = 0;
        const lxb_char_t* tag = lxb_dom_element_local_name(el, &tag_len);
        auto result =
            std::make_unique<dom::Element>(read_lexbor_str(tag, tag_len));

        for (lxb_dom_attr_t* attr = lxb_dom_element_first_attribute(el);
             attr != nullptr;
             attr = lxb_dom_element_next_attribute(attr)) {
            size_t name_len = 0;
            const lxb_char_t* name =
                lxb_dom_attr_qualified_name(attr, &name_len);
            size_t value_len = 0;
            const lxb_char_t* value = lxb_dom_attr_value(attr, &value_len);
            if (name != nullptr) {
                result->set_attribute(
                    read_lexbor_str(name, name_len),
                    read_lexbor_str(value, value_len));
            }
        }

        convert_children(src, *result);
        return result;
    }

    if (src->type == LXB_DOM_NODE_TYPE_TEXT) {
        auto* text = lxb_dom_interface_text(src);
        return std::make_unique<dom::TextNode>(
            read_lexbor_str(text->char_data.data.data,
                            text->char_data.data.length));
    }

    // Skip comments, doctype, processing instructions.
    return nullptr;
}

} // namespace

std::unique_ptr<dom::Document> parse(std::string_view html) {
    lxb_html_document_t* doc = lxb_html_document_create();
    if (doc == nullptr) return nullptr;

    const lxb_status_t status = lxb_html_document_parse(
        doc,
        reinterpret_cast<const lxb_char_t*>(html.data()),
        html.size());

    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return nullptr;
    }

    auto result = std::make_unique<dom::Document>();
    lxb_dom_node_t* root = lxb_dom_interface_node(doc);
    convert_children(root, *result);

    lxb_html_document_destroy(doc);
    return result;
}

} // namespace honeybee::renderer::html
