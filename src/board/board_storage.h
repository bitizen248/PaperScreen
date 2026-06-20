#pragma once

#include <stddef.h>
#include <stdint.h>

#include <FS.h>

namespace paper_screen {

enum class BoardStorageState : uint8_t {
    NotStarted,
    Mounted,
    CardMissing,
    MountFailed,
};

enum class BoardStorageCardType : uint8_t {
    None,
    Mmc,
    Sd,
    Sdhc,
    Unknown,
};

struct BoardStorageStatus {
    BoardStorageState state = BoardStorageState::NotStarted;
    BoardStorageCardType card_type = BoardStorageCardType::None;
    uint64_t card_size_bytes = 0;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
};

class BoardStorage {
public:
    BoardStorageStatus begin();
    BoardStorageStatus status() const;
    bool mounted() const;

    fs::File open(const char* path, const char* mode);
    bool exists(const char* path) const;
    bool mkdir(const char* path);
    bool remove(const char* path);
    bool rename(const char* from, const char* to);

private:
    void refresh_capacity();

    BoardStorageStatus status_;
};

const char* board_storage_state_label(BoardStorageState state);
const char* board_storage_card_type_label(BoardStorageCardType card_type);

}  // namespace paper_screen
