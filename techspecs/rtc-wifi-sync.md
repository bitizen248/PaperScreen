# RTC Wi-Fi Sync Technical Spec

## Purpose

Add manual RTC time synchronization over Wi-Fi so PaperScreen can keep reliable local time for focus sessions, daily task views, desk-board summaries, and future sync timestamps without requiring continuous network access.

The user triggers time sync from Settings. Settings also owns timezone setup. The device should connect to Wi-Fi only for the sync attempt, write the corrected time to the PCF85063 RTC, then disconnect when the network task policy requires it.

## Scope

V1 includes:
- timezone selection in Settings
- manual `Sync time` action in Settings
- one-shot Wi-Fi connection for time sync
- SNTP/NTP time fetch
- write successful network time to the RTC
- report last sync status and timestamp
- preserve local time across reboot through the RTC

Out of scope for V1:
- background periodic time sync
- automatic timezone detection
- daylight-saving rule editor
- GPS, LoRa, or backend-provided time
- time sync during deep sleep
- changing timer semantics beyond using corrected wall time
- multi-server NTP configuration UI

## Architecture Position

Add:

```text
src/services/time_service.h
src/services/time_service.cpp
```

Expected existing dependencies:

```text
src/board/board_rtc.h
src/board/board_rtc.cpp
src/services/settings_service.h
src/services/settings_service.cpp
src/services/wifi_service.h
src/services/wifi_service.cpp
```

Layer ownership:
- `board_rtc` owns direct PCF85063 access.
- `WifiService` owns Wi-Fi connection lifecycle.
- `SettingsService` owns timezone configuration and persisted time-sync metadata.
- `TimeService` coordinates SNTP, timezone conversion, and RTC updates.
- Settings UI triggers actions and displays status, but does not call RTC, SNTP, or Arduino `WiFi` APIs directly.

## User Experience

Settings should expose a Time group:

```text
Settings
  Time
    Timezone       Europe/Amsterdam
    Current time   2026-05-24 14:30
    RTC status     Set / Not set / Error
    Last sync      2026-05-24 14:29 / Never
    Sync time      Run
```

V1 timezone setup can be a simple picker from a small curated list instead of on-device free text. The default can be `UTC` if no timezone is configured.

Suggested initial timezone list:

```text
UTC
Europe/Amsterdam
Europe/London
America/New_York
America/Chicago
America/Denver
America/Los_Angeles
```

Rules:
- Manual sync requires Wi-Fi to be configured and enabled.
- Passwords are never shown.
- A failed sync must leave the previous RTC value intact.
- The screen should show a clear status such as `Connecting`, `Syncing`, `Updated`, `Failed`, or `Wi-Fi missing`.
- The Settings page should remain page-based and fit the 960x540 e-paper screen without scrolling. If needed, make Time a nested Settings page after Settings grows beyond one page.

## Timezone Model

Store timezone as an IANA timezone identifier:

```cpp
struct TimeSettings {
    char timezone[48] = "UTC";
    bool time_configured = false;
    int64_t last_sync_epoch = 0;
};
```

Use IANA names in persisted settings because they are stable and user-understandable. Internally, firmware may need to map each supported IANA name to a POSIX timezone string for ESP-IDF/newlib time conversion.

Example mapping:

```cpp
struct TimezoneOption {
    const char* id;
    const char* label;
    const char* posix_tz;
};

constexpr TimezoneOption kTimezoneOptions[] = {
    {"UTC", "UTC", "UTC0"},
    {"Europe/Amsterdam", "Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"America/New_York", "New York", "EST5EDT,M3.2.0,M11.1.0"},
};
```

Do not let arbitrary user-entered timezone strings reach `setenv("TZ", ...)` in V1. Only allow IDs from the supported table.

## Time Representation

Internal service APIs should use Unix epoch seconds in UTC.

Rules:
- RTC writes should store an absolute time value with a documented interpretation.
- Display formatting should apply the selected timezone.
- Sync cursors and backend timestamps should remain UTC.
- Timer duration math should use monotonic time where available, not wall-clock deltas.

Recommended RTC convention:
- store UTC in the PCF85063
- apply timezone only when rendering local time or calculating local calendar boundaries

This avoids rewriting the RTC when the user changes timezone and keeps sync timestamps unambiguous.

## Public API

```cpp
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
    bool rtc_valid = false;
    bool time_configured = false;
    int64_t current_epoch = 0;
    int64_t last_sync_epoch = 0;
    char timezone[48] = "UTC";
};

class TimeService {
public:
    void begin();

    TimeStatus status() const;

    bool set_timezone(const char* timezone_id);
    const TimezoneOption* timezone_options(size_t* count) const;

    bool sync_now();
    bool sync_now(uint32_t timeout_ms);

    int64_t now_epoch() const;
    bool format_local_time(int64_t epoch, char* out, size_t out_size) const;
};
```

Keep `sync_now()` synchronous for V1. Settings actions are explicit and e-paper refreshes are sparse, so a short blocking operation is acceptable. If Wi-Fi connection plus SNTP becomes too slow for the app shell, replace this with a non-blocking state machine later.

## Manual Sync Flow

