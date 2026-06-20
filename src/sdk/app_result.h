#pragma once

#include <stdint.h>

namespace paper_screen {

enum class AppRefreshHint : uint8_t {
    None,
    Partial,
    Full,
};

struct AppSleepRequest {
    bool requested = false;
    uint32_t seconds = 0;
};

struct AppActionRequest {
    bool return_home = false;
    bool keep_awake = false;
    bool clear_scheduled_wake = false;
    AppSleepRequest sleep;
    uint32_t schedule_wake_seconds = 0;
    uint32_t command = 0;
};

struct AppEventResult {
    bool handled = false;
    AppRefreshHint refresh_hint = AppRefreshHint::None;
    bool status_bar_changed = false;
    AppActionRequest action;
};

struct AppRenderResult {
    AppRefreshHint refresh_hint = AppRefreshHint::None;
    bool status_bar_changed = false;
};

}  // namespace paper_screen
