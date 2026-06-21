#pragma once

#include <stdio.h>

#include "apps/reader/format/random_access_reader.h"

namespace paper_screen::reader_format::test {

// Host-only RandomAccessReader backed by stdio - the firmware equivalent
// (backed by fs::File) lives in board integration code, not here, so this
// format layer never has to know about Arduino/ESP-IDF.
class FileRandomAccessReader final : public RandomAccessReader {
public:
    explicit FileRandomAccessReader(const char* path) : file_(fopen(path, "rb"))
    {
        if (file_ != nullptr) {
            fseek(file_, 0, SEEK_END);
            size_ = static_cast<uint64_t>(ftell(file_));
        }
    }

    ~FileRandomAccessReader() override
    {
        if (file_ != nullptr) {
            fclose(file_);
        }
    }

    bool is_open() const { return file_ != nullptr; }

    bool read_at(uint64_t offset, void* buffer, size_t size) override
    {
        if (file_ == nullptr || offset + size > size_) {
            return false;
        }
        if (fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
            return false;
        }
        return fread(buffer, 1, size, file_) == size;
    }

    uint64_t size() const override { return size_; }

private:
    FILE* file_ = nullptr;
    uint64_t size_ = 0;
};

}  // namespace paper_screen::reader_format::test
