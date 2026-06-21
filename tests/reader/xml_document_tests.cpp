#include <cstdio>
#include <cstring>
#include <vector>

#include "apps/reader/format/xml_document.h"
#include "apps/reader/format/zip_reader.h"
#include "tests/reader/file_random_access_reader.h"

namespace {

int g_failures = 0;

#define EXPECT_TRUE(condition)                                                                  \
    do {                                                                                        \
        if (!(condition)) {                                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++g_failures;                                                                       \
        }                                                                                        \
    } while (0)

#define EXPECT_STREQ(a, b)                                                                       \
    do {                                                                                        \
        const char* va = (a);                                                                   \
        const char* vb = (b);                                                                   \
        if (va == nullptr || vb == nullptr || std::strcmp(va, vb) != 0) {                        \
            std::fprintf(stderr, "FAIL %s:%d: %s ('%s') != %s ('%s')\n", __FILE__, __LINE__, #a, \
                          va ? va : "(null)", #b, vb ? vb : "(null)");                            \
            ++g_failures;                                                                        \
        }                                                                                        \
    } while (0)

using paper_screen::reader_format::kInvalidXmlNode;
using paper_screen::reader_format::XmlDocument;
using paper_screen::reader_format::ZipReader;
using paper_screen::reader_format::test::FileRandomAccessReader;

constexpr const char* kFixtureDir = "tests/reader/fixtures";

// Extracts `entry_name` from `fixture_name` into a mutable, owned buffer the
// caller keeps alive for the lifetime of the resulting XmlDocument.
bool load_entry(const char* fixture_name, const char* entry_name, std::vector<char>* out)
{
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%s", kFixtureDir, fixture_name);
    FileRandomAccessReader reader(path);
    if (!reader.is_open()) {
        return false;
    }
    ZipReader zip;
    if (!zip.open(&reader)) {
        return false;
    }
    const auto* entry = zip.find_entry(entry_name);
    if (entry == nullptr) {
        return false;
    }
    out->resize(entry->uncompressed_size);
    size_t output_size = 0;
    if (!zip.extract(*entry, &reader, reinterpret_cast<uint8_t*>(out->data()), out->size(), &output_size)) {
        return false;
    }
    out->resize(output_size);
    return true;
}

void test_container_xml_oebps()
{
    std::vector<char> buffer;
    EXPECT_TRUE(load_entry("oebps.epub", "META-INF/container.xml", &buffer));

    XmlDocument doc;
    EXPECT_TRUE(doc.parse(buffer.data(), buffer.size()));

    const int32_t container = doc.root();
    EXPECT_STREQ(doc.name(container), "container");
    const int32_t rootfiles = doc.first_child(container, "rootfiles");
    EXPECT_TRUE(rootfiles != kInvalidXmlNode);
    const int32_t rootfile = doc.first_child(rootfiles, "rootfile");
    EXPECT_TRUE(rootfile != kInvalidXmlNode);
    EXPECT_STREQ(doc.attribute(rootfile, "full-path"), "OEBPS/content.opf");
    EXPECT_STREQ(doc.attribute(rootfile, "media-type"), "application/oebps-package+xml");
}

void test_container_xml_gatsby_single_line()
{
    std::vector<char> buffer;
    EXPECT_TRUE(load_entry("great_gatsby.epub", "META-INF/container.xml", &buffer));

    XmlDocument doc;
    EXPECT_TRUE(doc.parse(buffer.data(), buffer.size()));

    const int32_t container = doc.root();
    const int32_t rootfile = doc.first_child(doc.first_child(container, "rootfiles"), "rootfile");
    EXPECT_TRUE(rootfile != kInvalidXmlNode);
    EXPECT_STREQ(doc.attribute(rootfile, "full-path"), "OEBPS/package.opf");
}

void test_relative_paths_container_xml()
{
    std::vector<char> buffer;
    EXPECT_TRUE(load_entry("relative_paths.epub", "META-INF/container.xml", &buffer));

    XmlDocument doc;
    EXPECT_TRUE(doc.parse(buffer.data(), buffer.size()));
    const int32_t rootfile = doc.first_child(doc.first_child(doc.root(), "rootfiles"), "rootfile");
    EXPECT_TRUE(rootfile != kInvalidXmlNode);
    EXPECT_TRUE(doc.attribute(rootfile, "full-path") != nullptr);
}

// The Gatsby OPF is the real-world shape that broke the old vendored parser:
// <meta> elements without a "name" attribute (dcterms:* properties) appear
// before the actual <meta name="cover" content="cover-image"/> tag.
void test_gatsby_opf_metadata_and_cover()
{
    std::vector<char> buffer;
    EXPECT_TRUE(load_entry("great_gatsby.epub", "OEBPS/package.opf", &buffer));

    XmlDocument doc;
    EXPECT_TRUE(doc.parse(buffer.data(), buffer.size()));

    const int32_t package = doc.root();
    EXPECT_STREQ(doc.name(package), "package");
    const int32_t metadata = doc.first_child(package, "metadata");
    EXPECT_TRUE(metadata != kInvalidXmlNode);

    const int32_t title = doc.first_child(metadata, "title");
    EXPECT_TRUE(title != kInvalidXmlNode);
    EXPECT_STREQ(doc.text(title), "The Great Gatsby");

    // Walk all <meta> children looking for name="cover", skipping nameless ones.
    int32_t cover_meta = doc.first_child(metadata, "meta");
    while (cover_meta != kInvalidXmlNode) {
        const char* meta_name = doc.attribute(cover_meta, "name");
        if (meta_name != nullptr && std::strcmp(meta_name, "cover") == 0) {
            break;
        }
        cover_meta = doc.next_sibling(cover_meta, "meta");
    }
    EXPECT_TRUE(cover_meta != kInvalidXmlNode);
    EXPECT_STREQ(doc.attribute(cover_meta, "content"), "cover-image");

    const int32_t manifest = doc.first_child(package, "manifest");
    EXPECT_TRUE(manifest != kInvalidXmlNode);
    int32_t item = doc.first_child(manifest, "item");
    bool found_cover_item = false;
    while (item != kInvalidXmlNode) {
        const char* id = doc.attribute(item, "id");
        if (id != nullptr && std::strcmp(id, "cover-image") == 0) {
            EXPECT_STREQ(doc.attribute(item, "href"), "bookcover-generated.jpg");
            found_cover_item = true;
        }
        item = doc.next_sibling(item, "item");
    }
    EXPECT_TRUE(found_cover_item);
}

void test_self_closing_and_entities()
{
    char buffer[] = "<root a=\"1\" b='two'><child/><text>a &amp; b &lt;tag&gt;</text></root>";
    std::vector<char> owned(buffer, buffer + sizeof(buffer) - 1);

    XmlDocument doc;
    EXPECT_TRUE(doc.parse(owned.data(), owned.size()));

    const int32_t root = doc.root();
    EXPECT_STREQ(doc.name(root), "root");
    EXPECT_STREQ(doc.attribute(root, "a"), "1");
    EXPECT_STREQ(doc.attribute(root, "b"), "two");

    const int32_t child = doc.first_child(root, "child");
    EXPECT_TRUE(child != kInvalidXmlNode);

    const int32_t text_node = doc.next_sibling(child, "text");
    EXPECT_TRUE(text_node != kInvalidXmlNode);
    EXPECT_STREQ(doc.text(text_node), "a & b <tag>");
}

void test_namespace_prefix_stripped()
{
    char buffer[] = "<dc:metadata xmlns:dc=\"x\"><dc:title>Hi</dc:title></dc:metadata>";
    std::vector<char> owned(buffer, buffer + sizeof(buffer) - 1);

    XmlDocument doc;
    EXPECT_TRUE(doc.parse(owned.data(), owned.size()));
    EXPECT_STREQ(doc.name(doc.root()), "metadata");
    const int32_t title = doc.first_child(doc.root(), "title");
    EXPECT_TRUE(title != kInvalidXmlNode);
    EXPECT_STREQ(doc.text(title), "Hi");
}

void test_comment_and_xml_declaration_skipped()
{
    char buffer[] = "<?xml version=\"1.0\"?><!-- comment <fake/> --><root>ok</root>";
    std::vector<char> owned(buffer, buffer + sizeof(buffer) - 1);

    XmlDocument doc;
    EXPECT_TRUE(doc.parse(owned.data(), owned.size()));
    EXPECT_STREQ(doc.name(doc.root()), "root");
    EXPECT_STREQ(doc.text(doc.root()), "ok");
}

void test_rejects_mismatched_tags()
{
    char buffer[] = "<root><child></root></child>";
    std::vector<char> owned(buffer, buffer + sizeof(buffer) - 1);
    XmlDocument doc;
    EXPECT_TRUE(!doc.parse(owned.data(), owned.size()));
}

void test_rejects_unclosed_tag()
{
    char buffer[] = "<root><child>";
    std::vector<char> owned(buffer, buffer + sizeof(buffer) - 1);
    XmlDocument doc;
    EXPECT_TRUE(!doc.parse(owned.data(), owned.size()));
}

}  // namespace

int main()
{
    test_container_xml_oebps();
    test_container_xml_gatsby_single_line();
    test_relative_paths_container_xml();
    test_gatsby_opf_metadata_and_cover();
    test_self_closing_and_entities();
    test_namespace_prefix_stripped();
    test_comment_and_xml_declaration_skipped();
    test_rejects_mismatched_tags();
    test_rejects_unclosed_tag();

    if (g_failures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("xml document tests passed\n");
    return 0;
}
