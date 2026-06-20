#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board/board_rtc.h"
#include "services/settings_service.h"
#include "services/wifi_service.h"

namespace paper_screen {

enum class TimeSyncState {
    Idle,
    ConnectingWifi,
    Syncing,
    Updated,
    Failed,
};

enum class TimeSyncError {
    None,
    WifiDisabled,
    WifiMissingCredentials,
    WifiConnectFailed,
    NetworkTimeout,
    InvalidNetworkTime,
    RtcWriteFailed,
    Unknown,
};

struct TimeStatus {
    TimeSyncState state = TimeSyncState::Idle;
    TimeSyncError last_error = TimeSyncError::None;
    bool rtc_initialized = false;
    bool rtc_valid = false;
    bool time_configured = false;
    int64_t current_epoch = 0;
    int64_t last_sync_epoch = 0;
    char timezone[48] = "UTC";
};

class TimeService {
public:
    void begin(BoardRtc& rtc, SettingsService& settings, WifiService& wifi);

    TimeStatus status() const;

    bool set_timezone(const char* timezone_id);
    const TimezoneOption* timezone_options(size_t* count) const;
    bool select_next_timezone();

    bool sync_now();
    bool sync_now(uint32_t timeout_ms);

    int64_t now_epoch() const;
    bool format_local_time(int64_t epoch, char* out, size_t out_size) const;
    bool format_local_clock(int64_t epoch, char* out, size_t out_size) const;

private:
    bool sync_system_time_to_rtc(int64_t epoch);
    bool apply_timezone() const;
    void refresh_status_from_settings();
    void set_failure(TimeSyncError error);

    BoardRtc* rtc_ = nullptr;
    SettingsService* settings_ = nullptr;
    WifiService* wifi_ = nullptr;
    TimeStatus status_;
};

}  // namespace paper_screen
