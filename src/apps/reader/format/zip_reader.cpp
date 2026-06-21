#include "apps/reader/format/zip_reader.h"

#include <string.h>

#include "apps/reader/format/inflate.h"

namespace paper_screen::reader_format {

namespace {

constexpr uint32_t kEocdSignature = 0x06054b50;
constexpr uint32_t kCentralDirectorySignature = 0x02014b50;
constexpr uint32_t kLocalFileHeaderSignature = 0x04034b50;
constexpr size_t kEocdFixedSize = 22;
constexpr size_t kCentralDirectoryFixedSize = 46;
constexpr size_t kLocalHeaderFixedSize = 30;
constexpr size_t kMaxEocdCommentSize = 65535;

uint16_t read_u16_le(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read_u32_le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Scans backward from the end of the archive for the End Of Central
// Directory signature. The EOCD record is fixed-size except for a trailing
// comment of up to 65535 bytes, so the signature can't be more than
// kEocdFixedSize + kMaxEocdCommentSize from the end of the file.
bool find_eocd(RandomAccessReader* reader, uint64_t* eocd_offset)
{
    const uint64_t file_size = reader->size();
    if (file_size < kEocdFixedSize) {
        return false;
    }
    const uint64_t search_window = (file_size < kEocdFixedSize + kMaxEocdCommentSize)
                                        ? file_size
                                        : kEocdFixedSize + kMaxEocdCommentSize;
    std::vector<uint8_t> tail(search_window);
    const uint64_t tail_offset = file_size - search_window;
    if (!reader->read_at(tail_offset, tail.data(), search_window)) {
        return false;
    }

    for (int64_t i = static_cast<int64_t>(search_window) - static_cast<int64_t>(kEocdFixedSize); i >= 0; --i) {
        if (read_u32_le(&tail[static_cast<size_t>(i)]) == kEocdSignature) {
            *eocd_offset = tail_offset + static_cast<uint64_t>(i);
            return true;
        }
    }
    return false;
}

}  // namespace

bool ZipReader::open(RandomAccessReader* reader)
{
    entries_.clear();
    if (reader == nullptr) {
        return false;
    }

    uint64_t eocd_offset = 0;
    if (!find_eocd(reader, &eocd_offset)) {
        return false;
    }

    uint8_t eocd[kEocdFixedSize];
    if (!reader->read_at(eocd_offset, eocd, sizeof(eocd))) {
        return false;
    }
    const uint16_t total_entries = read_u16_le(&eocd[10]);
    const uint32_t central_directory_offset = read_u32_le(&eocd[16]);

    uint64_t pos = central_directory_offset;
    for (uint16_t i = 0; i < total_entries; ++i) {
        uint8_t header[kCentralDirectoryFixedSize];
        if (!reader->read_at(pos, header, sizeof(header))) {
            return false;
        }
        if (read_u32_le(&header[0]) != kCentralDirectorySignature) {
            return false;
        }
        const uint16_t method = read_u16_le(&header[10]);
        const uint32_t compressed_size = read_u32_le(&header[20]);
        const uint32_t uncompressed_size = read_u32_le(&header[24]);
        const uint16_t name_len = read_u16_le(&header[28]);
        const uint16_t extra_len = read_u16_le(&header[30]);
        const uint16_t comment_len = read_u16_le(&header[32]);
        const uint32_t local_header_offset = read_u32_le(&header[42]);

        const uint64_t name_offset = pos + kCentralDirectoryFixedSize;
        if (name_len > 0 && name_len < ZipEntryInfo::kMaxNameLength && (method == 0 || method == 8)) {
            ZipEntryInfo info;
            if (!reader->read_at(name_offset, info.name, name_len)) {
                return false;
            }
            info.name[name_len] = '\0';
            info.method = method;
            info.compressed_size = compressed_size;
            info.uncompressed_size = uncompressed_size;
            info.local_header_offset = local_header_offset;
            entries_.push_back(info);
        }
        // Entries with unsupported methods or over-long names are skipped
        // (not fatal) - we still need to step over their variable-length
        // name/extra/comment fields to reach the next header.

        pos = name_offset + name_len + extra_len + comment_len;
    }

    return true;
}

const ZipEntryInfo* ZipReader::entry(size_t index) const
{
    if (index >= entries_.size()) {
        return nullptr;
    }
    return &entries_[index];
}

const ZipEntryInfo* ZipReader::find_entry(const char* name) const
{
    if (name == nullptr) {
        return nullptr;
    }
    for (const auto& entry : entries_) {
        if (strcmp(entry.name, name) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

bool ZipReader::extract(const ZipEntryInfo& entry, RandomAccessReader* reader, uint8_t* output,
                         size_t output_capacity, size_t* output_size) const
{
    if (reader == nullptr || output_size == nullptr) {
        return false;
    }
    if (output == nullptr && entry.uncompressed_size > 0) {
        return false;
    }
    if (output_capacity < entry.uncompressed_size) {
        return false;
    }

    uint8_t local_header[kLocalHeaderFixedSize];
    if (!reader->read_at(entry.local_header_offset, local_header, sizeof(local_header))) {
        return false;
    }
    if (read_u32_le(&local_header[0]) != kLocalFileHeaderSignature) {
        return false;
    }
    const uint16_t local_name_len = read_u16_le(&local_header[26]);
    const uint16_t local_extra_len = read_u16_le(&local_header[28]);
    const uint64_t data_offset =
        entry.local_header_offset + kLocalHeaderFixedSize + local_name_len + local_extra_len;

    if (entry.method == 0) {
        if (entry.compressed_size != entry.uncompressed_size) {
            return false;  // stored entries must have matching sizes
        }
        if (!reader->read_at(data_offset, output, entry.uncompressed_size)) {
            return false;
        }
        *output_size = entry.uncompressed_size;
        return true;
    }

    // method == 8 (deflate) - anything else was already excluded in open().
    std::vector<uint8_t> compressed(entry.compressed_size);
    if (!reader->read_at(data_offset, compressed.data(), entry.compressed_size)) {
        return false;
    }
    return inflate_raw(compressed.data(), compressed.size(), output, output_capacity, output_size);
}

}  // namespace paper_screen::reader_format
