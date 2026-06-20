# Native SDK v0

This note documents the first SDK shape for PaperScreen apps. The goal is to give TRMNL a clean app boundary first, while keeping the shape general enough for Tasks, Reader, Focus, Sync, and Diagnostics.

## Intent

The SDK is not a security sandbox and not a dynamic app runtime yet. It is a compile-time app contract that keeps app code away from board drivers and low-level services.

Apps should depend on:

- app descriptors
- app lifecycle
- app events
- app context APIs
- render results and refresh hints

Apps should not directly depend on:

- display panel drivers
- touch controller drivers
- RTC drivers
- SD drivers
- Wi-Fi driver setup
- deep sleep implementation details

## Layer Boundary

```text
apps/
  TRMNL, Tasks, Reader, Focus
    |
sdk/
  PaperApp, AppContext, events, results, descriptors
    |
platform services/
  display, storage, Wi-Fi, time, battery, power
    |
board + drivers/
  ED047TC1, GT911, PCF85063, SD, battery, ESP32 sleep
```

Driver code exposes hardware behavior. Platform services turn that behavior into product capabilities. The SDK exposes only app-safe operations.

## Descriptor

Each app has an `AppDescriptor` with:

- stable app ID
- human name
- icon
- capability flags
- Home visibility

The descriptor exists so Home, Settings, storage paths, logs, tooling, and the future CLI do not need hardcoded knowledge of every app.

## Capabilities

Capabilities are v0 policy hints, not strong permissions.

Current flags:

- `Network`
- `Storage`
- `BackgroundSchedule`
- `WakeTimer`
- `BatteryState`
- `Time`
- `Fullscreen`
- `Backlight`

TRMNL uses most of these because it fetches remote images, caches state, schedules refreshes, checks battery/charging, renders full screen, and controls light behavior.

Tasks may only need `Storage` and `Time`. Reader may need `Storage` and `Fullscreen`. Focus may need `Time` and `WakeTimer`.

## App Lifecycle

The initial interface is:

```cpp
class PaperApp {
public:
    virtual const AppDescriptor& descriptor() const = 0;

    virtual void on_open(AppContext& context) {}
    virtual void on_close(AppContext& context) {}
    virtual void on_suspend(AppContext& context) {}
    virtual void on_resume(AppContext& context) {}

    virtual AppEventResult on_event(AppContext& context, const AppEvent& event) = 0;
    virtual AppRenderResult render(AppContext& context, DisplayRenderContext& render_context) = 0;
};
```

This is intentionally small. It should grow from real app pressure, not from imagined platform needs.

## App Context

`AppContext` exposes abstract app-facing APIs:

- `storage`
- `network`
- `clock`
- `battery`
- `power`
- `log`

`AppPlatformContext` provides the firmware-side adapters that bridge those APIs to current services. A future desktop simulator can provide different adapters without changing app code.

For example, TRMNL should call `context.network.connect()` instead of owning Wi-Fi setup.

Power requests are expressed through the SDK and should be honored by the shell. Apps should not directly force deep sleep once the migration is complete.

## TRMNL Migration Target

TRMNL should eventually be structured as:

```text
src/apps/trmnl/
  trmnl_app.*
  trmnl_client.*
  trmnl_cache.*
  trmnl_view.*
  trmnl_model.*
```

Responsibilities:

- `trmnl_app`: lifecycle, events, menu actions, scheduling requests
- `trmnl_client`: backend fetch through SDK/network APIs
- `trmnl_cache`: last-known-good image and metadata through SDK/storage APIs
- `trmnl_view`: render current state
- `trmnl_model`: app-owned state

The current shell-owned TRMNL code can migrate gradually. The first safe step is to move metadata and capabilities into the registry, then move lifecycle and events, then move fetch/cache/render decisions.

Phase 1 has started:

- `src/apps/trmnl/trmnl_app.*` defines `TrmnlApp : PaperApp`.
- TRMNL app-owned session state lives in `TrmnlAppState`.
- The shell calls TRMNL lifecycle hooks when opening and closing TRMNL.
- Existing fetch, menu rendering, image rendering, and power behavior remain in the shell/services for now.

Phase 2 has started:

- TRMNL home button and menu touch decisions now flow through `TrmnlApp::on_event`.
- The app returns TRMNL-specific commands through the generic SDK action command slot.
- Scheduled refresh setup, clearing, and due checks now flow through TRMNL app events.
- TRMNL fetch settings are assembled by `trmnl_settings_adapter` instead of the shell.
- TRMNL view-model construction moved to `apps/trmnl/trmnl_view.*`.
- TRMNL saves the last successful image and metadata through `trmnl_cache`.
- TRMNL can load the last-good cached image and render it when a live fetch fails.
- The shell still executes display rendering, refresh fetches, backlight toggles, and navigation.

Next migration steps:

1. Move refresh orchestration out of the shell into a TRMNL controller/helper.
2. Replace shell TRMNL branches with a normal active `PaperApp` dispatch path.

## Design Rules

- Keep SDK headers small.
- Do not expose board drivers to apps.
- Do not expose raw display ownership to apps.
- Prefer app requests over app commands for sleep and background scheduling.
- Keep TRMNL-specific behavior in TRMNL modules, but express platform needs generically.
