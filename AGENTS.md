# AGENTS.md

Repository guidance for Codex, Junie, and other coding agents working on the LilyGO T5 E-Paper S3 Pro productivity tool.

## Project intent

Build a portable productivity device for the LilyGO T5 E-Paper S3 Pro with these primary capabilities:
- Tasks
- Reader
- Timers / focus sessions
- Optional desk-board mode
- Optional sync over Wi-Fi, with LoRa reserved for compact summaries or relay-style features

This board has a 4.7-inch 960×540 E-Paper display, GT911 touch, PCF85063 RTC, TF card support, ESP32-S3, battery support, and SX1262 LoRa on the official hardware and examples.[cite:17][cite:18]

## Non-goals

Do not turn this into a generic tablet UI, a highly animated LVGL showcase, or a framework-heavy experiment. The product should feel like a calm e-paper appliance.

## Stack policy

- Preferred firmware base: the official `Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO` project as bring-up reference.[cite:18]
- Preferred display path: `epdiy` / vendor-supported direct-drive path for the ED047TC1 panel on this board family.[cite:17][cite:18]
- Preferred architecture: ESP-IDF-style modular components, or PlatformIO with a component-like structure.
- LVGL is allowed, but only as a thin UI layer. Do not let UI framework code own hardware logic or application state.
- Avoid Arduino-only patterns unless a task explicitly asks for Arduino compatibility.

## Operating principles

1. Preserve working hardware initialization.
2. Never invent pin mappings, bus configs, controller models, or board revisions.
3. Keep logic modular and boring.
4. Optimize for low refresh count, predictable state, and good sleep/wake behavior.
5. Prefer paged layouts over scrolling.
6. Prefer explicit state machines and data models over callback soup.

## Hardware facts to respect

Assume the following unless the repository owner overrides them:
- Board: LilyGO T5 E-Paper S3 Pro / T5S3 4.7-inch E-Paper Pro.[cite:17][cite:18]
- MCU: ESP32-S3-WROOM-1 class board support in official materials.[cite:17]
- Display: 4.7-inch E-Paper, 960×540, ED047TC1 in the board documentation and repo text.[cite:17][cite:18]
- Touch: GT911.[cite:17]
- RTC: PCF85063.[cite:17]
- Radio: SX1262 LoRa on supported variants/examples.[cite:17][cite:18]
- Storage: TF / microSD support in official materials and examples.[cite:17][cite:18]

If a code change depends on pins, voltages, waveform settings, or board revisions, check the board source or documentation first instead of guessing.

## Product model

Treat the firmware as one product with multiple modes, not as disconnected demo apps.

Primary modes:
- Home
- Tasks
- Reader
- Focus timer
- Desk board
- Settings
- Sync

Expected UX characteristics:
- Large touch targets
- Stable layouts
- Minimal animation
- Partial refresh where safe
- Full refresh only when needed
- Reader is page-based, not scroll-based
- Desk board is glanceable from distance

## Architecture rules

Use these layers:
- `board/` — power, RTC, battery, touch, SD, radios, sleep/wake
- `display/` — e-paper driver wrapper, refresh strategy, theme, drawing helpers
- `services/` — tasks, reader, timer, settings, sync
- `ui/` — screens, widgets, navigation, view models
- `main/` — app bootstrap and top-level state machine

Hard boundaries:
- UI must not directly poke hardware except through service or display abstractions.
- Services must not depend on LVGL types.
- Board code must not know about screens.
- Domain state should be serializable.

## Agent workflow

For any substantial task, work in this order:
1. Read `BOARD.md` and `ARCHITECTURE.md`.
2. Find the smallest module that can own the change.
3. Make the smallest safe patch.
4. Keep public interfaces simple.
5. Add or update docs when behavior changes.

## Patch style

Prefer:
- Small commits
- Explicit enums / structs
- Narrow interfaces
- Predictable names
- Compile-time constants in one place

Avoid:
- Massive rewrites
- Hidden globals
- Template-heavy abstractions without clear value
- Conflating render logic and business logic
- “Smart” generic frameworks for everything

## LLM prompt contract

When asked to generate code for this repo, assume:
- The device is e-paper-first.
- Battery life matters.
- Touch latency is acceptable; full-screen animation is not required.
- LoRa is optional and should not contaminate the core app architecture.
- Wi-Fi sync comes before any complex mesh concept.
- Stability is more important than novelty.

## What agents should ask before large changes

Agents should request clarification before changing:
- Display library choice
- Storage format for tasks/notes/books
- Sync protocol
- Reader scope: TXT/MD first vs EPUB immediately
- Timer behavior during deep sleep
- Desk board trigger: dock, idle timer, schedule, or manual toggle

## Definition of done

A change is done when:
- It compiles
- It does not break core board bring-up
- It respects e-paper constraints
- It fits the repository architecture
- It has concise documentation where needed
