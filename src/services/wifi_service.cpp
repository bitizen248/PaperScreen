#include "wifi_service.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <cstring>

namespace {

constexpr char kWifiSsid[] = "Bears Den 2.4";
constexpr char kWifiPassword[] = "dying-dusk-panicked";
constexpr char kInternetCheckHost[] = "connectivitycheck.gstatic.com";
constexpr uint16_t kInternetCheckPort = 80;
constexpr uint32_t kDefaultConnectTimeoutMs = 15000;
constexpr uint32_t kShortRetryDelayMs = 1000;

void copy_text(char* destination, size_t destination_size, const char* source)
{
    if (destination_size == 0) {
        return;
    }

    std::strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

bool is_placeholder(const char* value, const char* placeholder)
{
    return std::strcmp(value, placeholder) == 0;
}

bool has_configured_credentials(const paper_screen::WifiSettings& settings)
{
    return settings.ssid[0] != '\0'
        && !is_placeholder(settings.ssid, "YOUR_SSID")
        && !is_placeholder(settings.password, "YOUR_PASSWORD");
}

}  // namespace

namespace paper_screen {

void WifiService::begin()
{
    Serial.println("[wifi] begin");

    const WifiSettings current_settings = settings();
    if (!current_settings.enabled) {
        status_.state = WifiConnectionState::Disabled;
        status_.last_error = WifiError::Disabled;
        status_.connected = false;
        status_.internet_access = false;
        status_.rssi = 0;
        copy_text(status_.ssid, sizeof(status_.ssid), current_settings.ssid);
        WiFi.mode(WIFI_OFF);
        Serial.println("[wifi] disabled");
        return;
    }

    status_.state = WifiConnectionState::Idle;
    status_.last_error = WifiError::None;
    status_.connected = false;
    status_.internet_access = false;
    status_.rssi = 0;
    copy_text(status_.ssid, sizeof(status_.ssid), current_settings.ssid);

    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);

    if (!has_configured_credentials(current_settings)) {
        status_.state = WifiConnectionState::Failed;
        status_.last_error = WifiError::MissingCredentials;
        Serial.println("[wifi] missing credentials");
        return;
    }

    Serial.println("[wifi] ready");
}

void WifiService::set_enabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled_) {
        disconnect();
        status_.state = WifiConnectionState::Disabled;
        status_.last_error = WifiError::Disabled;
        Serial.println("[wifi] disabled");
        return;
    }

    if (status_.state == WifiConnectionState::Disabled) {
        status_.state = WifiConnectionState::Idle;
        status_.last_error = WifiError::None;
    }
    Serial.println("[wifi] enabled");
}

WifiSettings WifiService::settings() const
{
    WifiSettings current_settings;
    current_settings.enabled = enabled_;
    copy_text(current_settings.ssid, sizeof(current_settings.ssid), kWifiSsid);
    copy_text(current_settings.password, sizeof(current_settings.password), kWifiPassword);
    current_settings.auto_connect_for_desk = true;
    current_settings.disconnect_after_network_task = true;
    current_settings.connect_timeout_ms = kDefaultConnectTimeoutMs;
    return current_settings;
}

WifiStatus WifiService::status() const
{
    WifiStatus current_status = status_;
    current_status.connected = WiFi.status() == WL_CONNECTED;
    if (current_status.connected) {
        current_status.state = WifiConnectionState::Connected;
        current_status.last_error = WifiError::None;
        current_status.rssi = WiFi.RSSI();
        copy_text(current_status.ssid, sizeof(current_status.ssid), WiFi.SSID().c_str());
    } else if (status_.state == WifiConnectionState::Connected) {
        current_status.state = WifiConnectionState::Failed;
        current_status.last_error = WifiError::Disconnected;
        current_status.internet_access = false;
        current_status.rssi = 0;
    }

    return current_status;
}

bool WifiService::connect()
{
    return connect(settings().connect_timeout_ms);
}

bool WifiService::connect(uint32_t timeout_ms)
{
    const WifiSettings current_settings = settings();
    copy_text(status_.ssid, sizeof(status_.ssid), current_settings.ssid);

    if (!current_settings.enabled) {
        status_.state = WifiConnectionState::Disabled;
        status_.last_error = WifiError::Disabled;
        status_.connected = false;
        status_.internet_access = false;
        status_.rssi = 0;
        Serial.println("[wifi] connect skipped: disabled");
        return false;
    }

    if (!has_configured_credentials(current_settings)) {
        status_.state = WifiConnectionState::Failed;
        status_.last_error = WifiError::MissingCredentials;
        status_.connected = false;
        status_.internet_access = false;
        status_.rssi = 0;
        Serial.println("[wifi] connect skipped: missing credentials");
        return false;
    }

    if (WiFi.status() == WL_CONNECTED) {
        status_.state = WifiConnectionState::Connected;
        status_.last_error = WifiError::None;
        status_.connected = true;
        status_.internet_access = false;
        status_.rssi = WiFi.RSSI();
        copy_text(status_.ssid, sizeof(status_.ssid), WiFi.SSID().c_str());
        Serial.printf("[wifi] already connected rssi=%ld\n", static_cast<long>(status_.rssi));
        return true;
    }

    Serial.printf("[wifi] connecting ssid=%s\n", current_settings.ssid);
    status_.state = WifiConnectionState::Connecting;
    status_.last_error = WifiError::None;
    status_.connected = false;
    status_.internet_access = false;
    status_.rssi = 0;

    WiFi.mode(WIFI_STA);
    WiFi.begin(current_settings.ssid, current_settings.password);

    const unsigned long started_ms = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - started_ms >= timeout_ms) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            status_.state = WifiConnectionState::Failed;
            status_.last_error = WifiError::Timeout;
            status_.connected = false;
            status_.internet_access = false;
            status_.rssi = 0;
            Serial.println("[wifi] connect timeout");
            return false;
        }

        delay(kShortRetryDelayMs);
    }

    status_.state = WifiConnectionState::Connected;
    status_.last_error = WifiError::None;
    status_.connected = true;
    status_.internet_access = false;
    status_.rssi = WiFi.RSSI();
    copy_text(status_.ssid, sizeof(status_.ssid), WiFi.SSID().c_str());
    Serial.printf("[wifi] connected rssi=%ld\n", static_cast<long>(status_.rssi));
    return true;
}

void WifiService::disconnect()
{
    Serial.println("[wifi] disconnect");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    status_.state = settings().enabled ? WifiConnectionState::Idle : WifiConnectionState::Disabled;
    status_.last_error = WifiError::None;
    status_.connected = false;
    status_.internet_access = false;
    status_.rssi = 0;
}

bool WifiService::confirm_internet_access(uint32_t timeout_ms)
{
    status_.internet_access = false;
    if (WiFi.status() != WL_CONNECTED) {
        status_.last_error = WifiError::Disconnected;
        Serial.println("[wifi] internet check skipped: not connected");
        return false;
    }

    WiFiClient client;
    client.setTimeout(timeout_ms / 1000);
    Serial.printf("[wifi] internet check host=%s\n", kInternetCheckHost);
    if (!client.connect(kInternetCheckHost, kInternetCheckPort, timeout_ms)) {
        Serial.println("[wifi] internet check failed");
        return false;
    }

    client.stop();
    status_.internet_access = true;
    Serial.println("[wifi] internet access confirmed");
    return true;
}

bool WifiService::is_connected() const
{
    return WiFi.status() == WL_CONNECTED;
}

int32_t WifiService::rssi() const
{
    if (WiFi.status() != WL_CONNECTED) {
        return 0;
    }

    return WiFi.RSSI();
}

}  // namespace paper_screen
