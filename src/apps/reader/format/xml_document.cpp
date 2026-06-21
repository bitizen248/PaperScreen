#include "apps/reader/format/xml_document.h"

#include <stdlib.h>
#include <string.h>

namespace paper_screen::reader_format {

namespace {

bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

char* skip_spaces(char* p)
{
    while (*p && is_space(*p)) ++p;
    return p;
}

// Strips the namespace prefix from an element/attribute name in place by
// returning a pointer past the ':' - the buffer itself is untouched, callers
// just use the returned pointer as the "local name".
const char* strip_prefix(const char* name)
{
    const char* colon = strchr(name, ':');
    return colon ? colon + 1 : name;
}

// Decodes XML entities in place. Entities are always >= as long as their
// decoded form, so writing the decoded bytes back into the same buffer while
// scanning forward never overwrites not-yet-read input.
char* decode_entities_in_place(char* text)
{
    char* read = text;
    char* write = text;
    while (*read) {
        if (*read == '&') {
            if (strncmp(read, "&amp;", 5) == 0) {
                *write++ = '&';
                read += 5;
                continue;
            }
            if (strncmp(read, "&lt;", 4) == 0) {
                *write++ = '<';
                read += 4;
                continue;
            }
            if (strncmp(read, "&gt;", 4) == 0) {
                *write++ = '>';
                read += 4;
                continue;
            }
            if (strncmp(read, "&quot;", 6) == 0) {
                *write++ = '"';
                read += 6;
                continue;
            }
            if (strncmp(read, "&apos;", 6) == 0) {
                *write++ = '\'';
                read += 6;
                continue;
            }
            if (read[1] == '#') {
                char* num_start = read + 2;
                int base = 10;
                if (*num_start == 'x' || *num_start == 'X') {
                    base = 16;
                    ++num_start;
                }
                char* end = nullptr;
                long code = strtol(num_start, &end, base);
                if (end != num_start && *end == ';') {
                    if (code > 0 && code < 0x80) {
                        *write++ = static_cast<char>(code);
                    } else {
                        *write++ = '?';
                    }
                    read = end + 1;
                    continue;
                }
            }
        }
        *write++ = *read++;
    }
    *write = '\0';
    return text;
}

// Trims leading/trailing whitespace in place by null-terminating early and
// returning a pointer past the leading whitespace.
char* trim_in_place(char* text)
{
    char* start = skip_spaces(text);
    if (*start == '\0') {
        return start;
    }
    char* end = start + strlen(start);
    while (end > start && is_space(end[-1])) --end;
    *end = '\0';
    return start;
}

class Scanner {
public:
    Scanner(char* buffer, size_t length) : pos_(buffer), end_(buffer + length) {}

    bool done() const { return pos_ >= end_; }
    char* pos() const { return pos_; }
    size_t remaining() const { return static_cast<size_t>(end_ - pos_); }
    void advance(size_t n) { pos_ += n; }

    // Like strncmp(pos(), marker, len) but never reads past end_.
    bool starts_with(const char* marker) const
    {
        const size_t marker_len = strlen(marker);
        return remaining() >= marker_len && strncmp(pos_, marker, marker_len) == 0;
    }

    // Checks the bytes right after the leading '<' (i.e. pos()+1) against
    // `suffix`. The byte at pos() itself is never read - it may have been
    // reused as a text node's null terminator (see parse()'s text-capture
    // step) even though it's logically still the '<' that started this tag.
    bool matches_after_lt(const char* suffix) const
    {
        const size_t suffix_len = strlen(suffix);
        return remaining() >= suffix_len + 1 && strncmp(pos_ + 1, suffix, suffix_len) == 0;
    }

