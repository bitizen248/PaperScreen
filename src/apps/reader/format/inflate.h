#pragma once

#include <stddef.h>
#include <stdint.h>

namespace paper_screen::reader_format {

// Decodes a raw RFC 1951 DEFLATE stream (no zlib/gzip wrapper - this is the
// format PKZIP's "deflate" compression method stores directly) into a
// caller-supplied buffer.
//
// The zip central directory always records the exact uncompressed size of an
// entry up front, so callers are expected to allocate `output_capacity` to
// match it exactly rather than growing a buffer incrementally - this keeps
// the decoder allocation-free and predictable on constrained hardware.
//
// Returns true and sets *output_size on success. Returns false on malformed
// input or if the stream would overflow output_capacity.
bool inflate_raw(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_capacity,
                  size_t* output_size);

}  // namespace paper_screen::reader_format
