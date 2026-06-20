#include "sdk/app_platform_context.h"

#include <Arduino.h>

namespace paper_screen {

namespace {

AppNetworkState map_network_state(WifiConnectionState state)
{
    switch (state) {
    case WifiConnectionState::Disabled:
        return AppNetworkState::Disabled;
    case WifiConnectionState::Idle:
        return AppNetworkState::Idle;
    case WifiConnectionState::Connecting:
        return AppNetworkState::Connecting;
    case WifiConnectionState::Connected:
        return AppNetworkState::Connected;
    case WifiConnectionState::Failed:
        return AppNetworkState::Failed;
    }
    return AppNetworkState::Failed;
}

}  // namespace

AppStorageServiceAdapter::AppStorageServiceAdapter(StorageService& storage, const char* app_id)
    : storage_(storage), app_id_(app_id)
{
}

bool AppStorageServiceAdapter::available() const
{
    return storage_.available();
}

bool AppStorageServiceAdapter::ensure_directory()
{
    return app_id_ != nullptr && storage_.ensure_app_directory(app_id_);
}

bool AppStorageServiceAdapter::path(const char* leaf, char* out, size_t out_size) const
{
    return app_id_ != nullptr && storage_.app_path(app_id_, leaf, out, out_size);
}

bool AppStorageServiceAdapter::write_text_atomic(const char* leaf, const char* text)
{
    char resolved[160] = {};
    if (!path(leaf, resolved, sizeof(resolved))) {
        return false;
    }
    return storage_.write_text_atomic(resolved, text);
}

bool AppStorageServiceAdapter::write_file_atomic(const char* leaf, const uint8_t* data, size_t size)
{
    char resolved[160] = {};
    if (!path(leaf, resolved, sizeof(resolved))) {
        return false;
    }
    return storage_.write_file_atomic(resolved, data, size);
}

bool AppStorageServiceAdapter::file_size(const char* leaf, size_t* out_size) const
{
    char resolved[160] = {};
    if (!path(leaf, resolved, sizeof(resolved))) {
        return false;
    }
    return storage_.file_size(resolved, out_size);
}

bool AppStorageServiceAdapter::read_file(const char* leaf, uint8_t* out, size_t capacity, size_t* out_size) const
{
    char resolved[160] = {};
    if (!path(leaf, resolved, sizeof(resolved))) {
        return false;
    }
    return storage_.read_file(resolved, out, capacity, out_size);
}

bool AppStorageServiceAdapter::remove_file(const char* leaf)
{
    char resolved[160] = {};
    if (!path(leaf, resolved, sizeof(resolved))) {
        return false;
    }
    return storage_.remove_file(resolved);
}

bool AppStorageServiceAdapter::append_text(const char* leaf, const char* text)
{
    char resolved[160] = {};
    if (!path(leaf, resolved, sizeof(resolved))) {
        return false;
    }
    return storage_.append_text(resolved, text);
}

bool AppStorageServiceAdapter::exists(const char* leaf) const
{
    char resolved[160] = {};
    if (!path(leaf, resolved, sizeof(resolved))) {
        return false;
    }
    return storage_.exists(resolved);
}

AppNetworkServiceAdapter::AppNetworkServiceAdapter(WifiService& wifi)
    : wifi_(wifi)
{
}

AppNetworkStatus AppNetworkServiceAdapter::status() const
{
    const WifiStatus wifi_status = wifi_.status();
    AppNetworkStatus status;
    status.state = map_network_state(wifi_status.state);
    status.connected = wifi_status.connected;
    status.internet_access = wifi_status.internet_access;
    status.rssi = wifi_status.rssi;
    return status;
}

bool AppNetworkServiceAdapter::connect()
{
    return wifi_.connect();
}

bool AppNetworkServiceAdapter::connect(uint32_t timeout_ms)
{
    return wifi_.connect(timeout_ms);
}

void AppNetworkServiceAdapter::disconnect()
{
    wifi_.disconnect();
}

bool AppNetworkServiceAdapter::confirm_internet_access(uint32_t timeout_ms)
{
    return wifi_.confirm_internet_access(timeout_ms);
}

bool AppNetworkServiceAdapter::is_connected() const
{
    return wifi_.is_connected();
}

int32_t AppNetworkServiceAdapter::rssi() const
{
    return wifi_.rssi();
}

AppClockServiceAdapter::AppClockServiceAdapter(TimeService& time)
    : time_(time)
{
}

AppTimeStatus AppClockServiceAdapter::status() const
{
    const TimeStatus time_status = time_.status();
    AppTimeStatus status;
    status.rtc_initialized = time_status.rtc_initialized;
    status.rtc_valid = time_status.rtc_valid;
    status.time_configured = time_status.time_configured;
    status.current_epoch = time_status.current_epoch;
    status.last_sync_epoch = time_status.last_sync_epoch;
    return status;
}

int64_t AppClockServiceAdapter::now_epoch() const
{
    return time_.now_epoch();
}

bool AppClockServiceAdapter::format_local_clock(int64_t epoch, char* out, size_t out_size) const
{
    return time_.format_local_clock(epoch, out, out_size);
}

bool AppClockServiceAdapter::format_local_time(int64_t epoch, char* out, size_t out_size) const
{
    return time_.format_local_time(epoch, out, out_size);
}

AppBatteryServiceAdapter::AppBatteryServiceAdapter(BatteryService& battery, TimeService& time)
    : battery_(battery), time_(time)
{
}

AppBatteryStatus AppBatteryServiceAdapter::status() const
{
    const BatteryStatus battery_status = battery_.status();
    AppBatteryStatus status;
    status.initialized = battery_status.initialized;
    status.valid = battery_status.valid;
    status.percentage = battery_status.percentage;
    status.voltage_mv = battery_status.voltage_mv;
    status.charging = battery_status.charging;
    status.full = battery_status.full;
    status.low = battery_status.low;
    status.last_update_epoch = battery_status.last_update_epoch;
    return status;
}

bool AppBatteryServiceAdapter::update()
{
    return battery_.update(time_.now_epoch());
}

bool AppBatteryServiceAdapter::format_percentage(char* out, size_t out_size) const
{
    return battery_.format_percentage(out, out_size);
}

AppPowerServiceAdapter::AppPowerServiceAdapter(PowerService& power)
    : power_(power)
{
}

bool AppPowerServiceAdapter::woke_from_timer() const
{
    return power_.woke_from_timer();
}

void AppPowerServiceAdapter::request_deep_sleep_for(uint32_t seconds)
{
    sleep_requested_ = true;
    sleep_seconds_ = seconds;
}

bool AppPowerServiceAdapter::consume_deep_sleep_request(uint32_t* seconds)
{
    if (!sleep_requested_) {
        return false;
    }
    if (seconds != nullptr) {
        *seconds = sleep_seconds_;
    }
    sleep_requested_ = false;
    sleep_seconds_ = 0;
    return true;
}

AppSerialLogAdapter::AppSerialLogAdapter(const char* app_id)
    : app_id_(app_id != nullptr ? app_id : "app")
{
}

void AppSerialLogAdapter::info(const char* message) const
{
    Serial.printf("[app:%s] %s\n", app_id_, message != nullptr ? message : "");
}

void AppSerialLogAdapter::warn(const char* message) const
{
    Serial.printf("[app:%s] warning: %s\n", app_id_, message != nullptr ? message : "");
}

void AppSerialLogAdapter::error(const char* message) const
{
    Serial.printf("[app:%s] error: %s\n", app_id_, message != nullptr ? message : "");
}

AppPlatformContext::AppPlatformContext(const char* app_id,
                                       StorageService& storage_service,
                                       WifiService& wifi,
                                       TimeService& time,
                                       BatteryService& battery_service,
                                       PowerService& power_service)
    : storage(storage_service, app_id),
      network(wifi),
      clock(time),
      battery(battery_service, time),
      power(power_service),
      log(app_id),
      context(app_id, storage, network, clock, battery, power, log)
{
}

}  // namespace paper_screen
