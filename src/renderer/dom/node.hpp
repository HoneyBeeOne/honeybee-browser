// =============================================================
// File: node.hpp
// Purpose: Our owned DOM tree (per ADR-007).
//
// This tree is fully independent of lexbor. After the converter
// in renderer/html/convert.cpp builds it, lexbor's document is
// destroyed and we own every byte.
//
// Ownership model:
//   - Children are owned via std::unique_ptr<Node>.
//   - parent_ is a raw back-pointer. It is safe because a parent
//     always outlives its children (the tree is never detached
//     from the root while any node is inspected).
//   - Nodes are non-copyable and non-movable: every node lives
//     at a fixed address for its entire lifetime, so parent_
//     never dangles.
//
// See ADR-007 for rationale.
// =============================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace honeybee::renderer::dom {

enum class NodeType : std::uint8_t { Document, Element, Text };

class Node {
public:
    virtual ~Node() = default;
    virtual NodeType type() const = 0;

    Node(const Node&)            = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&)                 = delete;
    Node& operator=(Node&&)      = delete;

    Node* parent() const noexcept { return parent_; }
    const std::vector<std::unique_ptr<Node>>& children() const noexcept {
        return children_;
    }

    // Takes ownership of `child` and sets its parent_ to this.
    void append_child(std::unique_ptr<Node> child);

    // Concatenated text of all descendant TextNodes, in document order.
    std::string text_content() const;

protected:
    Node() = default;

private:
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
};

class Element final : public Node {
public:
    explicit Element(std::string tag);

    NodeType type() const override { return NodeType::Element; }

    const std::string& tag() const noexcept { return tag_; }

    const std::unordered_map<std::string, std::string>&
    attributes() const noexcept { return attributes_; }

    void set_attribute(std::string name, std::string value);
    std::string get_attribute(std::string_view name) const;

private:
    std::string tag_;
    std::unordered_map<std::string, std::string> attributes_;
};

class TextNode final : public Node {
public:
    explicit TextNode(std::string data);

    NodeType type() const override { return NodeType::Text; }

    const std::string& data() const noexcept { return data_; }

private:
    std::string data_;
};

class Document final : public Node {
public:
    NodeType type() const override { return NodeType::Document; }

    // Returns the text content of the first <title> element found
    // anywhere in the tree, or an empty string if none exists.
    std::string title() const;
};

} // namespace honeybee::renderer::dom
