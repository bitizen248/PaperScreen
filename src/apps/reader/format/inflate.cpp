#include "apps/reader/format/inflate.h"

#include <string.h>

namespace paper_screen::reader_format {

namespace {

constexpr int kMaxCodeBits = 15;
constexpr int kMaxLitLenSymbols = 288;
constexpr int kMaxDistSymbols = 30;

// Bit-level reader over the raw DEFLATE stream. DEFLATE packs bits into bytes
// least-significant-bit first.
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    // Reads a single bit. Returns -1 once the input is exhausted.
    int get_bit()
    {
        if (bit_count_ == 0) {
            if (byte_pos_ >= size_) {
                return -1;
            }
            bit_buffer_ = data_[byte_pos_++];
            bit_count_ = 8;
        }
        const int bit = bit_buffer_ & 1;
        bit_buffer_ >>= 1;
        --bit_count_;
        return bit;
    }

    // Reads `count` bits (0-16), LSB first, returns -1 on exhaustion.
    int get_bits(int count)
    {
        int value = 0;
        for (int i = 0; i < count; ++i) {
            const int bit = get_bit();
            if (bit < 0) {
                return -1;
            }
            value |= (bit << i);
        }
        return value;
    }

    // Discards any partial byte so the next read starts on a byte boundary.
    void align_to_byte()
    {
        bit_count_ = 0;
        bit_buffer_ = 0;
    }

