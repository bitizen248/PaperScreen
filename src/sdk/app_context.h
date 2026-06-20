#pragma once

#include <stddef.h>
#include <stdint.h>

namespace paper_screen {

enum class AppNetworkState : uint8_t {
    Disabled,
    Idle,
    Connecting,
    Connected,
    Failed,
};

struct AppNetworkStatus {
    AppNetworkState state = AppNetworkState::Idle;
    bool connected = false;
    bool internet_access = false;
    int32_t rssi = 0;
};

struct AppBatteryStatus {
    bool initialized = false;
    bool valid = false;
    uint8_t percentage = 0;
    uint16_t voltage_mv = 0;
    bool charging = false;
    bool full = false;
    bool low = false;
    int64_t last_update_epoch = 0;
};

struct AppTimeStatus {
    bool rtc_initialized = false;
    bool rtc_valid = false;
    bool time_configured = false;
    int64_t current_epoch = 0;
    int64_t last_sync_epoch = 0;
};

class AppStorageApi {
public:
    virtual ~AppStorageApi() = default;

    virtual bool available() const = 0;
    virtual bool ensure_directory() = 0;
    virtual bool path(const char* leaf, char* out, size_t out_size) const = 0;
    virtual bool write_text_atomic(const char* leaf, const char* text) = 0;
    virtual bool write_file_atomic(const char* leaf, const uint8_t* data, size_t size) = 0;
    virtual bool file_size(const char* leaf, size_t* out_size) const = 0;
    virtual bool read_file(const char* leaf, uint8_t* out, size_t capacity, size_t* out_size) const = 0;
    virtual bool remove_file(const char* leaf) = 0;
    virtual bool append_text(const char* leaf, const char* text) = 0;
    virtual bool exists(const char* leaf) const = 0;
};

class AppNetworkApi {
public:
    virtual ~AppNetworkApi() = default;

    virtual AppNetworkStatus status() const = 0;
    virtual bool connect() = 0;
    virtual bool connect(uint32_t timeout_ms) = 0;
    virtual void disconnect() = 0;
    virtual bool confirm_internet_access(uint32_t timeout_ms = 5000) = 0;
    virtual bool is_connected() const = 0;
    virtual int32_t rssi() const = 0;
};

class AppClockApi {
public:
    virtual ~AppClockApi() = default;

    virtual AppTimeStatus status() const = 0;
    virtual int64_t now_epoch() const = 0;
    virtual bool format_local_clock(int64_t epoch, char* out, size_t out_size) const = 0;
    virtual bool format_local_time(int64_t epoch, char* out, size_t out_size) const = 0;
};

class AppBatteryApi {
public:
    virtual ~AppBatteryApi() = default;

    virtual AppBatteryStatus status() const = 0;
    virtual bool update() = 0;
    virtual bool format_percentage(char* out, size_t out_size) const = 0;
};

class AppPowerApi {
public:
    virtual ~AppPowerApi() = default;

    virtual bool woke_from_timer() const = 0;
    virtual void request_deep_sleep_for(uint32_t seconds) = 0;
    virtual bool consume_deep_sleep_request(uint32_t* seconds) = 0;
};

class AppLogApi {
public:
    virtual ~AppLogApi() = default;

    virtual void info(const char* message) const = 0;
    virtual void warn(const char* message) const = 0;
    virtual void error(const char* message) const = 0;
};

struct AppContext {
    AppContext(const char* app_id,
               AppStorageApi& storage,
               AppNetworkApi& network,
               AppClockApi& clock,
               AppBatteryApi& battery,
               AppPowerApi& power,
               AppLogApi& log)
        : app_id(app_id),
          storage(storage),
          network(network),
          clock(clock),
          battery(battery),
          power(power),
          log(log)
    {
    }

    const char* app_id;
    AppStorageApi& storage;
    AppNetworkApi& network;
    AppClockApi& clock;
    AppBatteryApi& battery;
    AppPowerApi& power;
    AppLogApi& log;
};

}  // namespace paper_screen
