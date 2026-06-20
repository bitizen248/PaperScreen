# PaperScreen Roadmap

Roadmap for turning the LilyGO T5 E-Paper S3 Pro firmware into a small offline-first productivity platform with a native core, a clean app SDK, and eventual developer tooling.

This project should behave like a calm e-paper appliance first and an open app platform second. The first target is the T5 E-Paper S3 Pro. Other e-paper boards are a portability goal, not a day-one requirement.

## Product Thesis

PaperScreen should become a personal productivity device that anyone can flash, use offline, and extend with small apps.

The product is strongest when it focuses on:

- low-distraction productivity
- reliable sleep and wake
- local-first tasks, reading, and focus sessions
- sparse e-paper-native refreshes
- simple app authoring through stable platform APIs
- optional Wi-Fi sync, without requiring continuous connectivity

The product is weakest if it becomes:

- a generic tablet UI
- a high-animation LVGL demo
- a Linux-like OS clone
- a framework experiment
- a multi-board abstraction project before one board is excellent

## Target Shape

Use a native firmware core with OS-like boundaries:

```text
Core firmware
  Board drivers, power, display, touch, RTC, storage, Wi-Fi, battery

Platform services
  Settings, tasks, reader state, focus timers, sync, storage helpers

SDK
  App lifecycle, events, rendering contract, storage contract, capabilities

Apps
  Home, Settings, TRMNL, Tasks, Reader, Focus, Sync, Diagnostics

Tools
  Build, flash, logs, app templates, simulator
```

The SDK should make app development simple without letting apps directly own hardware.

## Current Strategic Decisions

### Hardware

- Primary target: LilyGO T5 E-Paper S3 Pro.
- Portability target: future e-paper boards can implement the same board/display/touch/storage contracts.
- Rule: do not guess pins, display waveforms, voltages, or board revisions.

### Firmware Language

- Recommended core path: C or C++ on the current PlatformIO/ESP32 stack, with ESP-IDF-style boundaries.
- Rust is interesting, but should not own initial board bring-up or driver migration.
- Keep SDK interfaces simple enough that Rust bindings could be added later.

### UI

- LVGL is acceptable as a thin UI layer. It is MIT licensed and usable in commercial projects.
- LVGL should not own hardware logic, app state, persistence, or power behavior.
- PaperScreen needs its own e-paper refresh policy above the UI layer.

### App Model

- Native apps first.
- Apps are compiled into firmware in early versions.
- Dynamic loading from SD is out of scope for early versions.
- MicroPython or another scripting layer can be explored after the native SDK is stable.

### Storage

- Start with simple structured files and app-scoped directories.
- Use JSON for human-readable state where practical.
- Use NVS for small durable settings.
- Consider SQLite later only if real app requirements need indexed local queries.

### Sync

- Local-first behavior comes first.
- Wi-Fi sync should target a simple backend API.
- LoRa should remain optional and should not contaminate core app architecture.

## Milestones

### M0: Board Truth And Bring-Up

Goal: keep one known-good hardware baseline.

Deliverables:

- verified display, touch, RTC, SD, battery, Wi-Fi, and sleep/wake behavior
- board notes kept current in `BOARD.md`
- known-good factory or bring-up reference preserved
- diagnostics screen or logs for core hardware status

Done when:

- the device boots consistently
- display and touch work after fresh flash
- sleep/wake behavior is repeatable
- missing SD or missing Wi-Fi is handled as a normal condition

### M1: Product Shell

Goal: make the firmware feel like one device, not a set of demos.

Deliverables:

- Home screen
- Settings
- app navigation
- status bar or shared system area
- top-level event loop
- power policy hooks
- display refresh manager

Done when:

- users can navigate without debug tools
- the shell can restore a sensible state after reboot
- full refreshes are deliberate rather than accidental

### M2: Platform Storage

Goal: make storage a shared platform capability.

Deliverables:

- board storage mount/status layer
- storage service with `/paperscreen/` layout
- app-scoped directories
- atomic JSON write helper
- TRMNL last-known-good cache
- import/export directories

Done when:

- boot does not depend on SD being present
- apps can persist state through a shared service
- corrupted or missing app data has a recovery path

### M3: Minimal Native SDK

Goal: give apps one obvious shape.

Deliverables:

- `PaperApp` interface
- `AppDescriptor`
- `AppContext`
- app event type
- render result with refresh hints
- compile-time app registry
- example app

Done when:

- a simple app can be added without editing many shell files
- Home reads app tiles from the registry
- apps receive services through context instead of globals

### M4: Convert Real Apps

Goal: prove the SDK with actual product features.

Recommended order:

1. Settings as a registered system app
2. Diagnostics
3. TRMNL
4. Tasks
5. Focus
6. Reader
7. Sync

Done when:

