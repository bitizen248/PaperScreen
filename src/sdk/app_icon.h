#pragma once

namespace paper_screen {

enum class AppIcon {
    Tasks,
    Reader,
    Focus,
    Trmnl,
    Settings,
    Sync,
    Terrain,
    Notes,
    Timer,
};

const char* app_icon_label(AppIcon icon);

}  // namespace paper_screen
