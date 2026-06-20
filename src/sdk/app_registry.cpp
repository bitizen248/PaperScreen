#include "sdk/app_registry.h"

#include <cstring>

namespace paper_screen {

namespace {

constexpr AppDescriptor kRegisteredApps[] = {
    {
        .id = "tasks",
        .name = "Tasks",
        .icon = AppIcon::Tasks,
        .capabilities = AppCapabilityStorage | AppCapabilityTime,
        .show_on_home = true,
    },
    {
        .id = "reader",
        .name = "Reader",
        .icon = AppIcon::Reader,
        .capabilities = AppCapabilityStorage,
        .show_on_home = true,
    },
    {
        .id = "focus",
        .name = "Focus",
        .icon = AppIcon::Focus,
        .capabilities = AppCapabilityTime | AppCapabilityWakeTimer,
        .show_on_home = true,
    },
    {
        .id = "trmnl",
        .name = "TRMNL",
        .icon = AppIcon::Trmnl,
        .capabilities = AppCapabilityNetwork | AppCapabilityStorage | AppCapabilityBackgroundSchedule |
                        AppCapabilityWakeTimer | AppCapabilityBatteryState | AppCapabilityFullscreen |
                        AppCapabilityBacklight,
        .show_on_home = true,
    },
    {
        .id = "settings",
        .name = "Settings",
        .icon = AppIcon::Settings,
        .capabilities = AppCapabilityNone,
        .show_on_home = true,
    },
    {
        .id = "sync",
        .name = "Sync",
        .icon = AppIcon::Sync,
        .capabilities = AppCapabilityNetwork | AppCapabilityStorage | AppCapabilityBackgroundSchedule,
        .show_on_home = true,
    },
    {
        .id = "terrain",
        .name = "Terrain",
        .icon = AppIcon::Terrain,
        .capabilities = AppCapabilityNone,
        .show_on_home = true,
    },
};

}  // namespace

const AppDescriptor* app_registry(size_t* count)
{
    if (count != nullptr) {
        *count = sizeof(kRegisteredApps) / sizeof(kRegisteredApps[0]);
    }
    return kRegisteredApps;
}

const AppDescriptor* home_app_registry(size_t* count)
{
    return app_registry(count);
}

const AppDescriptor* find_app_by_id(const char* id)
{
    if (id == nullptr) {
        return nullptr;
    }

    size_t count = 0;
    const AppDescriptor* apps = app_registry(&count);
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(apps[i].id, id) == 0) {
            return &apps[i];
        }
    }
    return nullptr;
}

const AppDescriptor* find_app_by_icon(AppIcon icon)
{
    size_t count = 0;
    const AppDescriptor* apps = app_registry(&count);
    for (size_t i = 0; i < count; ++i) {
        if (apps[i].icon == icon) {
            return &apps[i];
        }
    }
    return nullptr;
}

}  // namespace paper_screen
