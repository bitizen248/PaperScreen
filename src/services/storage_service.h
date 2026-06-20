#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board/board_storage.h"

namespace paper_screen {

enum class StorageStatus : uint8_t {
    NotStarted,
    Mounted,
    CardMissing,
    MountFailed,
    IoError,
};

struct StorageInfo {
    StorageStatus status = StorageStatus::NotStarted;
    BoardStorageCardType card_type = BoardStorageCardType::None;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
};

class StorageService {
public:
    StorageInfo begin(BoardStorage& board_storage);
    StorageInfo info() const;
    bool available() const;

    bool ensure_app_directory(const char* app_id);
    bool write_text_atomic(const char* path, const char* text);
    bool write_file_atomic(const char* path, const uint8_t* data, size_t size);
    bool file_size(const char* path, size_t* out_size) const;
    bool read_file(const char* path, uint8_t* out, size_t capacity, size_t* out_size) const;
    bool remove_file(const char* path);
    bool append_text(const char* path, const char* text);
    bool exists(const char* path) const;
    bool app_path(const char* app_id, const char* leaf, char* out, size_t out_size) const;

private:
    bool ensure_layout();
    bool ensure_directory(const char* path);
    bool path_allowed(const char* path) const;
    bool set_io_error();

    BoardStorage* board_storage_ = nullptr;
    StorageInfo info_;
};

const char* storage_status_label(StorageStatus status);

}  // namespace paper_screen
