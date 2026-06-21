#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace paper_screen::reader_format {

constexpr int32_t kInvalidXmlNode = -1;

// A minimal XML reader covering just what EPUB's container.xml/OPF/NCX files
// need: elements, attributes, and direct text content. No DTD/CDATA/
// namespace-aware processing, no validation against a schema - just enough
// to walk these specific well-known-shaped documents.
//
// parse() works in place on `buffer`: attribute values and text content are
// entity-decoded in place (which can only shrink the buffer, since entities
// are always equal or longer than what they decode to) and the document
// holds pointers directly into it. `buffer` must stay alive and unmodified
// by the caller for the lifetime of the XmlDocument.
class XmlDocument {
public:
    bool parse(char* buffer, size_t length);

    int32_t root() const { return root_; }

    // First child element, optionally filtered by local name (namespace
    // prefix stripped - so "dc:title" matches local_name "title").
    int32_t first_child(int32_t node, const char* local_name = nullptr) const;

    // Next sibling element, optionally filtered by local name.
    int32_t next_sibling(int32_t node, const char* local_name = nullptr) const;

    // Exact (unprefixed) attribute name lookup. Returns nullptr if absent.
    const char* attribute(int32_t node, const char* attr_name) const;

    // Direct text content of the node (trimmed, entity-decoded), or nullptr.
    const char* text(int32_t node) const;

    const char* name(int32_t node) const;

private:
    struct Node {
        const char* name = nullptr;
        const char* text = nullptr;
        int32_t parent = kInvalidXmlNode;
        int32_t first_child = kInvalidXmlNode;
        int32_t next_sibling = kInvalidXmlNode;
        uint32_t attr_start = 0;
        uint32_t attr_count = 0;
    };
    struct Attribute {
        const char* name;
        const char* value;
    };

    bool valid(int32_t node) const { return node >= 0 && static_cast<size_t>(node) < nodes_.size(); }

    std::vector<Node> nodes_;
    std::vector<Attribute> attributes_;
    int32_t root_ = kInvalidXmlNode;
};

}  // namespace paper_screen::reader_format
