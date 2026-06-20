#include "home_screen.h"

#include <Arduino.h>

namespace paper_screen {

namespace {

const AppTileViewModel kHomeApps[] = {
    {"Tasks", AppIcon::Tasks},
    {"Reader", AppIcon::Reader},
    {"Focus", AppIcon::Focus},
    {"TRMNL", AppIcon::Trmnl},
    {"Settings", AppIcon::Settings},
    {"Sync", AppIcon::Sync},
    {"Notes", AppIcon::Notes},
    {"Timer", AppIcon::Timer},
};

}  // namespace

const char* app_icon_label(AppIcon icon)
{
    switch (icon) {
    case AppIcon::Tasks:
        return "Tasks";
    case AppIcon::Reader:
        return "Reader";
    case AppIcon::Focus:
        return "Focus";
    case AppIcon::Trmnl:
        return "TRMNL";
    case AppIcon::Settings:
        return "Settings";
    case AppIcon::Sync:
        return "Sync";
    case AppIcon::Notes:
        return "Notes";
    case AppIcon::Timer:
        return "Timer";
    }
    return "";
}

void HomeScreen::set_board_status(BoardStatus status)
{
    Serial.println("[home] set board status");
    board_status_ = status;
}

void HomeScreen::set_wallpaper(const uint8_t* data, int width, int height)
{
    wallpaper_ = data;
    wallpaper_width_ = width;
    wallpaper_height_ = height;
}

HomeViewModel HomeScreen::view_model() const
{
    HomeViewModel model;
    model.status_line = board_status_.state == BoardInitState::Ready
                            ? "Ready"
                            : "Board not initialized";
    model.status_bar.title = "Home";
    model.status_bar.time = "--:--";
    model.status_bar.battery = "--%";
    model.apps = kHomeApps;
    model.app_count = static_cast<int>(sizeof(kHomeApps) / sizeof(kHomeApps[0]));
    model.wallpaper = wallpaper_;
    model.wallpaper_width = wallpaper_width_;
    model.wallpaper_height = wallpaper_height_;
    return model;
}

}  // namespace paper_screen