    bool read_aligned_bytes(uint8_t* out, size_t count)
    {
        if (byte_pos_ + count > size_) {
            return false;
        }
        memcpy(out, data_ + byte_pos_, count);
        byte_pos_ += count;
        return true;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t byte_pos_ = 0;
    unsigned bit_buffer_ = 0;
    int bit_count_ = 0;
};

// Canonical Huffman decode table, built per RFC 1951 section 3.2.2: symbols
// are grouped by code length and assigned consecutive codes within each
// length, in order of symbol number.
struct HuffmanTable {
    uint16_t counts[kMaxCodeBits + 1] = {};
    uint16_t symbols[kMaxLitLenSymbols] = {};
};

void build_huffman_table(HuffmanTable* table, const uint8_t* lengths, int num_symbols)
{
    memset(table->counts, 0, sizeof(table->counts));
    for (int i = 0; i < num_symbols; ++i) {
        table->counts[lengths[i]]++;
    }
    table->counts[0] = 0;  // length 0 means "symbol unused"

    uint16_t offsets[kMaxCodeBits + 2] = {};
    for (int len = 1; len <= kMaxCodeBits; ++len) {
        offsets[len + 1] = offsets[len] + table->counts[len];
    }
    for (int i = 0; i < num_symbols; ++i) {
        if (lengths[i] != 0) {
            table->symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
        }
    }
}

// Decodes one symbol by reading bits one at a time and checking, after each
// bit, whether the code read so far falls within the range of codes of that
// length - the standard canonical-Huffman streaming decode.
int decode_symbol(BitReader* reader, const HuffmanTable& table)
{
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= kMaxCodeBits; ++len) {
        const int bit = reader->get_bit();
        if (bit < 0) {
            return -1;
        }
        code |= bit;
        const int count = table.counts[len];
        if (code - first < count) {
            return table.symbols[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

constexpr uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtraBits[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,   17,    25,
                                     33,   49,   65,   97,   129,  193,  257,  385,  513,   769,
                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtraBits[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3,  3,  4,  4,  5,  5, 6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr uint8_t kCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

void build_fixed_tables(HuffmanTable* lit_len, HuffmanTable* dist)
{
    uint8_t lit_len_lengths[kMaxLitLenSymbols];
    int i = 0;
    for (; i < 144; ++i) lit_len_lengths[i] = 8;
    for (; i < 256; ++i) lit_len_lengths[i] = 9;
    for (; i < 280; ++i) lit_len_lengths[i] = 7;
    for (; i < 288; ++i) lit_len_lengths[i] = 8;
    build_huffman_table(lit_len, lit_len_lengths, kMaxLitLenSymbols);

    uint8_t dist_lengths[kMaxDistSymbols];
    for (int j = 0; j < kMaxDistSymbols; ++j) dist_lengths[j] = 5;
    build_huffman_table(dist, dist_lengths, kMaxDistSymbols);
}

// Reads the dynamic Huffman code length tables (RFC 1951 section 3.2.7) and
// builds the literal/length and distance decode tables for this block.
bool read_dynamic_tables(BitReader* reader, HuffmanTable* lit_len, HuffmanTable* dist)
{
    const int hlit = reader->get_bits(5);
    const int hdist = reader->get_bits(5);
    const int hclen = reader->get_bits(4);
    if (hlit < 0 || hdist < 0 || hclen < 0) {
        return false;
    }
    const int num_lit_len = hlit + 257;
    const int num_dist = hdist + 1;
    const int num_code_lengths = hclen + 4;

    uint8_t cl_lengths[19] = {};
    for (int i = 0; i < num_code_lengths; ++i) {
        const int len = reader->get_bits(3);
        if (len < 0) {
            return false;
        }
        cl_lengths[kCodeLengthOrder[i]] = static_cast<uint8_t>(len);
    }
    HuffmanTable cl_table;
    build_huffman_table(&cl_table, cl_lengths, 19);

    uint8_t combined_lengths[kMaxLitLenSymbols + kMaxDistSymbols] = {};
    int total = num_lit_len + num_dist;
    int pos = 0;
    while (pos < total) {
        const int symbol = decode_symbol(reader, cl_table);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 16) {
            combined_lengths[pos++] = static_cast<uint8_t>(symbol);
        } else if (symbol == 16) {
            if (pos == 0) {
                return false;
            }
            const int repeat = reader->get_bits(2);
            if (repeat < 0 || pos + repeat + 3 > total) {
                return false;
            }
            const uint8_t prev = combined_lengths[pos - 1];
            for (int i = 0; i < repeat + 3; ++i) combined_lengths[pos++] = prev;
        } else if (symbol == 17) {
            const int repeat = reader->get_bits(3);
            if (repeat < 0 || pos + repeat + 3 > total) {
                return false;
            }
            for (int i = 0; i < repeat + 3; ++i) combined_lengths[pos++] = 0;
        } else {
            const int repeat = reader->get_bits(7);
            if (repeat < 0 || pos + repeat + 11 > total) {
                return false;
            }
            for (int i = 0; i < repeat + 11; ++i) combined_lengths[pos++] = 0;
        }
    }

    build_huffman_table(lit_len, combined_lengths, num_lit_len);
    build_huffman_table(dist, combined_lengths + num_lit_len, num_dist);
    return true;
}

bool inflate_block(BitReader* reader, const HuffmanTable& lit_len, const HuffmanTable& dist, uint8_t* output,
                    size_t output_capacity, size_t* output_pos)
{
    for (;;) {
        const int symbol = decode_symbol(reader, lit_len);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 256) {
            if (*output_pos >= output_capacity) {
                return false;
            }
            output[(*output_pos)++] = static_cast<uint8_t>(symbol);
            continue;
        }
        if (symbol == 256) {
            return true;  // end of block
        }
        const int length_index = symbol - 257;
        if (length_index >= 29) {
            return false;
        }
        const int extra = reader->get_bits(kLengthExtraBits[length_index]);
        if (extra < 0) {
            return false;
        }
        const size_t match_length = kLengthBase[length_index] + extra;

        const int dist_symbol = decode_symbol(reader, dist);
        if (dist_symbol < 0 || dist_symbol >= 30) {
            return false;
        }
        const int dist_extra = reader->get_bits(kDistExtraBits[dist_symbol]);
        if (dist_extra < 0) {
            return false;
        }
        const size_t match_distance = kDistBase[dist_symbol] + dist_extra;

        if (match_distance > *output_pos || *output_pos + match_length > output_capacity) {
            return false;
        }
        // Back-reference copy. Done byte-by-byte since match_distance can be
        // smaller than match_length (overlapping run-length patterns).
        size_t src = *output_pos - match_distance;
        for (size_t i = 0; i < match_length; ++i) {
            output[*output_pos] = output[src];
            ++(*output_pos);
            ++src;
        }
    }
}

}  // namespace

bool inflate_raw(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_capacity,
                  size_t* output_size)
{
    if (input == nullptr || output == nullptr || output_size == nullptr) {
        return false;
    }

    BitReader reader(input, input_size);
    size_t output_pos = 0;

    for (;;) {
        const int bfinal = reader.get_bit();
        const int btype = reader.get_bits(2);
        if (bfinal < 0 || btype < 0) {
            return false;
        }

        if (btype == 0) {
            // Stored (uncompressed) block.
            reader.align_to_byte();
            uint8_t header[4];
            if (!reader.read_aligned_bytes(header, 4)) {
                return false;
            }
            const uint16_t len = static_cast<uint16_t>(header[0] | (header[1] << 8));
            const uint16_t nlen = static_cast<uint16_t>(header[2] | (header[3] << 8));
            if (static_cast<uint16_t>(~len) != nlen) {
                return false;
            }
            if (output_pos + len > output_capacity) {
                return false;
            }
            if (!reader.read_aligned_bytes(output + output_pos, len)) {
                return false;
            }
            output_pos += len;
        } else if (btype == 1 || btype == 2) {
            HuffmanTable lit_len;
            HuffmanTable dist;
            if (btype == 1) {
                build_fixed_tables(&lit_len, &dist);
            } else {
                if (!read_dynamic_tables(&reader, &lit_len, &dist)) {
                    return false;
                }
            }
            if (!inflate_block(&reader, lit_len, dist, output, output_capacity, &output_pos)) {
                return false;
            }
        } else {
            return false;  // btype == 3 is reserved/invalid
        }

        if (bfinal) {
            break;
        }
    }

    *output_size = output_pos;
    return true;
}

}  // namespace paper_screen::reader_format
