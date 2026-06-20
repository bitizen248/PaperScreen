# PaperScreen OS Platform Technical Spec

## Purpose

Describe the path from the current PaperScreen firmware into a small e-paper OS with a stable core, a simple app SDK, and optional apps such as TRMNL, Tasks, Reader, Focus, Sync, and future community apps.

This should stay an embedded appliance, not a dynamic tablet OS. Apps are compile-time firmware modules at first. The SDK gives app authors clean boundaries, shared services, and predictable rendering rules without letting app code own board bring-up or power behavior.

## Product Direction

PaperScreen should become:

- a core firmware platform for the LilyGO T5 E-Paper S3 Pro
- an app shell with installed app tiles
- a small SDK for first-party and community apps
- an offline-first device where Wi-Fi, TRMNL, backend sync, and SD storage are optional capabilities
- an e-paper-native environment optimized for low refresh count, fast wake, sleep safety, and simple state recovery

TRMNL is a strong first real app because it exercises network, scheduling, image decode, battery-aware desk mode, and last-known-good rendering.

## Current State

The repository already has a good early foundation:

- `src/board/` owns board bring-up pieces such as RTC and battery-facing code.
- `src/display/` owns e-paper display wrapping, drawing helpers, widgets, and screen renderers.
- `src/services/` owns settings, Wi-Fi, time, battery, power, and TRMNL integration.
- `src/ui/` owns home, settings, TRMNL, lock, and placeholder app screens.
- `src/main/` owns startup, dependency wiring, event loop, navigation, power handling, and top-level state.
- `techspecs/` already captures backend sync, microSD, Wi-Fi, time, settings, battery, and TRMNL desk mode direction.

The current app model is still mostly a shell:

- Home tiles are fixed in `HomeScreen`.
- Most apps use `ScaffoldApp` placeholder text.
- TRMNL is implemented as a special path inside `App`.
- Settings is implemented as a special path inside `App`.
- Input routing, sleep decisions, dropdown handling, TRMNL gestures, refresh scheduling, and status bar updates live in the app shell.

That is fine for bring-up, but it will become hard to maintain once Tasks, Reader, Focus, Sync, and TRMNL all have real state.

## What Is Blocking Full OS Work

### 1. No Stable App Boundary

There is no common app interface yet. Apps do not have lifecycle methods, event handlers, render contracts, or capability declarations.

Needed:

- app descriptors for name, icon, capabilities, and default tile visibility
- app instances for runtime state
- lifecycle hooks such as open, close, suspend, resume, and tick
- input event handling owned by the active app where appropriate
- render output that can request full or partial refresh

### 2. App Shell Owns Too Much App Logic

`App` currently knows details of TRMNL menu behavior, TRMNL scheduling, settings pages, scaffold app creation, dropdown rendering, sleep behavior, and status bar updates.

Needed:

- keep `App` as the platform shell
- move app-specific logic behind app interfaces
- keep global responsibilities in the shell: startup, dependency wiring, status bar, power policy, navigation, and fatal errors

### 3. Storage Layer Is Designed But Not Implemented

The microSD spec exists, but there is not yet a shared `board_storage` and `storage_service` implementation.

Needed:

- mount SD without making boot depend on it
- app-scoped directories under `/paperscreen/`
- atomic JSON writes
- TRMNL cache path for last-good PNG and metadata
- reader import paths
- sync inbox/outbox paths

SD should store content, cache, logs, and import/export data. It should not be used for executable app loading in v1.

### 4. Persistence Is In Transition

Settings were temporarily simplified to keep TRMNL stable during crash debugging. That was useful for bring-up, but the OS direction needs a deliberate persistence contract.

Needed:

- small durable settings in NVS
- larger user-visible state on SD
- app-owned persistence with a shared schema and version field
- recovery behavior when persisted app state is missing or corrupt

### 5. No SDK Context Object

Apps need access to platform capabilities without directly including board drivers or global services.

Needed:

```cpp
struct AppContext {
    DisplayService& display;
    StorageService& storage;
    SettingsService& settings;
    WifiService& wifi;
    TimeService& time;
    BatteryService& battery;
    PowerService& power;
};
```

The exact names can change, but the concept matters: apps receive capabilities through a narrow context instead of creating or poking hardware services themselves.

