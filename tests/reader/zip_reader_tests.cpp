#include <cstdio>
#include <vector>

#include "apps/reader/format/zip_reader.h"
#include "tests/reader/file_random_access_reader.h"
#include "tests/reader/zip_manifest.h"

namespace {

int g_failures = 0;

#define EXPECT_TRUE(condition)                                                                   \
    do {                                                                                         \
        if (!(condition)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);            \
            ++g_failures;                                                                        \
        }                                                                                         \
    } while (0)

using paper_screen::reader_format::ZipReader;
using paper_screen::reader_format::test::FileRandomAccessReader;

constexpr const char* kFixtureDir = "tests/reader/fixtures";

uint64_t fnv1a64(const uint8_t* data, size_t size)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

void check_fixture_against_manifest(const char* fixture_name, const ManifestEntry* manifest, size_t manifest_count)
{
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%s", kFixtureDir, fixture_name);

    FileRandomAccessReader reader(path);
    if (!reader.is_open()) {
        std::fprintf(stderr, "  (%s) could not open fixture\n", fixture_name);
        ++g_failures;
        return;
    }

    ZipReader zip;
    if (!zip.open(&reader)) {
        std::fprintf(stderr, "  (%s) ZipReader::open failed\n", fixture_name);
        ++g_failures;
        return;
    }

    if (zip.entry_count() != manifest_count) {
        std::fprintf(stderr, "  (%s) entry count mismatch: got %zu want %zu\n", fixture_name, zip.entry_count(),
                      manifest_count);
        ++g_failures;
        return;
    }

    for (size_t i = 0; i < manifest_count; ++i) {
        const ManifestEntry& expected = manifest[i];
        const auto* entry = zip.find_entry(expected.name);
        if (!entry) {
            std::fprintf(stderr, "  (%s) missing entry '%s'\n", fixture_name, expected.name);
            ++g_failures;
            continue;
        }
        if (entry->uncompressed_size != expected.size) {
            std::fprintf(stderr, "  (%s) '%s' size mismatch: got %u want %zu\n", fixture_name, expected.name,
                          entry->uncompressed_size, expected.size);
            ++g_failures;
            continue;
        }

        std::vector<uint8_t> output(expected.size);
        size_t output_size = 0;
        const bool ok = zip.extract(*entry, &reader, output.data(), output.size(), &output_size);
        if (!ok) {
            std::fprintf(stderr, "  (%s) extract('%s') failed\n", fixture_name, expected.name);
            ++g_failures;
            continue;
        }
        if (output_size != expected.size) {
            std::fprintf(stderr, "  (%s) '%s' extracted size mismatch: got %zu want %zu\n", fixture_name,
                          expected.name, output_size, expected.size);
            ++g_failures;
            continue;
        }
        const uint64_t hash = fnv1a64(output.data(), output_size);
        if (hash != expected.hash) {
            std::fprintf(stderr, "  (%s) '%s' content hash mismatch\n", fixture_name, expected.name);
            ++g_failures;
        }
    }
}

void test_oebps_fixture()
{
    check_fixture_against_manifest("oebps.epub", kOebpsManifest, kOebpsManifest_count);
}

void test_relative_paths_fixture()
{
    check_fixture_against_manifest("relative_paths.epub", kRelativePathsManifest, kRelativePathsManifest_count);
}

void test_no_oebps_fixture()
{
    check_fixture_against_manifest("no_oebps.epub", kNoOebpsManifest, kNoOebpsManifest_count);
}

void test_great_gatsby_fixture()
{
    check_fixture_against_manifest("great_gatsby.epub", kGreatGatsbyManifest, kGreatGatsbyManifest_count);
}

void test_missing_entry_lookup_returns_null()
{
    char path[256];
    std::snprintf(path, sizeof(path), "%s/oebps.epub", kFixtureDir);
    FileRandomAccessReader reader(path);
    ZipReader zip;
    EXPECT_TRUE(zip.open(&reader));
    EXPECT_TRUE(zip.find_entry("does/not/exist.txt") == nullptr);
}

void test_rejects_non_zip_file()
{
    char path[256];
    std::snprintf(path, sizeof(path), "%s/../inflate_tests.cpp", kFixtureDir);
    FileRandomAccessReader reader(path);
    ZipReader zip;
    EXPECT_TRUE(!zip.open(&reader));
}

}  // namespace

int main()
{
    test_oebps_fixture();
    test_relative_paths_fixture();
    test_no_oebps_fixture();
    test_great_gatsby_fixture();
    test_missing_entry_lookup_returns_null();
    test_rejects_non_zip_file();

    if (g_failures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("zip reader tests passed\n");
    return 0;
}
