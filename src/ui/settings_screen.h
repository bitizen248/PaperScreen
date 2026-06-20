#pragma once

#include "services/time_service.h"
#include "services/wifi_service.h"
#include "ui/home_screen.h"

namespace paper_screen {

class SettingsService;

enum class SettingRowAction {
    None,
    OpenTimeSettings,
    OpenWifiSettings,
    OpenTrmnlSettings,
    BackToSettings,
    ToggleWifiEnabled,
    TestWifiConnection,
    SelectTimezone,
    SyncRtcTime,
    ToggleTrmnlEnabled,
    ToggleTrmnlMode,
    ToggleTrmnlAutonomous,
    ClearTrmnlCache,
    OpenUsbDrive,
};

enum class SettingsPage {
    Main,
    Time,
    Wifi,
    Trmnl,
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
    void open_page(SettingsPage page);
    void open_main_page();
    SettingsPage page() const;
    void set_state(const SettingsService& settings,
                   const WifiSettings& wifi_settings,
                   WifiStatus wifi_status,
                   const TimeService& time);
    SettingsViewModel view_model() const;

private:
    void add_row(const char* label, const char* value, SettingRowAction action = SettingRowAction::None, bool enabled = true);
    void build_main_page(const SettingsService& settings, const WifiSettings& wifi_settings, const TimeStatus& time_status);
    void build_time_page(const SettingsService& settings);
    void build_wifi_page(const SettingsService& settings);
    void build_trmnl_page(const SettingsService& settings);

    SettingRowViewModel rows_[18];
    int row_count_ = 0;
    SettingsPage page_ = SettingsPage::Main;
    char network_value_[64] = {};
    char status_value_[24] = {};
    char timezone_value_[48] = {};
    char current_time_value_[24] = {};
    char rtc_status_value_[12] = {};
    char last_sync_value_[24] = {};
    char sync_status_value_[24] = {};
};

}  // namespace paper_screen