### 6. Display Refresh Contract Is Too Implicit

The display layer can render screens and widgets, but apps need an e-paper-specific way to say what changed.

Needed:

- render result with refresh hint: none, partial, full, full after N partials
- dirty region support where the display path can safely honor it
- app-level guidance for page-based layouts and low refresh count
- shared status bar behavior that apps can opt into

### 7. No Capability or Permission Model

Open-source apps should declare what they need, even if this is not a security sandbox.

Needed capabilities:

- network
- storage
- background scheduling
- wake timer
- battery/charging state
- time/RTC
- full-screen drawing
- backlight

The shell can use capabilities to show settings, prevent surprises, and decide which apps are safe for autonomous desk mode.

### 8. No Build-Time App Selection

The intended open-source model needs users to build only what they need.

Needed:

- app registry compiled from enabled modules
- build flags for optional apps
- clear example app
- docs for adding an app without editing many central files

Dynamic loading from SD should be out of scope for early versions.

## Recommended Architecture

Use a three-part platform model:

```text
Core
  Board drivers, display, power, storage, Wi-Fi, time, battery, shared settings

SDK
  App interface, app context, app registry, event types, view contracts, storage helpers

Apps
  TRMNL, Tasks, Reader, Focus, Sync, Settings, Diagnostics, community apps
```

Suggested future layout:

```text
src/
  core/
    app_shell/
    events/
    navigation/
  board/
  display/
  services/
  sdk/
    app.h
    app_context.h
    app_registry.h
    app_events.h
    app_storage.h
  apps/
    trmnl/
    tasks/
    reader/
    focus/
    sync/
    settings/
    diagnostics/
  main/
```

This can be migrated gradually. Do not rewrite the repo just to match the layout.

## SDK Shape

Start with a small interface:

```cpp
enum class AppEventType {
    Open,
    Close,
    Suspend,
    Resume,
    TouchDown,
    TouchMove,
    TouchUp,
    Button,
    Tick,
    TimerWake,
};

enum class AppRefreshHint {
    None,
    Partial,
    Full,
};

struct AppDescriptor {
    const char* id;
    const char* name;
    AppIcon icon;
    uint32_t capabilities;
    bool show_on_home;
};

struct AppRenderResult {
    AppRefreshHint refresh_hint;
    bool status_bar_changed;
};

class PaperApp {
public:
    virtual ~PaperApp() = default;
    virtual const AppDescriptor& descriptor() const = 0;
    virtual void open(AppContext& context) = 0;
    virtual void close(AppContext& context) = 0;
    virtual bool handle_event(AppContext& context, const AppEvent& event) = 0;
    virtual AppRenderResult render(AppContext& context) = 0;
};
```

Keep this boring. The SDK should be easy to understand from one example app.

## First App Migration: TRMNL

TRMNL should become the first real app module.

Move toward:

```text
src/apps/trmnl/
  trmnl_app.h
  trmnl_app.cpp
  trmnl_view.h
  trmnl_view.cpp
  trmnl_service_adapter.h
```

TRMNL app owns:

- opening the app
- manual refresh
- menu behavior
- refresh schedule requests
- autonomous desk mode behavior
- last-known-good render decisions
- TRMNL-specific touch gestures

Core services still own:

- Wi-Fi connection primitive
- battery status
- power sleep and timer wake primitive
- image decode/render primitive
- storage primitive

The shell decides whether an app may schedule wake/sleep based on capability flags and global settings.

## App Registry

Use a compile-time registry:

```cpp
const RegisteredApp kRegisteredApps[] = {
    register_settings_app(),
    register_trmnl_app(),
#if PAPER_SCREEN_ENABLE_TASKS
    register_tasks_app(),
#endif
#if PAPER_SCREEN_ENABLE_READER
    register_reader_app(),
#endif
};
```

Rules:

- Home screen reads tiles from the registry.
- Opening an app uses the registry, not a `switch` over every app.
- Apps can be disabled by build flag.
- Settings can hide apps without removing them from firmware.

## Storage Contract

Implement storage before deep app work.

Recommended path:

1. Add `src/board/board_storage.*` for mount/unmount/status.
2. Add `src/services/storage_service.*` for `/paperscreen/` layout and atomic file helpers.
3. Give every app an app directory:

