#include "board/board_storage.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

namespace {

constexpr int kSdCs = 12;
constexpr int kLoraCs = 46;
constexpr uint32_t kSdSpiFrequency = 10000000;

paper_screen::BoardStorageCardType map_card_type(uint8_t card_type)
{
    switch (card_type) {
    case CARD_NONE:
        return paper_screen::BoardStorageCardType::None;
    case CARD_MMC:
        return paper_screen::BoardStorageCardType::Mmc;
    case CARD_SD:
        return paper_screen::BoardStorageCardType::Sd;
    case CARD_SDHC:
        return paper_screen::BoardStorageCardType::Sdhc;
    default:
        return paper_screen::BoardStorageCardType::Unknown;
    }
}

}  // namespace

namespace paper_screen {

BoardStorageStatus BoardStorage::begin()
{
    Serial.println("[storage-board] begin");

    pinMode(kLoraCs, OUTPUT);
    digitalWrite(kLoraCs, HIGH);
    pinMode(kSdCs, OUTPUT);
    digitalWrite(kSdCs, HIGH);

    if (!SD.begin(kSdCs, SPI, kSdSpiFrequency)) {
        status_.state = BoardStorageState::MountFailed;
        status_.card_type = BoardStorageCardType::None;
        Serial.println("[storage-board] mount failed");
        return status_;
    }

    status_.card_type = map_card_type(SD.cardType());
    if (status_.card_type == BoardStorageCardType::None) {
        status_.state = BoardStorageState::CardMissing;
        Serial.println("[storage-board] no card attached");
        return status_;
    }

    status_.state = BoardStorageState::Mounted;
    refresh_capacity();
    Serial.printf("[storage-board] mounted type=%s size=%lluMB total=%lluMB used=%lluMB\n",
                  board_storage_card_type_label(status_.card_type),
                  status_.card_size_bytes / (1024ULL * 1024ULL),
                  status_.total_bytes / (1024ULL * 1024ULL),
                  status_.used_bytes / (1024ULL * 1024ULL));
    return status_;
}

BoardStorageStatus BoardStorage::status() const
{
    return status_;
}

bool BoardStorage::mounted() const
{
    return status_.state == BoardStorageState::Mounted;
}

fs::File BoardStorage::open(const char* path, const char* mode)
{
    if (!mounted() || path == nullptr || mode == nullptr) {
        return fs::File();
    }
    return SD.open(path, mode);
}

bool BoardStorage::exists(const char* path) const
{
    if (!mounted() || path == nullptr) {
        return false;
    }
    return SD.exists(path);
}

bool BoardStorage::mkdir(const char* path)
{
    if (!mounted() || path == nullptr) {
        return false;
    }
    if (SD.exists(path)) {
        return true;
    }
    return SD.mkdir(path);
}

bool BoardStorage::remove(const char* path)
{
    if (!mounted() || path == nullptr) {
        return false;
    }
    return SD.remove(path);
}

bool BoardStorage::rename(const char* from, const char* to)
{
    if (!mounted() || from == nullptr || to == nullptr) {
        return false;
    }
    return SD.rename(from, to);
}

void BoardStorage::refresh_capacity()
{
    if (!mounted()) {
        status_.card_size_bytes = 0;
        status_.total_bytes = 0;
        status_.used_bytes = 0;
        return;
    }

    status_.card_size_bytes = SD.cardSize();
    status_.total_bytes = SD.totalBytes();
    status_.used_bytes = SD.usedBytes();
}

const char* board_storage_state_label(BoardStorageState state)
{
    switch (state) {
    case BoardStorageState::NotStarted:
        return "not started";
    case BoardStorageState::Mounted:
        return "mounted";
    case BoardStorageState::CardMissing:
        return "card missing";
    case BoardStorageState::MountFailed:
        return "mount failed";
    }
    return "unknown";
}

const char* board_storage_card_type_label(BoardStorageCardType card_type)
{
    switch (card_type) {
    case BoardStorageCardType::None:
        return "none";
    case BoardStorageCardType::Mmc:
        return "MMC";
    case BoardStorageCardType::Sd:
        return "SDSC";
    case BoardStorageCardType::Sdhc:
        return "SDHC";
    case BoardStorageCardType::Unknown:
        return "unknown";
    }
    return "unknown";
}

}  // namespace paper_screen
