#pragma once

#include <stdint.h>

namespace paper_screen {

enum AppCapability : uint32_t {
    AppCapabilityNone = 0,
    AppCapabilityNetwork = 1U << 0,
    AppCapabilityStorage = 1U << 1,
    AppCapabilityBackgroundSchedule = 1U << 2,
    AppCapabilityWakeTimer = 1U << 3,
    AppCapabilityBatteryState = 1U << 4,
    AppCapabilityTime = 1U << 5,
    AppCapabilityFullscreen = 1U << 6,
    AppCapabilityBacklight = 1U << 7,
};

inline bool app_has_capability(uint32_t capabilities, AppCapability capability)
{
    return (capabilities & static_cast<uint32_t>(capability)) != 0;
}

}  // namespace paper_screen