```text
/paperscreen/apps/trmnl/
/paperscreen/apps/tasks/
/paperscreen/apps/reader/
/paperscreen/apps/focus/
```

4. Add shared directories:

```text
/paperscreen/cache/
/paperscreen/import/
/paperscreen/export/
/paperscreen/logs/
```

5. Cache TRMNL last-good image first.

This creates immediate user value: TRMNL can show a previous screen after reboot, Wi-Fi failure, or server failure.

## Recommended Implementation Path

### Phase 0: Stabilize Current Firmware

Goal: keep the device useful while architecture moves.

- Keep TRMNL working with the official backend.
- Keep autonomous desk mode working on charger.
- Keep crash fixes small and reversible.
- Restore durable settings after the simple in-memory debug version is stable.
- Add a last-known-good TRMNL cache once SD is mounted.

Done when TRMNL can boot, refresh, sleep, wake, and fail gracefully.

### Phase 1: Build Platform Storage

Goal: make SD a shared platform capability.

- Implement board storage.
- Implement storage service.
- Add Settings storage status.
- Add diagnostics log files.
- Add TRMNL cache.

Done when removing the SD card does not break boot and inserting it enables cache/import/export.

### Phase 2: Add Minimal SDK and Registry

Goal: app modules have one obvious shape.

- Add `PaperApp`, `AppDescriptor`, `AppContext`, `AppEvent`, and registry.
- Keep existing UI renderers where possible.
- Move Home tiles to the registry.
- Keep Settings special only if needed during transition.

Done when a simple example app can be added without editing the shell in many places.

### Phase 3: Convert TRMNL Into an App

Goal: prove the SDK with the most demanding current app.

- Move TRMNL routing and menu behavior out of `App`.
- Keep `TrmnlService` as a service dependency.
- Move TRMNL schedule requests behind app capability calls.
- Persist last image metadata and refresh timing.

Done when TRMNL works the same as before but is registered like any other app.

### Phase 4: Implement Core First-Party Apps

Goal: build the product around real local apps.

Recommended order:

1. Settings and Diagnostics
2. Tasks
3. Focus
4. Reader
5. Sync

Tasks and Focus are good first local apps because their data models are small. Reader should wait until storage and pagination are stable.

### Phase 5: Open-Source App Authoring

Goal: make community contribution pleasant.

- Add `apps/example_counter/` or `apps/example_status/`.
- Add an app author guide.
- Add build flags documentation.
- Add app capability documentation.
- Add rules for persistence, refresh, and power behavior.

Done when a contributor can copy one example folder and register an app.

## What Not To Build Yet

Avoid these until the core is stable:

- dynamic executable loading from SD
- scripting runtime for apps
- app store UX
- arbitrary background tasks
- complex permissions with false security claims
- heavy LVGL ownership of app state
- generic plugin architecture that hides what the firmware is doing

The first public version can still feel like an OS if it has a clean home screen, app registry, app lifecycle, shared services, and good documentation.

## Near-Term Decisions

Before implementation gets large, decide:

- Should all apps use the same status bar, or can full-screen apps hide it?
- Which settings return to NVS after the debug simplification?
- What exact app IDs are stable for storage paths?
- Should Settings itself be a normal registered app?
- Should TRMNL cache use PNG only, or store decoded framebuffer data later?
- Which apps are enabled by default in the open-source build?

Recommended defaults:

- Keep Settings as a registered system app.
- Keep TRMNL, Settings, Diagnostics enabled by default.
- Keep Tasks, Focus, Reader, and Sync buildable but allowed to mature behind flags.
- Store TRMNL cache as PNG plus metadata first.
- Use stable lowercase app IDs such as `trmnl`, `tasks`, `reader`, `focus`, `sync`, `settings`.

## First OS Milestone

The first milestone should be small and real:

- boot to Home
- Home app list comes from the registry
- Settings is registered
- TRMNL is registered
- TRMNL still supports official backend refresh
- TRMNL can cache last-good image to SD
- missing SD and missing Wi-Fi are normal states
- app authors can read one example and understand how to add another app

That is enough to call the firmware a platform without overbuilding it.
