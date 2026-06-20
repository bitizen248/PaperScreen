#include "apps/trmnl/trmnl_settings_adapter.h"

namespace paper_screen {

TrmnlSettings make_trmnl_settings(const SettingsService& settings,
                                  const WifiService& wifi,
                                  const BatteryService& battery,
                                  const TrmnlAppState& state,
                                  uint16_t display_width,
                                  uint16_t display_height,
                                  bool disconnect_wifi_after_fetch)
{
    const BatteryStatus battery_status = battery.status();

    TrmnlSettingsInputs inputs;
    inputs.enabled = settings.trmnl_enabled();
    inputs.mode = settings.trmnl_mode();
    inputs.base_url = "https://trmnl.com";
    inputs.api_key = settings.trmnl_api_key();
    inputs.fallback_refresh_seconds = 1800;
    inputs.battery_voltage_mv = battery_status.valid ? battery_status.voltage_mv : 0;
    inputs.wifi_rssi = wifi.rssi();
    inputs.display_width = display_width;
    inputs.display_height = display_height;
    inputs.disconnect_wifi_after_fetch = disconnect_wifi_after_fetch;
    inputs.allow_insecure_https = true;
    return build_trmnl_settings(inputs, state);
}

}  // namespace paper_screen
