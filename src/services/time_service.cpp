#include "time_service.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

namespace {

constexpr uint32_t kTimeSyncTimeoutMs = 20000;
constexpr uint32_t kSntpPollIntervalMs = 250;
constexpr int64_t kMinimumValidEpoch = 1704067200;  // 2024-01-01T00:00:00Z

void copy_text(char* destination, size_t destination_size, const char* source)
{
    if (destination_size == 0) {
        return;
    }

    std::strncpy(destination, source == nullptr ? "" : source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

int64_t epoch_from_utc(const paper_screen::RtcDateTime& value)
{
    const int64_t days = days_from_civil(value.year, static_cast<unsigned>(value.month), static_cast<unsigned>(value.day));
    return (days * 86400)
        + (static_cast<int64_t>(value.hour) * 3600)
        + (static_cast<int64_t>(value.minute) * 60)
        + value.second;
}

paper_screen::RtcDateTime rtc_from_epoch(int64_t epoch)
{
    time_t raw = static_cast<time_t>(epoch);
    tm utc = {};
    gmtime_r(&raw, &utc);

    paper_screen::RtcDateTime value;
    value.year = utc.tm_year + 1900;
    value.month = utc.tm_mon + 1;
    value.day = utc.tm_mday;
    value.hour = utc.tm_hour;
    value.minute = utc.tm_min;
    value.second = utc.tm_sec;
    return value;
}

bool system_time_valid()
{
    return static_cast<int64_t>(time(nullptr)) >= kMinimumValidEpoch;
}

}  // namespace

namespace paper_screen {

void TimeService::begin(BoardRtc& rtc, SettingsService& settings, WifiService& wifi)
{
    Serial.println("[time] begin");
    rtc_ = &rtc;
    settings_ = &settings;
    wifi_ = &wifi;

    refresh_status_from_settings();
    apply_timezone();

    status_.rtc_initialized = rtc_->initialized();
    status_.rtc_valid = rtc_->is_valid();
    if (status_.rtc_valid) {
        RtcDateTime rtc_time;
        if (rtc_->read_utc(&rtc_time)) {
            const int64_t epoch = epoch_from_utc(rtc_time);
            if (epoch >= kMinimumValidEpoch) {
                timeval tv;
                tv.tv_sec = static_cast<time_t>(epoch);
                tv.tv_usec = 0;
                settimeofday(&tv, nullptr);
                status_.current_epoch = epoch;
                Serial.printf("[time] system time restored epoch=%lld\n", static_cast<long long>(epoch));
            }
        }
    }

    Serial.println("[time] ready");
}

TimeStatus TimeService::status() const
{
    TimeStatus current = status_;
    if (rtc_ != nullptr) {
        current.rtc_initialized = rtc_->initialized();
        current.rtc_valid = rtc_->is_valid();
    }
    if (system_time_valid()) {
        current.current_epoch = static_cast<int64_t>(time(nullptr));
    }
    if (settings_ != nullptr) {
        const TimeSettings& settings = settings_->time_settings();
        current.time_configured = settings.time_configured;
        current.last_sync_epoch = settings.last_sync_epoch;
        copy_text(current.timezone, sizeof(current.timezone), settings.timezone);
    }
    return current;
}

bool TimeService::set_timezone(const char* timezone_id)
{
    if (settings_ == nullptr || !settings_->set_timezone(timezone_id)) {
        return false;
    }

    refresh_status_from_settings();
    return apply_timezone();
}

const TimezoneOption* TimeService::timezone_options(size_t* count) const
{
    if (settings_ == nullptr) {
        if (count != nullptr) {
            *count = 0;
        }
        return nullptr;
    }
    return settings_->timezone_options(count);
}

bool TimeService::select_next_timezone()
{
    if (settings_ == nullptr) {
        return false;
    }

    size_t count = 0;
    const TimezoneOption* options = settings_->timezone_options(&count);
    if (options == nullptr || count == 0) {
        return false;
    }

    const char* current = settings_->time_settings().timezone;
    size_t next = 0;
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(options[i].id, current) == 0) {
            next = (i + 1) % count;
            break;
        }
    }

    return set_timezone(options[next].id);
}

bool TimeService::sync_now()
{
    return sync_now(kTimeSyncTimeoutMs);
}

bool TimeService::sync_now(uint32_t timeout_ms)
{
    if (rtc_ == nullptr || settings_ == nullptr || wifi_ == nullptr) {
        set_failure(TimeSyncError::Unknown);
        return false;
    }

    refresh_status_from_settings();
    apply_timezone();

    const WifiSettings wifi_settings = wifi_->settings();
    if (!wifi_settings.enabled) {
        set_failure(TimeSyncError::WifiDisabled);
        return false;
    }
    if (wifi_settings.ssid[0] == '\0') {
        set_failure(TimeSyncError::WifiMissingCredentials);
        return false;
    }

    Serial.println("[time] sync start");
    status_.state = TimeSyncState::ConnectingWifi;
    status_.last_error = TimeSyncError::None;

    if (!wifi_->connect(wifi_settings.connect_timeout_ms)) {
        const WifiStatus wifi_status = wifi_->status();
        set_failure(wifi_status.last_error == WifiError::MissingCredentials
                        ? TimeSyncError::WifiMissingCredentials
                        : TimeSyncError::WifiConnectFailed);
        return false;
    }

    status_.state = TimeSyncState::Syncing;
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    configTime(0, 0, "pool.ntp.org", "time.google.com");

    const unsigned long started_ms = millis();
    int64_t network_epoch = 0;
    while (millis() - started_ms < timeout_ms) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            network_epoch = static_cast<int64_t>(time(nullptr));
            break;
        }
        delay(kSntpPollIntervalMs);
    }

    if (network_epoch < kMinimumValidEpoch) {
        if (wifi_settings.disconnect_after_network_task) {
            wifi_->disconnect();
        }
        set_failure(TimeSyncError::NetworkTimeout);
        return false;
    }

    if (!sync_system_time_to_rtc(network_epoch)) {
        if (wifi_settings.disconnect_after_network_task) {
            wifi_->disconnect();
        }
        set_failure(TimeSyncError::RtcWriteFailed);
        return false;
    }

    settings_->mark_time_synced(network_epoch);
    if (wifi_settings.disconnect_after_network_task) {
        wifi_->disconnect();
    }

    status_.state = TimeSyncState::Updated;
    status_.last_error = TimeSyncError::None;
    status_.rtc_valid = true;
    status_.time_configured = true;
    status_.current_epoch = network_epoch;
    status_.last_sync_epoch = network_epoch;
    Serial.printf("[time] sync complete epoch=%lld\n", static_cast<long long>(network_epoch));
    return true;
}