- TRMNL works as a normal registered app
- Tasks and Focus have local persistence
- Reader supports page-based TXT/Markdown before any heavier format

### M5: Developer Tooling

Goal: make development pleasant enough for future contributors.

Deliverables:

- `paperscreen new app`
- `paperscreen build`
- `paperscreen flash`
- `paperscreen logs`
- `paperscreen package`
- app template
- app author guide

Done when:

- a contributor can create, build, and flash an example app from docs
- app capability, storage, and refresh rules are documented

### M6: Desktop Simulator

Goal: reduce flash-test cycles for UI and app logic.

Start with a UI simulator, not full hardware emulation.

Deliverables:

- 960x540 desktop canvas
- touch event simulation
- app lifecycle/event replay
- screenshot capture for layout checks
- optional mocked services for time, battery, storage, and network

Done when:

- app layout and basic interactions can be tested without hardware
- simulator code uses the same app/view contracts as firmware

### M7: Backend Sync

Goal: support offline-first productivity data with simple Wi-Fi sync.

Deliverables:

- backend API for tasks and device state
- device sync client
- conflict rules
- sync status in Settings
- import/export fallback

Done when:

- tasks remain usable offline
- sync failures do not block the device
- conflict behavior is predictable and documented

### M8: Optional Scripting

Goal: make simple user apps easier after the native platform is stable.

Evaluate MicroPython only after:

- native SDK is stable
- app lifecycle is proven
- storage and refresh contracts are documented
- memory headroom is measured

Done when:

- a small scripted app can draw UI, handle touch, and persist data
- scripted apps cannot destabilize board bring-up, power, or global services

## Technical Focus By Priority

1. Reliable board bring-up
2. Power and sleep correctness
3. E-paper refresh quality
4. Local persistence and recovery
5. Product shell and navigation
6. Native app SDK
7. First-party productivity apps
8. CLI and simulator
9. Sync backend
10. Optional scripting

## Major Risks

### E-Paper Refresh Quality

Bad refresh behavior can make the device feel broken even if the software is logically correct.

Mitigation:

- centralize refresh policy
- prefer page-based UIs
- track full refresh cadence
- test ghosting-prone screens early

### Sleep And Wake Complexity

Timers, Wi-Fi, RTC, touch, charging state, and app state all interact with sleep.

Mitigation:

- make sleep decisions explicit
- persist enough state before sleep
- keep wake reasons visible in diagnostics
- add tests for timer and desk-board wake paths

### Storage Corruption

SD cards and sudden power loss need careful write behavior.

Mitigation:

- use atomic writes for JSON
- include schema versions
- recover from missing or corrupt files
- keep critical settings in NVS

### SDK Too Early

An SDK designed before real apps exist will likely be wrong.

Mitigation:

- extract SDK from TRMNL, Tasks, Focus, and Reader needs
- keep interfaces small
- avoid dynamic loading and complex plugin machinery in v1

### Hardware Portability Overreach

Multi-board support can consume the project before the first device is good.

Mitigation:

- keep T5 as the only required target
- define board contracts
- postpone second-board support until the app shell is useful

## Open Decisions

- Should the long-term core move from PlatformIO toward direct ESP-IDF?
- Which settings stay in NVS and which move to SD?
- Should Settings always be a normal registered app?
- What is the stable app ID list for v1?
- Should Reader start with TXT only or TXT plus Markdown?
- What is the first backend sync model: custom API, WebDAV-like files, or local desktop bridge?
- How much of LVGL should the public SDK expose, if any?
- What memory and flash budget should be reserved for future scripting?

## Version Targets

### v0.1 Bring-Up Firmware

- hardware status is visible
- display/touch/RTC/SD/battery paths are stable
- Home and Settings exist
- Wi-Fi and sleep behavior are understandable

### v0.2 Appliance Shell

- app registry exists
- Settings and Diagnostics are registered apps
- storage service exists
- TRMNL cache works

### v0.3 First Productivity Build

- Tasks app
- Focus app
- basic Reader app
- local persistence
- clear import/export path

### v0.4 SDK Preview

- native app template
- app author guide
- capability declarations
- build flags for optional apps
- one example community-style app

### v0.5 Tooling Preview

- CLI can create, build, flash, and read logs
- simulator can run simple app UI
- docs cover the full app development loop

### v0.6 Sync Preview

- local-first sync client
- backend API
- conflict rules
- visible sync state

### v1.0 Personal Release

- flashable by a non-expert user
- stable daily Tasks, Focus, Reader, Settings, and optional TRMNL
- safe sleep/wake behavior
- documented SDK preview
- documented board assumptions and recovery steps

## Definition Of Done

A roadmap item is done when:

- it compiles
- it preserves core board bring-up
- it respects e-paper constraints
- it has a recovery path for missing hardware capabilities
- it fits the architecture in `ARCHITECTURE.md`
- it updates docs when behavior or public interfaces change

