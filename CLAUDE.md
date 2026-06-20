# CLAUDE.md

Working notes for Claude (and other agents) on the **PaperScreen** firmware.
Read this first, then `AGENTS.md` + `ARCHITECTURE.md` + `BOARD.md` for the rules of the road.

## What this project actually is

Firmware for the **LilyGO T5 E-Paper S3 Pro** (4.7" 960×540 ED047TC1 e-paper, ESP32-S3,
GT911 touch, PCF85063 RTC, microSD, battery, optional SX1262 LoRa). It is an in-progress
rewrite of LilyGO's factory firmware into a layered "calm e-paper appliance" with a small
native **app SDK**.

PlatformIO + Arduino framework (not bare ESP-IDF, despite the "ESP-IDF-style boundaries"
language in the docs). Env: `T5_E_PAPER_S3_V7`, board json in `boards/T5-ePaper-S3.json`.

## Docs vs. reality (important)

The markdown docs are **aspirational product/architecture intent**, not a description of
what is built. Keep the gap in mind:

- **PRODUCT.md / ARCHITECTURE.md / ROADMAP.md** promise Tasks, Reader, Focus timer,
  Desk-board, Sync. **None of these are implemented.**
- The **only fully built app is TRMNL** (`src/apps/trmnl/`) — a cloud e-paper dashboard
  integration. **Settings** is also functional.
- `src/sdk/app_registry.cpp` advertises 7 home apps (tasks, reader, focus, trmnl, settings,
  sync, terrain). For everything except `trmnl` and `settings`, `App::open_app()` just
  constructs a generic placeholder `ScaffoldApp` — tapping those icons opens an empty shell,
  not a real app.
- The repo is **mid-refactor**: screens are migrating from `src/ui/` into
  `src/display/screens/` + `src/apps/`. Both locations coexist right now
  (e.g. `ui/trmnl_screen.cpp` is deleted, `display/screens/lock_screen.cpp` added).
- **Most of the interesting code is uncommitted.** `git log` is just the LilyGO factory
  history + an "Initial"/"TRMNL" pair; the entire `src/sdk/`, `src/apps/`, several services
  (`battery`, `storage`, `time`), `tests/`, `ROADMAP.md`, and most `techspecs/` are untracked
  or modified working-tree changes. Don't assume `HEAD` reflects the current design.

## Architecture (layers)

Enforced boundaries (from AGENTS.md/ARCHITECTURE.md — these are real and worth respecting):

```
main/      app bootstrap + top-level state machine (App in src/main/app.{h,cpp}, ~1300 lines)
sdk/       app contract: PaperApp lifecycle, AppContext (storage/network/clock/battery/
           power/log APIs), AppDescriptor, capabilities, events, results, registry
apps/      concrete PaperApp implementations (only trmnl/ today)
services/  domain + platform: settings, storage, time, wifi, battery, power, trmnl
display/   e-paper driver wrapper, refresh policy, drawing, fonts, image_renderer, screens/
board/     hardware: board, board_battery, board_rtc, board_storage
ui/        legacy screen/view-model code being migrated out
```

Hard rules: UI never touches hardware except via service/display abstractions; services
never depend on LVGL types; board code never knows about screens; domain state is
serializable. Optimize for **few e-paper refreshes**, predictable state, clean sleep/wake.

The SDK boundary (`src/sdk/app.h`, `app_context.h`) is the cleanest part of the codebase and
the intended extension point — new apps should implement `PaperApp` and get capabilities/IO
only through `AppContext`, never by reaching into board/display directly.

## Build / test

```sh
# Host-side SDK tests — no hardware, no PlatformIO needed. Run these first; they're fast.
./tests/sdk/run_sdk_tests.sh      # compiles with c++20 -Wall -Wextra -Werror, prints "SDK tests passed"

# Firmware build / flash (needs PlatformIO + the physical board)
pio run -e T5_E_PAPER_S3_V7
pio run -e T5_E_PAPER_S3_V7 -t upload
pio device monitor -b 115200
```

There is a CI workflow at `.github/workflows/sdk-tests.yml` (untracked) for the host tests.

## Gotchas

- **Secret in repo:** `platformio.ini` hard-codes a TRMNL API key in
  `-DPAPER_SCREEN_TRMNL_API_KEY=...`. Treat it as a leaked credential — don't echo it, and
  flag if asked to share configs. Long-term it should move out of source control.
- **Second board variant:** branch `H752-01` and `firmware/H752_01_*.bin` target a different
  LilyGO board. Don't mix its pin maps/assumptions into the S3 Pro path.
- **Don't invent hardware details** — pins, waveforms, voltages, board revisions. Check
  `board/` source, `BOARD.md`, or `hardware/*.pdf` datasheets first.
- `src/.DS_Store` and `.idea/` are committed/untracked noise; ignore.
- Fonts (`src/display/fonts/firasans_*.h`) are huge generated headers — don't read them whole.

## Where to start for common tasks

- New app  → implement `PaperApp` in `src/apps/<name>/`, register in `app_registry.cpp`,
  wire an `open_*` path in `src/main/app.cpp`, add a host test in `tests/sdk/`.
- Touch / nav / sleep behavior → `src/main/app.cpp` (the central state machine).
- Persistence → `services/storage_service.*` + the `AppStorageApi` in `sdk/app_context.h`.
- Display/refresh → `src/display/display.cpp` + `display/refresh_policy.h`.
- Specs for intended-but-unbuilt features live in `techspecs/`.
