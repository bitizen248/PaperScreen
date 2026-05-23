# ARCHITECTURE.md

Architecture guidance for the portable productivity tool.

## Goal

Create a maintainable firmware architecture for a portable e-paper productivity device with tasks, reading, timers, and an optional desk-board mode.

## System shape

The system should be one app shell with multiple modes:
- Home
- Tasks
- Reader
- Focus timer
- Desk board
- Settings
- Sync

The app should behave like an appliance: fast wake, stable screens, sparse redraws, and strong persistence.

## Layer model

### 1. Board layer

Responsibilities:
- power state
- battery status
- RTC
- touch input
- SD card
- optional radio setup
- sleep / wake orchestration

Examples:
- `board_power.*`
- `board_rtc.*`
- `board_touch.*`
- `board_storage.*`
- `board_radio.*`

### 2. Display layer

Responsibilities:
- e-paper init wrapper
- dirty region refresh
- full refresh scheduling
- drawing primitives and theme constants
- font registration

The display layer should expose a device-appropriate abstraction, not raw app-specific screen logic.

### 3. Service layer

Responsibilities:
- domain state
- persistence
- scheduling
- business rules
- sync adapters

Core services:
- `task_service`
- `reader_service`
- `timer_service`
- `settings_service`
- `sync_service`
- `deskboard_service`

### 4. UI layer

Responsibilities:
- screens
- widgets
- navigation
- mapping service state to view state
- translating touch gestures into events

UI rules:
- UI must not own business truth.
- UI must be rebuildable from service state.
- UI should favor page changes and selective refresh over animated transitions.

### 5. App shell

Responsibilities:
- startup ordering
- dependency wiring
- mode switching
- global event loop
- top-level error handling

## Event model

Preferred flow:
1. input event arrives
2. app shell routes event
3. service updates state
4. UI derives view model
5. display layer refreshes only changed regions where possible

Avoid direct chains where touch handlers call draw code and mutate domain state at the same time.

## Persistence

Persist only structured, portable state.

Suggested persisted entities:
- tasks
- task completion history
- current timer session
- settings
- reader bookmarks and last position
- desk-board preferences

Avoid persisting UI internals or LVGL widget state.

## Refresh policy

Because the device is e-paper:
- prefer partial refresh for checkboxes, timers, and small counters when hardware behavior is acceptable
- trigger periodic full refresh to manage ghosting
- avoid rapid visual churn
- batch redraws when possible

## Sync policy

Recommended order of implementation:
1. local-only
2. SD import/export
3. Wi-Fi sync
4. optional LoRa summaries / relays

Do not make LoRa a requirement for core productivity features.

## Reader scope

Recommended scope for v1:
- plain text
- Markdown
- simple chapterized content

EPUB can be added after the pagination and bookmark model is stable.

## Testing priorities

Highest-value test areas:
- wake from sleep restores correct mode
- timer survives sleep correctly
- task completion updates only the necessary screen areas
- reader resumes exact position
- desk-board mode is readable from distance
- storage corruption is handled safely

## Design constraints

- No flashy animation-driven architecture
- No screen logic coupled to board drivers
- No heavy generic plugin system in v1
- No assumption of continuous network connectivity

## Suggested repo layout

```text
src/
  main/
  board/
  display/
  services/
  ui/
  util/
assets/
docs/
```
