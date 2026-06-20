#pragma once

#include "display/refresh_policy.h"

#include <stddef.h>
#include <stdint.h>

namespace paper_screen {

enum class TrmnlMode {
    Mirror,
    Playlist,
};

struct TimezoneOption {
    const char* id;
    const char* label;
    const char* posix_tz;
};

struct TimeSettings {
    char timezone[48] = "UTC";
    bool time_configured = false;
    int64_t last_sync_epoch = 0;
};

class SettingsService {
public:
    void begin();
    RefreshPolicy refresh_policy() const;

    bool wifi_enabled() const;
    void set_wifi_enabled(bool enabled);

    bool trmnl_enabled() const;
    void set_trmnl_enabled(bool enabled);

    TrmnlMode trmnl_mode() const;
    void set_trmnl_mode(TrmnlMode mode);

    bool trmnl_autonomous_enabled() const;
    void set_trmnl_autonomous_enabled(bool enabled);

    bool trmnl_api_key_configured() const;
    const char* trmnl_api_key() const;

    const TimeSettings& time_settings() const;
    const TimezoneOption* timezone_options(size_t* count) const;
    const TimezoneOption* timezone_option(const char* timezone_id) const;
    bool set_timezone(const char* timezone_id);
    void mark_time_synced(int64_t sync_epoch);

private:
    RefreshPolicy refresh_policy_ = RefreshPolicy::PartialWhenSafe;
    bool wifi_enabled_ = true;
    bool trmnl_enabled_ = false;
    TrmnlMode trmnl_mode_ = TrmnlMode::Playlist;
    bool trmnl_autonomous_enabled_ = false;
    bool trmnl_api_key_configured_ = false;
    char trmnl_api_key_[96] = {};
    TimeSettings time_settings_;
};

}  // namespace paper_screen
