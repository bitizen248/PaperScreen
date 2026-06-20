#pragma once

#include "services/wifi_service.h"
#include "ui/home_screen.h"

namespace paper_screen {

class SettingsService;

enum class SettingRowAction {
    None,
    ToggleWifiEnabled,
    TestWifiConnection,
    ToggleTrmnlEnabled,
    ToggleTrmnlMode,
    RegenerateWallpaper,
};

struct SettingRowViewModel {
    const char* label = "";
    const char* value = "";
    SettingRowAction action = SettingRowAction::None;
    bool enabled = true;
};

struct SettingsViewModel {
    const char* title = "Settings";
    StatusBarViewModel status_bar;
    const SettingRowViewModel* rows = nullptr;
    int row_count = 0;
};

class SettingsScreen {
public:
    void set_state(const SettingsService& settings, const WifiSettings& wifi_settings, WifiStatus wifi_status);
    SettingsViewModel view_model() const;

private:
    void add_row(const char* label, const char* value, SettingRowAction action = SettingRowAction::None, bool enabled = true);

    SettingRowViewModel rows_[16];
    int row_count_ = 0;
    char network_value_[64] = {};
    char status_value_[24] = {};
    char signal_value_[20] = {};
    char internet_value_[12] = {};
};

}  // namespace paper_screen
