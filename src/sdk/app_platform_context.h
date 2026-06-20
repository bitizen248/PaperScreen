#pragma once

#include "sdk/app_context.h"
#include "services/battery_service.h"
#include "services/power_service.h"
#include "services/storage_service.h"
#include "services/time_service.h"
#include "services/wifi_service.h"

namespace paper_screen {

class AppStorageServiceAdapter final : public AppStorageApi {
public:
    AppStorageServiceAdapter(StorageService& storage, const char* app_id);

    bool available() const override;
    bool ensure_directory() override;
    bool path(const char* leaf, char* out, size_t out_size) const override;
    bool write_text_atomic(const char* leaf, const char* text) override;
    bool write_file_atomic(const char* leaf, const uint8_t* data, size_t size) override;
    bool file_size(const char* leaf, size_t* out_size) const override;
    bool read_file(const char* leaf, uint8_t* out, size_t capacity, size_t* out_size) const override;
    bool remove_file(const char* leaf) override;
    bool append_text(const char* leaf, const char* text) override;
    bool exists(const char* leaf) const override;

private:
    StorageService& storage_;
    const char* app_id_;
};

class AppNetworkServiceAdapter final : public AppNetworkApi {
public:
    explicit AppNetworkServiceAdapter(WifiService& wifi);

    AppNetworkStatus status() const override;
    bool connect() override;
    bool connect(uint32_t timeout_ms) override;
    void disconnect() override;
    bool confirm_internet_access(uint32_t timeout_ms = 5000) override;
    bool is_connected() const override;
    int32_t rssi() const override;

private:
    WifiService& wifi_;
};

class AppClockServiceAdapter final : public AppClockApi {
public:
    explicit AppClockServiceAdapter(TimeService& time);

    AppTimeStatus status() const override;
    int64_t now_epoch() const override;
    bool format_local_clock(int64_t epoch, char* out, size_t out_size) const override;
    bool format_local_time(int64_t epoch, char* out, size_t out_size) const override;

private:
    TimeService& time_;
};

class AppBatteryServiceAdapter final : public AppBatteryApi {
public:
    AppBatteryServiceAdapter(BatteryService& battery, TimeService& time);

    AppBatteryStatus status() const override;
    bool update() override;
    bool format_percentage(char* out, size_t out_size) const override;

private:
    BatteryService& battery_;
    TimeService& time_;
};

class AppPowerServiceAdapter final : public AppPowerApi {
public:
    explicit AppPowerServiceAdapter(PowerService& power);

    bool woke_from_timer() const override;
    void request_deep_sleep_for(uint32_t seconds) override;
    bool consume_deep_sleep_request(uint32_t* seconds) override;

private:
    PowerService& power_;
    bool sleep_requested_ = false;
    uint32_t sleep_seconds_ = 0;
};

class AppSerialLogAdapter final : public AppLogApi {
public:
    explicit AppSerialLogAdapter(const char* app_id);

    void info(const char* message) const override;
    void warn(const char* message) const override;
    void error(const char* message) const override;

private:
    const char* app_id_;
};

struct AppPlatformContext {
    AppPlatformContext(const char* app_id,
                       StorageService& storage,
                       WifiService& wifi,
                       TimeService& time,
                       BatteryService& battery,
                       PowerService& power);

    AppStorageServiceAdapter storage;
    AppNetworkServiceAdapter network;
    AppClockServiceAdapter clock;
    AppBatteryServiceAdapter battery;
    AppPowerServiceAdapter power;
    AppSerialLogAdapter log;
    AppContext context;
};

}  // namespace paper_screen
