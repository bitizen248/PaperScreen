# Wi-Fi Service Technical Spec

## Purpose

Provide a standalone Wi-Fi service for PaperScreen so network access can be shared by TRMNL Desk mode, future sync, firmware diagnostics, and later import/export features without coupling those features to Wi-Fi hardware details.

For the first implementation, Wi-Fi credentials may be stored in code. This is temporary bring-up behavior. The service API should still look like credentials come from settings so the later move to persisted configuration is straightforward.

## Scope

V1 scope:
- store one configured Wi-Fi network in code
- connect on demand
- disconnect on request
- report connection status and RSSI
- expose simple error states
- avoid keeping Wi-Fi on continuously

Out of scope for V1:
- captive portal provisioning
- multi-network selection
- enterprise Wi-Fi
- background scans
- Wi-Fi access point mode
- OTA firmware updates
- full credential editing UI

## Architecture Position

Add:

```text
src/services/wifi_service.h
src/services/wifi_service.cpp
```

Optional later:

```text
src/services/network_time_service.h
src/services/network_time_service.cpp
```

Layer ownership:
- `WifiService` belongs in `services/`.
- `board/` should not manage Wi-Fi connection policy.
- `ui/` should not call Arduino `WiFi` APIs directly.
- TRMNL and Sync services should depend on `WifiService`, not on `WiFi.h`.

## Dependencies

Expected Arduino dependencies:

```cpp
#include <WiFi.h>
```

HTTP clients should live in feature services such as `TrmnlService`, not inside `WifiService`.

## Temporary Credential Storage

For bring-up, credentials can live in one private code file:

```cpp
namespace {

constexpr char kWifiSsid[] = "YOUR_SSID";
constexpr char kWifiPassword[] = "YOUR_PASSWORD";

}
```

Rules:
- do not print the password
- do not expose credentials through public view models
- keep temporary credentials in `wifi_service.cpp`, not headers
- later replace this with `SettingsService::wifi_settings()`

Do not spread credentials across TRMNL, Sync, or app code.

## Future Settings Model

The long-term model should be compatible with `SettingsService`:

```cpp
struct WifiSettings {
    bool enabled = false;
    char ssid[64] = {};
    char password[96] = {};
    bool auto_connect_for_desk = true;
    bool disconnect_after_network_task = true;
    uint32_t connect_timeout_ms = 15000;
};
```

During V1 hard-coded credential bring-up, `WifiService` can internally construct the same shape:

```cpp
WifiSettings WifiService::settings() const;
```

That keeps call sites stable when settings persistence is added.

## Public API

```cpp
enum class WifiConnectionState {
    Disabled,
    Idle,
    Connecting,
    Connected,
    Failed,
};

enum class WifiError {
    None,
    Disabled,
    MissingCredentials,
    Timeout,
    Disconnected,
    Unknown,
};

struct WifiStatus {
    WifiConnectionState state = WifiConnectionState::Idle;
    WifiError last_error = WifiError::None;
    bool connected = false;
    int32_t rssi = 0;
    char ssid[64] = {};
};

class WifiService {
public:
    void begin();

    WifiStatus status() const;

    bool connect();
    bool connect(uint32_t timeout_ms);
    void disconnect();

    bool is_connected() const;
    int32_t rssi() const;

private:
    WifiStatus status_;
};
```

Keep the API synchronous for V1. The app loop is slow and e-paper oriented, so a short blocking connection attempt is acceptable during Desk mode fetch. If this becomes painful, add a non-blocking state machine later.

## State Machine

```text
Disabled
  -> Idle                 when Wi-Fi is enabled and credentials exist

Idle
  -> Connecting           connect requested

Connecting
  -> Connected            association succeeds
  -> Failed               timeout or Wi-Fi failure

Connected
  -> Idle                 disconnect requested
  -> Failed               unexpected disconnect

Failed
  -> Connecting           retry requested
  -> Idle                 reset/clear requested
```

## Connect Policy

Desk mode should connect only when it needs fresh network data:

```text
Desk refresh starts
  -> WifiService.connect()
  -> TrmnlService fetches metadata/image
  -> WifiService.disconnect() if configured
```

Minimum expectations:
- use a connect timeout
- return failure to caller instead of looping forever
- do not reset the device on Wi-Fi failure
- allow cached/offline UX to continue

Suggested defaults:

```cpp
constexpr uint32_t kDefaultConnectTimeoutMs = 15000;
constexpr uint32_t kShortRetryDelayMs = 1000;
```

## Logging

Allowed logs:
- Wi-Fi enabled/disabled
- connecting to SSID
- connected
- RSSI
- timeout/failure reason
- disconnected

Forbidden logs:
- password
- full settings dump containing secrets

Example:

```text
[wifi] begin
[wifi] connecting ssid=HomeNetwork
[wifi] connected rssi=-61
[wifi] disconnect
```

## Integration With TRMNL

`TrmnlService` should receive or reference `WifiService`.

Example app wiring:

```cpp
WifiService wifi_;
TrmnlService trmnl_;
DeskScreen desk_;
```

Example fetch:

```cpp
if (!wifi_.connect()) {
    desk_.show_offline();
    return;
}

trmnl_.refresh();

if (settings_.wifi_settings().disconnect_after_network_task) {
    wifi_.disconnect();
}
```

TRMNL should use `wifi_.rssi()` for optional request headers, but Wi-Fi should not know about TRMNL.

## Integration With Backend Sync

Backend sync is specified in `techspecs/backend-sync.md`.

`SyncService` should use `WifiService` for connection lifecycle:
- connect on manual sync, scheduled sync, or wake-driven sync
- push local outbox changes
- pull backend changes
- disconnect when the configured network task policy requires it

`WifiService` must not know about backend endpoints, device tokens, sync cursors, or external app connectors.

## Integration With Settings App

Settings should expose Wi-Fi as a top-level item:

```text
Settings
  Wi-Fi
    Enabled
    SSID
    Status
    Connect test
```

For V1, SSID may be read-only if credentials are compiled in.

The Settings app should not edit the hard-coded password directly. It can show:
- Wi-Fi enabled/disabled
- configured SSID
- connection status
- test connection action

Later, when text input or captive portal exists, the Settings app can update `WifiSettings` through `SettingsService`.

## Failure Handling

Missing credentials:
- return `WifiError::MissingCredentials`
- Settings shows "Wi-Fi not configured"

Timeout:
- return `WifiError::Timeout`
- caller may use cached content

Unexpected disconnect:
- return `WifiError::Disconnected`
- caller decides whether to retry

Repeated failures:
- do not retry in a tight loop
- caller should back off using feature-specific timing

## Acceptance Criteria

The Wi-Fi service is complete for V1 when:

- `WifiService` builds as a standalone service.
- Credentials exist in one implementation file only.
- `connect()` succeeds or times out cleanly.
- `disconnect()` turns Wi-Fi off or disconnects from the AP.
- `status()` reports state, error, SSID, and RSSI.
- TRMNL can use the service without calling `WiFi.h` directly.
- Settings can display Wi-Fi status without knowing Wi-Fi internals.
- Password is never logged.

## Open Questions

- Should V1 use `WiFi.disconnect(true)` to fully erase runtime state, or a lighter disconnect to speed later reconnects?
- Should Wi-Fi power be explicitly disabled after disconnect for battery savings?
- Should SNTP time sync be part of Wi-Fi connection or a separate service?
- Should hard-coded credentials be excluded from git by moving them into a local ignored header?
