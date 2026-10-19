#include "renderer/dom/node.hpp"

#include <functional>
#include <utility>

namespace honeybee::renderer::dom {

void Node::append_child(std::unique_ptr<Node> child) {
    if (!child) return;
    child->parent_ = this;
    children_.push_back(std::move(child));
}

std::string Node::text_content() const {
    std::string result;
    for (const auto& child : children_) {
        if (child->type() == NodeType::Text) {
            result += static_cast<const TextNode*>(child.get())->data();
        } else {
            result += child->text_content();
        }
    }
    return result;
}

Element::Element(std::string tag) : tag_(std::move(tag)) {}

void Element::set_attribute(std::string name, std::string value) {
    attributes_[std::move(name)] = std::move(value);
}

std::string Element::get_attribute(std::string_view name) const {
    auto it = attributes_.find(std::string(name));
    if (it == attributes_.end()) return {};
    return it->second;
}

TextNode::TextNode(std::string data) : data_(std::move(data)) {}

std::string Document::title() const {
    const Element* found = nullptr;
    std::function<void(const Node*)> dfs = [&](const Node* node) {
        if (found) return;
        if (node->type() == NodeType::Element) {
            const auto* el = static_cast<const Element*>(node);
            if (el->tag() == "title") { found = el; return; }
        }
        for (const auto& child : node->children()) {
            dfs(child.get());
        }
    };
    dfs(this);
    return found ? found->text_content() : std::string{};
}

} // namespace honeybee::renderer::dom
