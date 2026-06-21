#include <cstdio>
#include <cstring>
#include <vector>

#include "apps/reader/format/inflate.h"
#include "tests/reader/inflate_test_vectors.h"

namespace {

int g_failures = 0;

#define EXPECT_TRUE(condition)                                                                   \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);            \
            ++g_failures;                                                                        \
        }                                                                                         \
    } while (0)

using paper_screen::reader_format::inflate_raw;

void check_case(const char* name, const uint8_t* compressed, size_t compressed_len, const uint8_t* expected,
                 size_t expected_len)
{
    std::vector<uint8_t> output(expected_len + 16, 0xAA);
    size_t output_size = 0;
    const bool ok = inflate_raw(compressed, compressed_len, output.data(), expected_len, &output_size);
    if (!ok) {
        std::fprintf(stderr, "  (%s) inflate_raw failed\n", name);
        ++g_failures;
        return;
    }
    if (output_size != expected_len) {
        std::fprintf(stderr, "  (%s) size mismatch: got %zu want %zu\n", name, output_size, expected_len);
        ++g_failures;
        return;
    }
    EXPECT_TRUE(std::memcmp(output.data(), expected, expected_len) == 0);
}

void test_empty_input()
{
    check_case("empty", kInflateCase0Compressed, kInflateCase0Compressed_len, kInflateCase0Expected,
               kInflateCase0Expected_len);
}

void test_single_byte()
{
    check_case("single_byte", kInflateCase1Compressed, kInflateCase1Compressed_len, kInflateCase1Expected,
               kInflateCase1Expected_len);
}

void test_short_text_fixed_huffman()
{
    check_case("short_text", kInflateCase2Compressed, kInflateCase2Compressed_len, kInflateCase2Expected,
               kInflateCase2Expected_len);
}

void test_repetitive_text_dynamic_huffman()
{
    check_case("repetitive_text", kInflateCase3Compressed, kInflateCase3Compressed_len, kInflateCase3Expected,
               kInflateCase3Expected_len);
}

void test_full_byte_range()
{
    check_case("full_byte_range", kInflateCase4Compressed, kInflateCase4Compressed_len, kInflateCase4Expected,
               kInflateCase4Expected_len);
}

void test_long_back_reference()
{
    // 258 'a' bytes - exercises the maximum DEFLATE match length and a
    // distance-1 back-reference (the classic RLE case).
    check_case("long_back_reference", kInflateCase5Compressed, kInflateCase5Compressed_len, kInflateCase5Expected,
               kInflateCase5Expected_len);
}

void test_real_zip_entry()
{
    // META-INF/container.xml as actually stored (DEFLATE method 8) inside
    // fixtures/oebps.epub - confirms our inflater agrees with zlib on a real,
    // not synthetically generated, compressed stream.
    check_case("real_zip_entry", kZipEntryCompressed, kZipEntryCompressed_len, kZipEntryExpected,
               kZipEntryExpected_len);
}

void test_rejects_undersized_output_buffer()
{
    uint8_t output[4];
    size_t output_size = 0;
    const bool ok = inflate_raw(kInflateCase3Compressed, kInflateCase3Compressed_len, output, sizeof(output),
                                 &output_size);
    EXPECT_TRUE(!ok);
}

void test_rejects_truncated_input()
{
    uint8_t output[64];
    size_t output_size = 0;
    // Truncate a real compressed stream to a handful of bytes - must fail
    // cleanly rather than reading past the input buffer.
    const bool ok = inflate_raw(kInflateCase2Compressed, 2, output, sizeof(output), &output_size);
    EXPECT_TRUE(!ok);
}

}  // namespace

int main()
{
    test_empty_input();
    test_single_byte();
    test_short_text_fixed_huffman();
    test_repetitive_text_dynamic_huffman();
    test_full_byte_range();
    test_long_back_reference();
    test_real_zip_entry();
    test_rejects_undersized_output_buffer();
    test_rejects_truncated_input();

    if (g_failures > 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("reader format tests passed\n");
    return 0;
}
