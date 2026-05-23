# Settings App V1 Technical Spec

## Purpose

Implement the smallest useful Settings app for current development:
- expose Wi-Fi as a settings item
- show the compiled-in Wi-Fi network status
- provide a Wi-Fi connection test
- show the minimum TRMNL Desk mode configuration state

This spec intentionally does not cover general device settings, display tuning, power policy, time, storage, sync, or About pages. Those can be specified later when they are ready to build.

## Scope

V1 includes one Settings screen with two groups:

```text
Settings
  Wi-Fi
  TRMNL Desk
```

V1 does not include:
- nested Settings categories
- display settings
- power settings
- time settings
- storage settings
- sync settings
- About page
- on-device text entry
- captive portal setup
- credential persistence

## Product Behavior

Settings is a simple status-and-actions screen.

The user should be able to:
- open Settings from Home
- see whether Wi-Fi is configured
- see the configured SSID as read-only
- see current Wi-Fi connection state
- run a Wi-Fi connection test
- see whether TRMNL Desk mode has enough configuration to run
- return Home with the home button

No passwords or API keys are displayed.

## Architecture Position

Add:

```text
src/ui/settings_screen.h
src/ui/settings_screen.cpp
```

Optional display renderer later:

```text
src/display/screens/settings_screen.h
src/display/screens/settings_screen.cpp
```

Existing service dependencies:

```text
src/services/settings_service.h
src/services/settings_service.cpp
```

New service dependency from the Wi-Fi spec:

```text
src/services/wifi_service.h
src/services/wifi_service.cpp
```

Settings UI must not call Arduino `WiFi`, HTTP clients, SD, NVS, or TRMNL endpoints directly.

## Temporary Credential Policy

For now, Wi-Fi credentials are stored in code inside `WifiService`.

Settings should treat Wi-Fi as configurable state, but V1 does not edit it. The SSID is read-only and the password is never visible.

Expected display:

```text
Network    <compiled SSID>
Source     Compiled
```

When settings persistence is added later, the same Settings screen can be extended to edit or import credentials.

## View Model

```cpp
enum class SettingRowAction {
    None,
    ToggleWifiEnabled,
    TestWifiConnection,
    ToggleTrmnlEnabled,
    ToggleTrmnlMode,
};

struct SettingRowViewModel {
    const char* label = "";
    const char* value = "";
    SettingRowAction action = SettingRowAction::None;
    bool enabled = true;
};

struct SettingsViewModel {
    const char* title = "Settings";
    const SettingRowViewModel* rows = nullptr;
    int row_count = 0;
};
```

Use fixed row storage in `SettingsScreen` to avoid dynamic allocation.

## Rows

V1 rows:

```text
Wi-Fi
Enabled       On / Off
Network       <SSID> / Not configured
Source        Compiled
Status        Idle / Connecting / Connected / Failed
Signal        <RSSI> dBm / --
Test          Connect

TRMNL Desk
Enabled       On / Off
Mode          Mirror / Playlist
API Key       Set / Missing
Ready         Yes / No
```

Rows may be rendered as plain full-width list rows. Group labels can be simple text separators.

## Settings Service API

Keep `SettingsService` narrow for V1:

```cpp
class SettingsService {
public:
    void begin();

    bool wifi_enabled() const;
    void set_wifi_enabled(bool enabled);

    bool trmnl_enabled() const;
    void set_trmnl_enabled(bool enabled);

    TrmnlMode trmnl_mode() const;
    void set_trmnl_mode(TrmnlMode mode);

    bool trmnl_api_key_configured() const;
};
```

Initial values may be hard-coded defaults in RAM.

Do not add broad settings structures until there is a caller that needs them.

## Wi-Fi Integration

Settings reads Wi-Fi state from `WifiService::status()`.

Settings can request a test connection through the app shell:

```cpp
void App::handle_settings_action(SettingRowAction action)
{
    if (action == SettingRowAction::TestWifiConnection) {
        wifi_.connect();
        settings_screen_.set_wifi_status(wifi_.status());
        wifi_.disconnect();
        render_settings();
    }
}
```

The Settings screen should not own `WifiService`.

## TRMNL Integration

Settings should only show TRMNL configuration readiness.

TRMNL is ready when:

```text
TRMNL enabled == true
API key configured == true
Wi-Fi enabled == true
```

V1 does not edit the TRMNL API key. It only shows `Set` or `Missing`.

V1 may toggle:
- TRMNL enabled
- TRMNL mode: Mirror / Playlist

## Navigation

V1 navigation:

```text
Home -> Settings
Settings -> Home via GT911 home button
```

No nested pages are required.

Touch behavior:
- tapping toggle rows changes value
- tapping Test row runs Wi-Fi test
- read-only rows do nothing

Touch targets:
- full-width rows
- minimum row height 56 px

## Rendering

Initial rendering can use the generic app content path or a dedicated settings renderer.

Rendering requirements:
- one page only
- no scrolling in V1
- full content-area redraw after an action
- avoid tiny controls
- do not display password/API key

If all rows do not fit comfortably, remove lower-priority rows before adding scrolling.

Priority order:
1. Wi-Fi Enabled
2. Wi-Fi Network
3. Wi-Fi Status
4. Wi-Fi Test
5. TRMNL Enabled
6. TRMNL Mode
7. TRMNL API Key
8. TRMNL Ready
9. Wi-Fi Source
10. Wi-Fi Signal

## App Shell Integration

Current app routing opens `ScaffoldApp` for every tile. Settings should become a real screen.

Expected routing:

```cpp
void App::open_app(AppIcon icon)
{
    if (icon == AppIcon::Settings) {
        open_settings();
        return;
    }

    open_scaffold(icon);
}
```

Later, Desk can also become a real screen:

```cpp
if (icon == AppIcon::Desk) {
    open_desk();
    return;
}
```

## Persistence

No persistence is required for Settings V1.

Allowed V1 behavior:
- Wi-Fi credentials compiled in `WifiService`
- Wi-Fi enabled defaults to true when credentials exist
- TRMNL enabled defaults to false until API key exists
- TRMNL mode defaults to `MirrorCurrent`

Later:
- move Wi-Fi credentials to NVS
- move TRMNL API key to NVS
- add import from SD only if needed

## Failure Handling

Wi-Fi test fails:
- Status row becomes `Failed`
- Signal row becomes `--`
- Wi-Fi is disconnected/turned off after test attempt

Missing Wi-Fi credentials:
- Network row shows `Not configured`
- Test row is disabled

Missing TRMNL API key:
- API Key row shows `Missing`
- Ready row shows `No`

## Acceptance Criteria

Settings V1 is complete when:

- Settings opens from the Home grid as a real screen.
- The screen shows only Wi-Fi and TRMNL Desk groups.
- Wi-Fi appears as a settings item.
- Compiled-in Wi-Fi SSID is visible as read-only.
- Wi-Fi password is never visible.
- Wi-Fi status comes from `WifiService`.
- A Wi-Fi test action can connect, report status, and disconnect.
- TRMNL Desk shows enabled state, mode, API-key status, and readiness.
- Settings does not directly call Arduino `WiFi` or TRMNL HTTP APIs.
- The screen fits without scrolling on 960x540.

## Open Questions

- Should Wi-Fi enabled be toggleable in V1, or always enabled when compiled credentials exist?
- Should TRMNL API key be compiled for bring-up, or imported separately before Desk mode work?
- Should Settings use a dedicated display renderer immediately, or first reuse the generic app content renderer?
