#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "apps/reader/format/random_access_reader.h"

namespace paper_screen::reader_format {

struct ZipEntryInfo {
    static constexpr size_t kMaxNameLength = 256;

    char name[kMaxNameLength] = {};
    uint16_t method = 0;  // 0 = stored, 8 = deflated; anything else is unsupported
    uint32_t compressed_size = 0;
    uint32_t uncompressed_size = 0;
    uint32_t local_header_offset = 0;
};

// Reads the central directory of a (non-zip64) zip archive - which is all an
// EPUB container is - and extracts individual entries on demand. Entries with
// a compression method other than stored or deflate, or a name too long to
// fit ZipEntryInfo::name, are skipped during open() rather than failing the
// whole archive, since EPUBs in the wild occasionally carry odd extra
// manifest/metadata entries we don't need to read.
class ZipReader {
public:
    // Parses the End Of Central Directory record and central directory.
    // Returns false if `reader` doesn't look like a valid zip archive.
    bool open(RandomAccessReader* reader);

    size_t entry_count() const { return entries_.size(); }
    const ZipEntryInfo* entry(size_t index) const;

    // Case-sensitive exact match, as zip entry names are.
    const ZipEntryInfo* find_entry(const char* name) const;

    // Extracts `entry`'s decompressed contents into `output`, which must be
    // at least entry.uncompressed_size bytes (no incremental growth - the
    // exact size is always known up front from the central directory).
    bool extract(const ZipEntryInfo& entry, RandomAccessReader* reader, uint8_t* output, size_t output_capacity,
                 size_t* output_size) const;

private:
    std::vector<ZipEntryInfo> entries_;
};

}  // namespace paper_screen::reader_format
