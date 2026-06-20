#pragma once

#include <stdint.h>

namespace paper_screen {

enum class BoardBatteryState : uint8_t {
    Unknown,
    Discharging,
    Charging,
    Full,
};

struct BoardBatteryStatus {
    bool initialized = false;
    bool present = false;
    bool valid = false;
    uint8_t percentage = 0;
    uint16_t voltage_mv = 0;
    int16_t current_ma = 0;
    BoardBatteryState state = BoardBatteryState::Unknown;
};

class BoardBattery {
public:
    bool begin();
    BoardBatteryStatus read();

private:
    bool initialized_ = false;
    uint8_t consecutive_failures_ = 0;
};

}  // namespace paper_screen