int64_t TimeService::now_epoch() const
{
    if (system_time_valid()) {
        return static_cast<int64_t>(time(nullptr));
    }

    if (rtc_ == nullptr || !rtc_->is_valid()) {
        return 0;
    }

    RtcDateTime rtc_time;
    if (!rtc_->read_utc(&rtc_time)) {
        return 0;
    }
    return epoch_from_utc(rtc_time);
}

bool TimeService::format_local_time(int64_t epoch, char* out, size_t out_size) const
{
    if (out == nullptr || out_size == 0 || epoch < kMinimumValidEpoch) {
        return false;
    }

    apply_timezone();

    time_t raw = static_cast<time_t>(epoch);
    tm local = {};
    if (localtime_r(&raw, &local) == nullptr) {
        return false;
    }

    return std::strftime(out, out_size, "%Y-%m-%d %H:%M", &local) > 0;
}

bool TimeService::format_local_clock(int64_t epoch, char* out, size_t out_size) const
{
    if (out == nullptr || out_size == 0 || epoch < kMinimumValidEpoch) {
        return false;
    }

    apply_timezone();

    time_t raw = static_cast<time_t>(epoch);
    tm local = {};
    if (localtime_r(&raw, &local) == nullptr) {
        return false;
    }

    return std::strftime(out, out_size, "%H:%M", &local) > 0;
}

bool TimeService::sync_system_time_to_rtc(int64_t epoch)
{
    if (rtc_ == nullptr || epoch < kMinimumValidEpoch) {
        return false;
    }

    const RtcDateTime value = rtc_from_epoch(epoch);
    if (!rtc_->write_utc(value)) {
        return false;
    }

    timeval tv;
    tv.tv_sec = static_cast<time_t>(epoch);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    return true;
}

bool TimeService::apply_timezone() const
{
    if (settings_ == nullptr) {
        return false;
    }

    const TimezoneOption* option = settings_->timezone_option(settings_->time_settings().timezone);
    if (option == nullptr) {
        return false;
    }

    setenv("TZ", option->posix_tz, 1);
    tzset();
    return true;
}

void TimeService::refresh_status_from_settings()
{
    if (settings_ == nullptr) {
        return;
    }

    const TimeSettings& settings = settings_->time_settings();
    status_.time_configured = settings.time_configured;
    status_.last_sync_epoch = settings.last_sync_epoch;
    copy_text(status_.timezone, sizeof(status_.timezone), settings.timezone);
}

void TimeService::set_failure(TimeSyncError error)
{
    status_.state = TimeSyncState::Failed;
    status_.last_error = error;
    status_.current_epoch = now_epoch();
    if (rtc_ != nullptr) {
        status_.rtc_initialized = rtc_->initialized();
        status_.rtc_valid = rtc_->is_valid();
    }
    refresh_status_from_settings();
    Serial.printf("[time] sync failed error=%d\n", static_cast<int>(error));
}

}  // namespace paper_screen
