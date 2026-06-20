#include "settings_service.h"

#include <Arduino.h>
#include <cstring>

namespace {

#ifndef PAPER_SCREEN_TRMNL_API_KEY
#define PAPER_SCREEN_TRMNL_API_KEY "PASTE_TRMNL_API_KEY_HERE"
#endif

constexpr char kTrmnlApiKey[] = PAPER_SCREEN_TRMNL_API_KEY;
constexpr paper_screen::TimezoneOption kTimezoneOptions[] = {
    {"UTC", "UTC", "UTC0"},
    {"Europe/Amsterdam", "Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"America/New_York", "New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
};

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

const paper_screen::TimezoneOption* find_timezone_option(const char* timezone_id)
{
    if (timezone_id == nullptr) {
        return nullptr;
    }

    for (const auto& option : kTimezoneOptions) {
        if (std::strcmp(option.id, timezone_id) == 0) {
            return &option;
        }
    }
    return nullptr;
}

}  // namespace

namespace paper_screen {

void SettingsService::begin()
{
    Serial.println("[settings] begin");
    refresh_policy_ = RefreshPolicy::PartialWhenSafe;
    copy_text(trmnl_api_key_, sizeof(trmnl_api_key_), kTrmnlApiKey);
    trmnl_api_key_configured_ = trmnl_key_configured(trmnl_api_key_);
    trmnl_enabled_ = true;
    Serial.println("[settings] using simple in-memory settings");
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

bool SettingsService::trmnl_autonomous_enabled() const
{
    return trmnl_autonomous_enabled_;
}

void SettingsService::set_trmnl_autonomous_enabled(bool enabled)
{
    trmnl_autonomous_enabled_ = enabled;
    Serial.printf("[settings] trmnl autonomous=%s\n", trmnl_autonomous_enabled_ ? "on" : "off");
}

bool SettingsService::trmnl_api_key_configured() const
{
    return trmnl_api_key_configured_;
}

const char* SettingsService::trmnl_api_key() const
{
    return trmnl_api_key_configured_ ? trmnl_api_key_ : "";
}

const TimeSettings& SettingsService::time_settings() const
{
    return time_settings_;
}

const TimezoneOption* SettingsService::timezone_options(size_t* count) const
{
    if (count != nullptr) {
        *count = sizeof(kTimezoneOptions) / sizeof(kTimezoneOptions[0]);
    }
    return kTimezoneOptions;
}

const TimezoneOption* SettingsService::timezone_option(const char* timezone_id) const
{
    return find_timezone_option(timezone_id);
}

bool SettingsService::set_timezone(const char* timezone_id)
{
    const TimezoneOption* option = find_timezone_option(timezone_id);
    if (option == nullptr) {
        Serial.printf("[settings] timezone rejected=%s\n", timezone_id == nullptr ? "(null)" : timezone_id);
        return false;
    }

    copy_text(time_settings_.timezone, sizeof(time_settings_.timezone), option->id);
    Serial.printf("[settings] timezone=%s\n", time_settings_.timezone);
    return true;
}

void SettingsService::mark_time_synced(int64_t sync_epoch)
{
    time_settings_.time_configured = true;
    time_settings_.last_sync_epoch = sync_epoch;
    Serial.printf("[settings] time synced epoch=%lld\n", static_cast<long long>(sync_epoch));
}

}  // namespace paper_screen
