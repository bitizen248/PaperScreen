# V1 Must Update

Temporary choices, missing product pieces, and production-replacement work needed to make PaperScreen V1 coherent.

## Release Blockers

- Remove hard-coded Wi-Fi SSID/password from `src/services/wifi_service.cpp`. Move credentials to persisted settings, local ignored config, or an on-device/import provisioning flow.
- Remove hard-coded TRMNL API key from `src/services/settings_service.cpp`. Store the key outside tracked source and expose only `Set` / `Missing` in UI.
- Replace the local TRMNL base URL in `src/services/trmnl_service.cpp` with a configurable endpoint or official production endpoint.
- Decide whether TRMNL should allow insecure HTTPS. `allow_insecure_https = true` is useful for bring-up but should not be the production default.
- Implement persistent local storage for V1 product state. `PRODUCT.md` makes this a V1 requirement, but Tasks, Focus history, sleep/refresh/theme settings, and most toggles are not fully persisted yet.
- Replace scaffold apps for Tasks, Reader, Focus, and Sync with real V1 behavior or remove non-V1 entries from Home.

## Product V1 Scope

`PRODUCT.md` defines V1 as:

- Home screen
- Tasks: inbox, today, done
- Focus timer: start, pause, resume, history-lite
- Desk-board mode with clock, next task, current timer, battery
- Settings for sleep, refresh, and theme density
- Persistent local storage

Current implementation still needs:

- `TaskService` with serializable tasks, inbox/today/done states, and minimal persistence.
- Task UI for inbox, today, complete/undo, and done review.
- `TimerService` with start/pause/resume/reset, durable active session state, and history-lite.
- Focus UI that remains legible at distance and survives sleep/wake.
- Desk-board mode that is product-owned, not only TRMNL image fetch; it should show clock, next task, timer, and battery from local services.
- Settings rows/pages for sleep policy, refresh policy, and theme density, backed by persisted settings.
- A storage decision for V1 local data: NVS only, microSD, or a hybrid.

## Battery

- `BoardBattery` reads BQ27220 state-of-charge directly and does not run fuel-gauge data-memory provisioning. Confirm whether the target battery pack requires BQ27220 configuration before trusting `StateOfCharge`.
- The BQ27220 wrapper is kept as a file-local singleton in `board_battery.cpp` to keep the noisy vendor header out of public app headers. Replace this with a cleaner driver wrapper if the board layer grows more power devices.
- Charging/full state is inferred from BQ27220 battery-status flags only. If those flags are unreliable on the shipping board, add a read-only BQ25896 charger status path.
- Battery logging is intentionally simple for bring-up. Gate or reduce per-minute battery logs before production.
- Low battery is hard-coded at 15 percent. Move this into settings or a power policy module if product behavior depends on it.
- Add battery-aware behavior only after product policy is clear: warn, block sync, dim/disable backlight, or sleep.

## Settings And Persistence

- Persist `wifi_enabled`, TRMNL enabled, TRMNL mode, refresh policy, sleep policy, theme density, and any future power thresholds. Currently time settings are persisted, but other toggles are mostly RAM defaults.
- Replace fixed curated timezone cycling with a better picker/import path if V1 ships outside the current supported timezone list.
- Decide whether Settings should remain nested pages or use a denser e-paper list model as pages grow.
- Add an About/diagnostics page with firmware version, board revision assumptions, RTC status, battery status, and storage status.
- Keep secrets out of logs and source. The UI already avoids showing passwords/API keys, but source-level secrets remain.

## Wi-Fi And Time

- Move Wi-Fi credentials from compiled constants into `SettingsService` or a dedicated credential store.
- Decide whether `WiFi.disconnect(true)` is acceptable for every network task, since it clears runtime state and may slow reconnects.
- Make network operations non-blocking or explicitly modal if connect/SNTP/TRMNL fetch latency hurts the app loop.
- Confirm SNTP server policy and timeout defaults for production.
- Keep RTC storage as UTC and verify PCF85063 behavior across sleep/reboot on hardware.
- Add a clear recovery path when RTC is invalid and Wi-Fi credentials are missing.

## TRMNL / Desk Mode

- Split product Desk-board mode from TRMNL integration. TRMNL can remain optional, but V1 desk-board requirements need local widgets.
- Replace the hard-coded local TRMNL server URL.
- Decide Mirror vs Playlist behavior for V1 and persist that choice.
- Rate-limit manual TRMNL refresh if it causes playlist churn.
- Cap image memory more deliberately than the current `1 MB` constant if heap pressure appears on device.
- Confirm PNG/BMP rendering paths and failure messages on actual TRMNL payloads.

## Power And Sleep

- Replace fixed sleep/lock constants with persisted sleep policy settings.
- Define timer behavior during lock and deep sleep before Focus ships.
- Decide desk-board trigger: manual, idle timer, schedule, dock/power, or TRMNL mode.
- Verify wake sources and button behavior against the exact board revision.
- Add low-battery policy after battery readings are validated.
- Reduce or gate power/button diagnostic logging before production.

## Board And Hardware

- Centralize board constants that are currently repeated across `board.cpp`, `board_rtc.cpp`, and service code.
- Keep pin mappings tied to `docs/pinmap.md` and board revision notes; do not spread new pin constants through feature services.
- Decide whether the current Arduino/PlatformIO layout remains the V1 firmware base or whether to move closer to ESP-IDF-style components.
- Verify BQ27220, BQ25896, PCF85063, GT911, SD, and display initialization on the exact shipping board.

## Storage

- Choose and implement V1 local storage. Options:
  - NVS for settings plus task/timer state.
  - microSD for task/reader files plus NVS for settings.
  - hybrid with NVS indexes and SD documents.
- Add corruption handling and default recovery behavior.
- Decide whether microSD is required for V1 or deferred with Reader/import work.
- Add manual remount/status only if microSD is part of V1.

## UI And Display

- Replace placeholder scaffold content in `src/ui/scaffold_app.cpp`.
- Decide which Home icons are V1. Reader and Sync are V2 in `PRODUCT.md`, but currently appear on Home.
- Ensure status bar updates are batched and do not create full-screen refresh churn.
- Review TRMNL rotation/status-bar expectations; TRMNL content rendering currently differs from normal app screens.
- Add real empty/error states for Tasks, Focus, Desk-board, Storage, and Sync instead of generic placeholders.
- Tune touch targets and page density after real V1 screens exist.

## Sync

- Backend sync is not V1 per `PRODUCT.md`, but Sync appears on Home. Either hide it for V1 or make it an explicit placeholder/diagnostic entry.
- If any network sync ships in V1, define first connector/source, auth model, conflict policy, and offline outbox persistence.
- Do not let LoRa enter core V1 architecture.

## Build, Config, And Repository Hygiene

- Add an ignored local config path or provisioning mechanism for private Wi-Fi/TRMNL values.
- Remove checked-in local IDE/system artifacts from the working tree if they are not intentionally tracked.
- Ensure PlatformIO can build without special home-directory permission fixes on the developer machine.
- Add a repeatable firmware version/build metadata path.
- Add minimal tests for pure services where possible: battery formatting, settings persistence, time formatting, task/timer state transitions.

## Documentation To Keep Current

- Keep `techspecs/battery-percentage-service.md` aligned with the implementation.
- Add or update specs before implementing Tasks, Focus timer, local storage, and product Desk-board mode.
- Update `ARCHITECTURE.md` if the repo stays Arduino/PlatformIO rather than moving to ESP-IDF-style components.
- Document any board-revision-specific behavior found during hardware testing.