    // Finds the next occurrence of `marker` starting at pos_, returns nullptr
    // (and leaves pos_ unchanged) if not found before end_.
    char* find(const char* marker)
    {
        const size_t marker_len = strlen(marker);
        for (char* p = pos_; p + marker_len <= end_; ++p) {
            if (strncmp(p, marker, marker_len) == 0) {
                return p;
            }
        }
        return nullptr;
    }

private:
    char* pos_;
    char* end_;
};

}  // namespace

bool XmlDocument::parse(char* buffer, size_t length)
{
    nodes_.clear();
    attributes_.clear();
    root_ = kInvalidXmlNode;

    Scanner scanner(buffer, length);
    std::vector<int32_t> open_stack;

    while (!scanner.done()) {
        char* lt = scanner.find("<");
        if (lt == nullptr) {
            break;
        }
        if (lt != scanner.pos() && !open_stack.empty()) {
            // Text content between the previous tag and this one. This may
            // null-terminate exactly at lt, reusing the byte that starts the
            // upcoming tag - that's fine, every check below reads from lt+1
            // onward and never depends on *lt itself still being '<'.
            *lt = '\0';
            char* trimmed = trim_in_place(scanner.pos());
            if (*trimmed != '\0') {
                decode_entities_in_place(trimmed);
                nodes_[static_cast<size_t>(open_stack.back())].text = trimmed;
            }
        }
        scanner.advance(static_cast<size_t>(lt - scanner.pos()));

        if (scanner.matches_after_lt("?")) {
            char* close = scanner.find("?>");
            if (close == nullptr) {
                return false;
            }
            scanner.advance(static_cast<size_t>(close - scanner.pos()) + 2);
            continue;
        }
        if (scanner.matches_after_lt("!--")) {
            char* close = scanner.find("-->");
            if (close == nullptr) {
                return false;
            }
            scanner.advance(static_cast<size_t>(close - scanner.pos()) + 3);
            continue;
        }
        if (scanner.matches_after_lt("![CDATA[")) {
            char* cdata_start = scanner.pos() + 9;
            char* close = scanner.find("]]>");
            if (close == nullptr) {
                return false;
            }
            *close = '\0';
            if (!open_stack.empty()) {
                nodes_[static_cast<size_t>(open_stack.back())].text = cdata_start;
            }
            scanner.advance(static_cast<size_t>(close - scanner.pos()) + 3);
            continue;
        }
        if (scanner.matches_after_lt("!")) {
            char* close = scanner.find(">");
            if (close == nullptr) {
                return false;
            }
            scanner.advance(static_cast<size_t>(close - scanner.pos()) + 1);
            continue;
        }
        if (scanner.matches_after_lt("/")) {
            char* close = scanner.find(">");
            if (close == nullptr || open_stack.empty()) {
                return false;
            }
            char* closing_name_start = skip_spaces(scanner.pos() + 2);
            char* end = close;
            while (end > closing_name_start && is_space(end[-1])) --end;
            const char saved = *end;
            *end = '\0';
            const bool matches = strcmp(strip_prefix(closing_name_start), nodes_[static_cast<size_t>(open_stack.back())].name) == 0;
            *end = saved;
            if (!matches) {
                return false;
            }
            open_stack.pop_back();
            scanner.advance(static_cast<size_t>(close - scanner.pos()) + 1);
            continue;
        }
        if (scanner.remaining() < 2) {
            return false;
        }

        // Opening or self-closing element tag.
        char* name_start = scanner.pos() + 1;
        char* p = name_start;
        while (p < buffer + length && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '>' &&
               *p != '/') {
            ++p;
        }
        if (p >= buffer + length) {
            return false;
        }
        const char terminator = *p;
        *p = '\0';
        const char* local_name = strip_prefix(name_start);

        const int32_t node_index = static_cast<int32_t>(nodes_.size());
        nodes_.push_back(Node{});
        nodes_[static_cast<size_t>(node_index)].name = local_name;
        nodes_[static_cast<size_t>(node_index)].parent = open_stack.empty() ? kInvalidXmlNode : open_stack.back();

        if (open_stack.empty()) {
            if (root_ == kInvalidXmlNode) {
                root_ = node_index;
            }
        } else {
            const int32_t parent = open_stack.back();
            if (nodes_[static_cast<size_t>(parent)].first_child == kInvalidXmlNode) {
                nodes_[static_cast<size_t>(parent)].first_child = node_index;
            } else {
                int32_t sibling = nodes_[static_cast<size_t>(parent)].first_child;
                while (nodes_[static_cast<size_t>(sibling)].next_sibling != kInvalidXmlNode) {
                    sibling = nodes_[static_cast<size_t>(sibling)].next_sibling;
                }
                nodes_[static_cast<size_t>(sibling)].next_sibling = node_index;
            }
        }

        const uint32_t attr_start = static_cast<uint32_t>(attributes_.size());
        uint32_t attr_count = 0;
        bool self_closing = false;
        char* cursor;
        if (terminator == '>') {
            cursor = p + 1;
        } else if (terminator == '/') {
            if (p[1] != '>') {
                return false;
            }
            self_closing = true;
            cursor = p + 2;
        } else {
            cursor = p + 1;
            for (;;) {
                cursor = skip_spaces(cursor);
                if (*cursor == '/' && cursor[1] == '>') {
                    self_closing = true;
                    cursor += 2;
                    break;
                }
                if (*cursor == '>') {
                    ++cursor;
                    break;
                }
                if (*cursor == '\0') {
                    return false;
                }
                char* attr_name_start = cursor;
                while (*cursor && *cursor != '=' && !is_space(*cursor) && *cursor != '/' && *cursor != '>') ++cursor;
                char* attr_name_end = cursor;
                cursor = skip_spaces(cursor);
                if (*cursor != '=') {
                    return false;
                }
                *attr_name_end = '\0';
                ++cursor;
                cursor = skip_spaces(cursor);
                const char quote = *cursor;
                if (quote != '"' && quote != '\'') {
                    return false;
                }
                ++cursor;
                char* value_start = cursor;
                while (*cursor && *cursor != quote) ++cursor;
                if (*cursor != quote) {
                    return false;
                }
                *cursor = '\0';
                ++cursor;

                decode_entities_in_place(value_start);
                attributes_.push_back(Attribute{strip_prefix(attr_name_start), value_start});
                ++attr_count;
            }
        }

        nodes_[static_cast<size_t>(node_index)].attr_start = attr_start;
        nodes_[static_cast<size_t>(node_index)].attr_count = attr_count;

        if (!self_closing) {
            open_stack.push_back(node_index);
        }

        scanner.advance(static_cast<size_t>(cursor - scanner.pos()));
    }

    return root_ != kInvalidXmlNode && open_stack.empty();
}

int32_t XmlDocument::first_child(int32_t node, const char* local_name) const
{
    if (!valid(node)) {
        return kInvalidXmlNode;
    }
    int32_t child = nodes_[static_cast<size_t>(node)].first_child;
    while (child != kInvalidXmlNode && local_name != nullptr && strcmp(nodes_[static_cast<size_t>(child)].name, local_name) != 0) {
        child = nodes_[static_cast<size_t>(child)].next_sibling;
    }
    return child;
}

int32_t XmlDocument::next_sibling(int32_t node, const char* local_name) const
{
    if (!valid(node)) {
        return kInvalidXmlNode;
    }
    int32_t sibling = nodes_[static_cast<size_t>(node)].next_sibling;
    while (sibling != kInvalidXmlNode && local_name != nullptr &&
           strcmp(nodes_[static_cast<size_t>(sibling)].name, local_name) != 0) {
        sibling = nodes_[static_cast<size_t>(sibling)].next_sibling;
    }
    return sibling;
}

const char* XmlDocument::attribute(int32_t node, const char* attr_name) const
{
    if (!valid(node)) {
        return nullptr;
    }
    const Node& n = nodes_[static_cast<size_t>(node)];
    for (uint32_t i = 0; i < n.attr_count; ++i) {
        const Attribute& attr = attributes_[n.attr_start + i];
        if (strcmp(attr.name, attr_name) == 0) {
            return attr.value;
        }
    }
    return nullptr;
}

const char* XmlDocument::text(int32_t node) const
{
    if (!valid(node)) {
        return nullptr;
    }
    return nodes_[static_cast<size_t>(node)].text;
}

const char* XmlDocument::name(int32_t node) const
{
    if (!valid(node)) {
        return nullptr;
    }
    return nodes_[static_cast<size_t>(node)].name;
}

}  // namespace paper_screen::reader_format
