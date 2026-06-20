#pragma once

#include <cstdint>

#include "board/board.h"

namespace paper_screen {

enum class AppIcon {
    Tasks,
    Reader,
    Focus,
    Trmnl,
    Settings,
    Sync,
    Notes,
    Timer,
};

const char* app_icon_label(AppIcon icon);

struct AppTileViewModel {
    const char* label;
    AppIcon icon;
};

struct StatusBarViewModel {
    const char* title = "PaperScreen";
    const char* time = "--:--";
    const char* battery = "--%";
};

struct HomeViewModel {
    const char* title = "Home";
    const char* status_line = "Board shell ready";
    StatusBarViewModel status_bar;
    const AppTileViewModel* apps = nullptr;
    int app_count = 0;
    const uint8_t* wallpaper = nullptr;
    int wallpaper_width = 0;
    int wallpaper_height = 0;
};

struct AppScreenViewModel {
    StatusBarViewModel status_bar;
    const char* app_name = "";
};

class HomeScreen {
public:
    void set_board_status(BoardStatus status);
    void set_wallpaper(const uint8_t* data, int width, int height);
    HomeViewModel view_model() const;

private:
    BoardStatus board_status_;
    const uint8_t* wallpaper_ = nullptr;
    int wallpaper_width_ = 0;
    int wallpaper_height_ = 0;
};

}  // namespace paper_screen
