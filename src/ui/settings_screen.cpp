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

const char* trmnl_mode_label(TrmnlMode mode)
{
    return mode == TrmnlMode::Mirror ? "Mirror" : "Playlist";
}

}  // namespace

void SettingsScreen::set_state(const SettingsService& settings, const WifiSettings& wifi_settings, WifiStatus wifi_status)
{
    row_count_ = 0;

    const bool wifi_configured = wifi_settings.ssid[0] != '\0';
    std::snprintf(network_value_, sizeof(network_value_), "%s", wifi_configured ? wifi_settings.ssid : "Not configured");
    std::snprintf(status_value_, sizeof(status_value_), "%s", wifi_state_label(wifi_status.state));
    if (wifi_status.connected) {
        std::snprintf(signal_value_, sizeof(signal_value_), "%ld dBm", static_cast<long>(wifi_status.rssi));
        std::snprintf(internet_value_, sizeof(internet_value_), "%s", wifi_status.internet_access ? "Yes" : "No");
    } else {
        std::snprintf(signal_value_, sizeof(signal_value_), "--");
        std::snprintf(internet_value_, sizeof(internet_value_), "%s", wifi_status.internet_access ? "Yes" : "--");
    }

    const bool trmnl_ready = settings.trmnl_enabled()
        && settings.trmnl_api_key_configured()
        && settings.wifi_enabled();

    add_row("Wi-Fi", "", SettingRowAction::None, false);
    add_row("Enabled", on_off(settings.wifi_enabled()), SettingRowAction::ToggleWifiEnabled);
    add_row("Network", network_value_);
    add_row("Source", "Compiled");
    add_row("Status", status_value_);
    add_row("Signal", signal_value_);
    add_row("Internet", internet_value_);
    add_row("Test", "Connect", SettingRowAction::TestWifiConnection, settings.wifi_enabled());

    add_row("TRMNL", "", SettingRowAction::None, false);
    add_row("Enabled", on_off(settings.trmnl_enabled()), SettingRowAction::ToggleTrmnlEnabled);
    add_row("Mode", trmnl_mode_label(settings.trmnl_mode()), SettingRowAction::ToggleTrmnlMode);
    add_row("API Key", settings.trmnl_api_key_configured() ? "Set" : "Missing");
    add_row("Ready", yes_no(trmnl_ready));

    add_row("Wallpaper", "", SettingRowAction::None, false);
    add_row("Home background", "Regenerate", SettingRowAction::RegenerateWallpaper);
}

SettingsViewModel SettingsScreen::view_model() const
{
    SettingsViewModel model;
    model.status_bar.title = "Settings";
    model.status_bar.time = "--:--";
    model.status_bar.battery = "--%";
    model.rows = rows_;
    model.row_count = row_count_;
    return model;
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
