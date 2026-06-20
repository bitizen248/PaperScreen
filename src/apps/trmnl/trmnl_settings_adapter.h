#pragma once

#include <stdint.h>

#include "apps/trmnl/trmnl_app.h"
#include "services/battery_service.h"
#include "services/settings_service.h"
#include "services/trmnl_service.h"
#include "services/wifi_service.h"

namespace paper_screen {

struct TrmnlSettingsInputs {
    bool enabled = false;
    TrmnlMode mode = TrmnlMode::Playlist;
    const char* base_url = "https://trmnl.com";
    const char* api_key = "";
    uint32_t fallback_refresh_seconds = 1800;
    uint16_t battery_voltage_mv = 0;
    int32_t wifi_rssi = 0;
    uint16_t display_width = 960;
    uint16_t display_height = 540;
    bool disconnect_wifi_after_fetch = true;
    bool allow_insecure_https = true;
};

TrmnlSettings build_trmnl_settings(const TrmnlSettingsInputs& inputs, const TrmnlAppState& state);

TrmnlSettings make_trmnl_settings(const SettingsService& settings,
                                  const WifiService& wifi,
                                  const BatteryService& battery,
                                  const TrmnlAppState& state,
                                  uint16_t display_width,
                                  uint16_t display_height,
                                  bool disconnect_wifi_after_fetch);

}  // namespace paper_screen
