#include "apps/trmnl/trmnl_settings_adapter.h"

namespace paper_screen {

TrmnlSettings build_trmnl_settings(const TrmnlSettingsInputs& inputs, const TrmnlAppState& state)
{
    TrmnlSettings trmnl_settings;
    trmnl_settings.enabled = inputs.enabled;
    trmnl_settings.mode = inputs.mode;
    trmnl_settings.base_url = inputs.base_url;
    trmnl_settings.api_key = inputs.api_key;
    trmnl_settings.fallback_refresh_seconds = inputs.fallback_refresh_seconds;
    trmnl_settings.current_refresh_seconds = state.next_refresh_seconds > 0
        ? state.next_refresh_seconds
        : inputs.fallback_refresh_seconds;
    trmnl_settings.battery_voltage_mv = inputs.battery_voltage_mv;
    trmnl_settings.wifi_rssi = inputs.wifi_rssi;
    trmnl_settings.display_width = inputs.display_width;
    trmnl_settings.display_height = inputs.display_height;
    trmnl_settings.disconnect_wifi_after_fetch = inputs.disconnect_wifi_after_fetch;
    trmnl_settings.allow_insecure_https = inputs.allow_insecure_https;
    trmnl_settings.previous_image_url_hash = state.last_image_url_hash;
    trmnl_settings.skip_unchanged_image = state.image_rendered;
    return trmnl_settings;
}

}  // namespace paper_screen
