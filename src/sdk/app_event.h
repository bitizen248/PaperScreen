#pragma once

#include <stdint.h>

namespace paper_screen {

enum class AppEventType : uint8_t {
    None,
    Open,
    Close,
    Suspend,
    Resume,
    TouchDown,
    TouchMove,
    TouchUp,
    HomeButton,
    FunctionButton,
    Tick,
    ScheduledWake,
    BackgroundRefresh,
};

struct AppTouchEvent {
    int16_t x = 0;
    int16_t y = 0;
};

struct AppEvent {
    AppEventType type = AppEventType::None;
    uint32_t timestamp_ms = 0;
    AppTouchEvent touch;
    uint32_t value = 0;
};

}  // namespace paper_screen
