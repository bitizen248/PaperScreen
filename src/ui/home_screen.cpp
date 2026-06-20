#include "home_screen.h"

#include <Arduino.h>

#include "sdk/app_registry.h"

namespace paper_screen {

void HomeScreen::set_board_status(BoardStatus status)
{
    Serial.println("[home] set board status");
    board_status_ = status;
}

void HomeScreen::set_time_status(TimeStatus status)
{
    time_status_ = status;
}

HomeViewModel HomeScreen::view_model() const
{
    HomeViewModel model;
    if (board_status_.state != BoardInitState::Ready) {
        model.status_line = "Board not initialized";
    } else if (!time_status_.rtc_initialized) {
        model.status_line = "Ready / RTC error";
    } else if (!time_status_.rtc_valid) {
        model.status_line = "Ready / Time not set";
    } else {
        model.status_line = "Ready / Time set";
    }
    model.status_bar.title = "Home";
    model.status_bar.time = "--:--";
    model.status_bar.battery = "--%";
    model.apps = home_app_registry(&model.app_count);
    model.wallpaper = wallpaper_;
    model.wallpaper_width = wallpaper_width_;
    model.wallpaper_height = wallpaper_height_;
    return model;
}

}  // namespace paper_screen
