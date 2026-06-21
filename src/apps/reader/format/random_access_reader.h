#pragma once

#include <stddef.h>
#include <stdint.h>

namespace paper_screen::reader_format {

// Seek+read abstraction the format layer (zip, and later OPF/HTML parsing)
// reads books through. Keeping this as an interface - rather than reaching
// into fs::File/Arduino directly - is what lets all of this code be built
// and tested on the host with no PlatformIO/hardware involved; the board
// integration supplies an fs::File-backed implementation later.
class RandomAccessReader {
public:
    virtual ~RandomAccessReader() = default;

    // Reads exactly `size` bytes starting at `offset`. Returns false (and
    // leaves *buffer contents unspecified) if the read would run past the
    // end of the underlying data or otherwise fails.
    virtual bool read_at(uint64_t offset, void* buffer, size_t size) = 0;

    virtual uint64_t size() const = 0;
};

}  // namespace paper_screen::reader_format