```text
User opens Settings
  -> opens Time group
  -> selects timezone if needed
  -> taps Sync time
  -> App shell calls TimeService.sync_now()
  -> TimeService reads timezone from SettingsService
  -> TimeService requests WifiService.connect()
  -> TimeService starts SNTP/NTP wait
  -> on valid network time, TimeService writes UTC time to board_rtc
  -> TimeService records last_sync_epoch and time_configured
  -> WifiService.disconnect() when settings policy requires it
  -> Settings redraws Time rows
```

Minimum expectations:
- use bounded Wi-Fi and SNTP timeouts
- do not loop forever waiting for time
- do not reset the device on failure
- preserve previous `last_sync_epoch` on failure
- disconnect Wi-Fi after the attempt when `disconnect_after_network_task` is enabled

Suggested defaults:

```cpp
constexpr uint32_t kTimeSyncTimeoutMs = 20000;
constexpr uint32_t kSntpPollIntervalMs = 250;
constexpr int64_t kMinimumValidEpoch = 1704067200; // 2024-01-01T00:00:00Z
```

Reject network time older than `kMinimumValidEpoch` so a failed SNTP setup does not write an obviously invalid timestamp to the RTC.

## SNTP Configuration

V1 can use public pool servers:

```text
pool.ntp.org
time.google.com
```

Rules:
- configure SNTP only inside `TimeService`
- keep server names out of Settings UI for V1
- stop or ignore SNTP after sync completes
- do not run SNTP continuously in the background

If the backend later exposes server time during normal sync, that can be an additional time source, but manual Wi-Fi time sync should remain useful without the backend.

## RTC Integration

`board_rtc` should expose a small API:

```cpp
struct RtcDateTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
};

class BoardRtc {
public:
    bool begin();
    bool is_valid() const;
    bool read_utc(RtcDateTime* out) const;
    bool write_utc(const RtcDateTime& value);
};
```

Rules:
- `board_rtc` should not know about Wi-Fi, SNTP, Settings, or UI.
- `TimeService` converts between epoch seconds and `RtcDateTime`.
- RTC write failures must be surfaced as `RtcWriteFailed`.
- If the RTC reports an invalid time at boot, the app should still start and Settings should show `RTC status: Not set`.

## Settings Integration

Extend `SettingsService` with time settings:

```cpp
class SettingsService {
public:
    const TimeSettings& time_settings() const;
    bool set_timezone(const char* timezone_id);
    void mark_time_synced(int64_t sync_epoch);
};
```

Timezone should be persisted in NVS with other small settings. It should not require microSD.

Settings UI actions:

```cpp
enum class SettingRowAction {
    None,
    ToggleWifiEnabled,
    TestWifiConnection,
    SelectTimezone,
    SyncRtcTime,
    ToggleTrmnlEnabled,
    ToggleTrmnlMode,
};
```

Settings screen should request timezone changes and manual sync through the app shell:

```cpp
void App::handle_settings_action(SettingRowAction action)
{
    if (action == SettingRowAction::SyncRtcTime) {
        time_.sync_now();
        settings_screen_.set_time_status(time_.status());
        render_settings();
        return;
    }
}
```

## Failure Handling

Wi-Fi disabled:
- show `Wi-Fi disabled`
- do not attempt SNTP

Missing credentials:
- show `Wi-Fi missing`
- keep `Sync time` disabled or return an immediate failure

Wi-Fi connect timeout:
- show `Connect failed`
- do not update RTC

SNTP timeout:
- show `Time server failed`
- do not update RTC

Invalid network time:
- show `Invalid time`
- do not update RTC

RTC write failure:
- show `RTC write failed`
- keep network time out of persisted sync metadata because the hardware clock was not updated

## Logging

Allowed logs:
- selected timezone ID
- sync start
- Wi-Fi connect result
- SNTP timeout or success
- RTC write success/failure
- last sync epoch

Forbidden logs:
- Wi-Fi password
- full settings dumps containing secrets

Example:

```text
[time] timezone=Europe/Amsterdam
[time] sync start
[time] network time epoch=1779632940
[time] rtc updated
[time] sync complete
```

## E-Paper Refresh Policy

Manual time sync should use a small number of screen updates:
- initial Settings page render
- action state update such as `Connecting`
- final status update

Avoid redrawing a ticking clock every second on the Settings screen. Current time can be shown as a snapshot when the screen is opened or after an action completes.

## Acceptance Criteria

RTC Wi-Fi sync is complete when:
- Settings includes a Time area or page.
- The user can select a supported timezone.
- The selected timezone persists across reboot.
- The user can manually trigger `Sync time` from Settings.
- The device connects to Wi-Fi only for the manual sync attempt.
- Successful SNTP sync writes UTC time to the PCF85063 RTC.
- Failed sync attempts do not overwrite the existing RTC time.
- Settings shows current time, RTC status, last sync, and final sync result.
- Time display uses the configured timezone.
- Wi-Fi password is never displayed or logged.
- Settings UI does not directly call Wi-Fi, SNTP, or RTC APIs.
- The implementation compiles and preserves board bring-up.

## Open Questions

- Should Settings V1 stay as one flat page, or should Time become the first nested Settings page?
- Which timezone list should ship initially for the intended users?
- Should a successful backend sync also opportunistically update the RTC from trusted server time?
- Should the RTC be corrected before or after sync outbox timestamps are generated during future automatic sync flows?
