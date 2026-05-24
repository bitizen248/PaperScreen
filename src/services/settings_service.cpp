#include "settings_service.h"

#include <Arduino.h>
#include <cstring>

namespace {

constexpr char kTrmnlApiKey[] = "Cn2f9wIPjTNmxy4MPJfJ";

void copy_text(char* destination, size_t destination_size, const char* source)
{
    if (destination_size == 0) {
        return;
    }

    std::strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

bool trmnl_key_configured(const char* api_key)
{
    return api_key[0] != '\0' && std::strcmp(api_key, "PASTE_TRMNL_API_KEY_HERE") != 0;
}

}  // namespace

namespace paper_screen {

void SettingsService::begin()
{
    Serial.println("[settings] begin");
    refresh_policy_ = RefreshPolicy::Conservative;
    copy_text(trmnl_api_key_, sizeof(trmnl_api_key_), kTrmnlApiKey);
    trmnl_api_key_configured_ = trmnl_key_configured(trmnl_api_key_);
    trmnl_enabled_ = true;
    Serial.println("[settings] using default settings");
    Serial.println("[settings] ready");
}

RefreshPolicy SettingsService::refresh_policy() const
{
    return refresh_policy_;
}

bool SettingsService::wifi_enabled() const
{
    return wifi_enabled_;
}

void SettingsService::set_wifi_enabled(bool enabled)
{
    wifi_enabled_ = enabled;
    Serial.printf("[settings] wifi enabled=%s\n", wifi_enabled_ ? "on" : "off");
}

bool SettingsService::trmnl_enabled() const
{
    return trmnl_enabled_;
}

void SettingsService::set_trmnl_enabled(bool enabled)
{
    trmnl_enabled_ = enabled;
    Serial.printf("[settings] trmnl enabled=%s\n", trmnl_enabled_ ? "on" : "off");
}

TrmnlMode SettingsService::trmnl_mode() const
{
    return trmnl_mode_;
}

void SettingsService::set_trmnl_mode(TrmnlMode mode)
{
    trmnl_mode_ = mode;
    Serial.printf("[settings] trmnl mode=%s\n", trmnl_mode_ == TrmnlMode::Mirror ? "mirror" : "playlist");
}

bool SettingsService::trmnl_api_key_configured() const
{
    return trmnl_api_key_configured_;
}

const char* SettingsService::trmnl_api_key() const
{
    return trmnl_api_key_configured_ ? trmnl_api_key_ : "";
}

}  // namespace paper_screen
