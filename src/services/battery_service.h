#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board/board_battery.h"

namespace paper_screen {

struct BatteryStatus {
    bool initialized = false;
    bool valid = false;
    uint8_t percentage = 0;
    uint16_t voltage_mv = 0;
    bool charging = false;
    bool full = false;
    bool low = false;
    int64_t last_update_epoch = 0;
};

class BatteryService {
public:
    void begin(BoardBattery& battery);

    BatteryStatus status() const;
    bool update(int64_t now_epoch);
    bool format_percentage(char* out, size_t out_size) const;

private:
    BoardBattery* battery_ = nullptr;
    BatteryStatus status_;
    bool has_cached_percentage_ = false;
    char formatted_percentage_[6] = "--%";
};

}  // namespace paper_screen
