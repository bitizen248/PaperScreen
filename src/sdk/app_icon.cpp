#include "sdk/app_icon.h"

namespace paper_screen {

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
    case AppIcon::Terrain:
        return "Terrain";
    case AppIcon::Notes:
        return "Notes";
    case AppIcon::Timer:
        return "Timer";
    }
    return "";
}

}  // namespace paper_screen
