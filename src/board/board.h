#pragma once

#include <stdint.h>

#include "TouchDrvGT911.hpp"

namespace paper_screen {

enum class BoardInitState : uint8_t {
    NotStarted,
    Ready,
};

struct BoardStatus {
    BoardInitState state = BoardInitState::NotStarted;
    bool hardware_initialized = false;
    bool touch_initialized = false;
};

struct TouchPoint {
    int16_t x = 0;
    int16_t y = 0;
    bool pressed = false;
};

class Board {
public:
    BoardStatus begin();
    BoardStatus status() const;
    bool boot_button_pressed() const;
    bool consume_home_button_pressed();
    bool function_button_pressed() const;
    void clear_function_button_interrupt() const;
    TouchPoint touch_point();
    void poll_power_button_diagnostics();

private:
    static void handle_touch_home_button(void* user_data);

    void log_i2c_devices();

    BoardStatus status_;
    TouchDrvGT911 touch_;
    bool home_button_pressed_ = false;
    int last_boot_level_ = -1;
    int last_rtc_int_level_ = -1;
    int last_touch_int_level_ = -1;
    int last_pca_int_level_ = -1;
    int last_pca_port0_ = -1;
    int last_pca_port1_ = -1;
    unsigned long last_power_diag_poll_ms_ = 0;
};

}  // namespace paper_screen
