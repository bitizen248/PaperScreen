#include "settings_screen.h"

#include "services/settings_service.h"

#include <cstdio>

namespace paper_screen {

namespace {

const char* on_off(bool value)
{
    return value ? "On" : "Off";
}

const char* yes_no(bool value)
{
    return value ? "Yes" : "No";
}

const char* wifi_state_label(WifiConnectionState state)
{
    switch (state) {
    case WifiConnectionState::Disabled:
        return "Disabled";
    case WifiConnectionState::Idle:
        return "Idle";
    case WifiConnectionState::Connecting:
        return "Connecting";
    case WifiConnectionState::Connected:
        return "Connected";
    case WifiConnectionState::Failed:
        return "Failed";
    }
    return "Unknown";
}

const char* time_sync_state_label(TimeSyncState state, TimeSyncError error)
{
    if (state == TimeSyncState::Failed) {
        switch (error) {
        case TimeSyncError::WifiDisabled:
            return "Wi-Fi disabled";
        case TimeSyncError::WifiMissingCredentials:
            return "Wi-Fi missing";
        case TimeSyncError::WifiConnectFailed:
            return "Connect failed";
        case TimeSyncError::NetworkTimeout:
            return "Server failed";
        case TimeSyncError::InvalidNetworkTime:
            return "Invalid time";
        case TimeSyncError::RtcWriteFailed:
            return "RTC failed";
        case TimeSyncError::None:
        case TimeSyncError::Unknown:
            return "Failed";
        }
    }

    switch (state) {
    case TimeSyncState::Idle:
        return "Run";
    case TimeSyncState::ConnectingWifi:
        return "Connecting";
    case TimeSyncState::Syncing:
        return "Syncing";
    case TimeSyncState::Updated:
        return "Updated";
    case TimeSyncState::Failed:
        return "Failed";
    }
    return "Run";
}

const char* trmnl_mode_label(TrmnlMode mode)
{
    return mode == TrmnlMode::Mirror ? "Mirror" : "Playlist";
}

const char* page_title(SettingsPage page)
{
    switch (page) {
    case SettingsPage::Main:
        return "Settings";
    case SettingsPage::Time:
        return "Time";
    case SettingsPage::Wifi:
        return "Wi-Fi";
    case SettingsPage::Trmnl:
        return "TRMNL";
    }
    return "Settings";
}

}  // namespace

void SettingsScreen::open_page(SettingsPage page)
{
    page_ = page;
}

void SettingsScreen::open_main_page()
{
    page_ = SettingsPage::Main;
}

SettingsPage SettingsScreen::page() const
{
    return page_;
}

void SettingsScreen::set_state(const SettingsService& settings,
                               const WifiSettings& wifi_settings,
                               WifiStatus wifi_status,
                               const TimeService& time)
{
    row_count_ = 0;

    const TimeStatus time_status = time.status();
    std::snprintf(timezone_value_, sizeof(timezone_value_), "%s", time_status.timezone);
    if (!time.format_local_time(time_status.current_epoch, current_time_value_, sizeof(current_time_value_))) {
        std::snprintf(current_time_value_, sizeof(current_time_value_), "--");
    }
    if (!time_status.rtc_initialized) {
        std::snprintf(rtc_status_value_, sizeof(rtc_status_value_), "Error");
    } else {
        std::snprintf(rtc_status_value_, sizeof(rtc_status_value_), "%s", time_status.rtc_valid ? "Set" : "Not set");
    }
    if (!time.format_local_time(time_status.last_sync_epoch, last_sync_value_, sizeof(last_sync_value_))) {
        std::snprintf(last_sync_value_, sizeof(last_sync_value_), "Never");
    }
    std::snprintf(sync_status_value_,
                  sizeof(sync_status_value_),
                  "%s",
                  time_sync_state_label(time_status.state, time_status.last_error));

    const bool wifi_configured = wifi_settings.ssid[0] != '\0';
    std::snprintf(network_value_, sizeof(network_value_), "%s", wifi_configured ? wifi_settings.ssid : "Not configured");
    std::snprintf(status_value_, sizeof(status_value_), "%s", wifi_state_label(wifi_status.state));

    switch (page_) {
    case SettingsPage::Main:
        build_main_page(settings, wifi_settings, time_status);
        return;
    case SettingsPage::Time:
        build_time_page(settings);
        return;
    case SettingsPage::Wifi:
        build_wifi_page(settings);
        return;
    case SettingsPage::Trmnl:
        build_trmnl_page(settings);
        return;
    }
}

SettingsViewModel SettingsScreen::view_model() const
{
    SettingsViewModel model;
    model.status_bar.title = page_title(page_);
    model.status_bar.time = "--:--";
    model.status_bar.battery = "--%";
    model.rows = rows_;
    model.row_count = row_count_;
    return model;
}

void SettingsScreen::build_main_page(const SettingsService& settings,
                                     const WifiSettings& wifi_settings,
                                     const TimeStatus& time_status)
{
    const bool trmnl_ready = settings.trmnl_enabled()
        && settings.trmnl_api_key_configured()
        && settings.wifi_enabled();

    add_row("Time", time_status.rtc_valid ? "Set" : "Needs setup", SettingRowAction::OpenTimeSettings);
    add_row("Wi-Fi", wifi_settings.enabled ? status_value_ : "Off", SettingRowAction::OpenWifiSettings);
    add_row("TRMNL", trmnl_ready ? "Ready" : "Not ready", SettingRowAction::OpenTrmnlSettings);
    add_row("USB Drive", "Connect", SettingRowAction::OpenUsbDrive);
}

void SettingsScreen::build_time_page(const SettingsService& settings)
{
    add_row("Back", "Settings", SettingRowAction::BackToSettings);
    add_row("Timezone", timezone_value_, SettingRowAction::SelectTimezone);
    add_row("Current", current_time_value_);
    add_row("RTC", rtc_status_value_);
    add_row("Last sync", last_sync_value_);
    add_row("Sync time", sync_status_value_, SettingRowAction::SyncRtcTime, settings.wifi_enabled());
}

void SettingsScreen::build_wifi_page(const SettingsService& settings)
{
    add_row("Back", "Settings", SettingRowAction::BackToSettings);
    add_row("Enabled", on_off(settings.wifi_enabled()), SettingRowAction::ToggleWifiEnabled);
    add_row("Network", network_value_);
    add_row("Status", status_value_);
    add_row("Test", "Connect", SettingRowAction::TestWifiConnection, settings.wifi_enabled());
}

void SettingsScreen::build_trmnl_page(const SettingsService& settings)
{
    const bool trmnl_ready = settings.trmnl_enabled()
        && settings.trmnl_api_key_configured()
        && settings.wifi_enabled();

    add_row("Back", "Settings", SettingRowAction::BackToSettings);
    add_row("Enabled", on_off(settings.trmnl_enabled()), SettingRowAction::ToggleTrmnlEnabled);
    add_row("Mode", trmnl_mode_label(settings.trmnl_mode()), SettingRowAction::ToggleTrmnlMode);
    add_row("Desk on charge", on_off(settings.trmnl_autonomous_enabled()), SettingRowAction::ToggleTrmnlAutonomous);
    add_row("API Key", settings.trmnl_api_key_configured() ? "Set" : "Missing");
    add_row("Cache", "Clear", SettingRowAction::ClearTrmnlCache);
    add_row("Ready", yes_no(trmnl_ready));

    add_row("Wallpaper", "", SettingRowAction::None, false);
    // add_row("Home background", "Regenerate", SettingRowAction::RegenerateWallpaper);
}

void SettingsScreen::add_row(const char* label, const char* value, SettingRowAction action, bool enabled)
{
    if (row_count_ >= static_cast<int>(sizeof(rows_) / sizeof(rows_[0]))) {
        return;
    }

    rows_[row_count_].label = label;
    rows_[row_count_].value = value;
    rows_[row_count_].action = action;
    rows_[row_count_].enabled = enabled;
    ++row_count_;
}

}  // namespace paper_screen
