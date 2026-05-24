# microSD Filesystem Technical Spec

## Purpose

Add a shared microSD-backed filesystem for PaperScreen so local data, import/export, and large caches can survive reboot without coupling app logic to SD card hardware.

The filesystem should support:
- local productivity state that is too large or too user-visible for NVS
- reader documents and bookmarks
- TRMNL image cache
- sync staging and import/export
- diagnostics and recovery files

NVS remains the right place for small configuration values such as Wi-Fi credentials, feature flags, and the selected TRMNL mode.

## Hardware Baseline

The board has TF / microSD support in official materials and examples. The checked-in SD example uses the shared SPI bus:

```text
SCLK: GPIO14
MISO: GPIO21
MOSI: GPIO13
SD_CS: GPIO12
LoRa_CS: GPIO46
```

Bring-up rules:
- Initialize SPI once in the board layer.
- Drive `SD_CS` high before selecting other SPI devices.
- Drive `LORA_CS` high before SD access because SD and LoRa share SPI.
- Do not invent alternate pins; verify against board source or documentation before changing pin mappings.
- Treat card absence as a normal runtime state, not a boot failure.

## Scope

### In Scope

- Mount and unmount microSD from the board layer.
- Expose a small storage service for app and service code.
- Provide app-owned directories under one PaperScreen root.
- Provide atomic write helpers for structured files.
- Provide safe read-only listing for import flows.
- Report card status to Settings and diagnostics.
- Let apps degrade cleanly when the card is missing.

### Out Of Scope

- Full database engine.
- Arbitrary shell-like file manager.
- User-editable settings format as the primary settings store.
- SD as required boot media.
- Hot-plug support beyond periodic card-present checks and explicit remount.
- LoRa storage behavior.

## Layer Ownership

### Board Layer

Suggested module:

```text
src/board/board_storage.h
src/board/board_storage.cpp
```

Responsibilities:
- own SD card initialization details
- own SPI chip-select discipline
- expose mount state, card type, capacity, and free space
- provide basic open/read/write/remove/rename operations or a filesystem handle
- never know about Tasks, Reader, TRMNL, or Sync

### Service Layer

Suggested module:

```text
src/services/storage_service.h
src/services/storage_service.cpp
```

Responsibilities:
- define the PaperScreen directory layout
- provide atomic structured-file writes
- provide path validation
- provide app-scoped helpers such as `read_json`, `write_json_atomic`, `exists`, `list_dir`
- translate board storage errors into product-level statuses

Services such as `TrmnlService`, `ReaderService`, and `TaskService` should depend on `StorageService`, not directly on `SD`, `SPI`, or board pins.

### UI Layer

Settings and app screens may show storage status and import choices, but they must not call SD APIs directly.

## Directory Layout

Root all PaperScreen-owned files under a single directory:

```text
/paperscreen/
  cache/
    trmnl/
      current.png
      current.meta.json
      next.png.tmp
      next.meta.json.tmp
  reader/
    books/
    state/
      library.json
      positions.json
  tasks/
    tasks.json
    history.jsonl
  timer/
    sessions.jsonl
  sync/
    inbox/
    outbox/
    archive/
  import/
    tasks/
    reader/
    settings/
  export/
    tasks/
    reader/
    diagnostics/
  logs/
    boot.jsonl
    errors.jsonl
```

Rules:
- Firmware creates missing directories lazily.
- App code receives logical app paths, not raw absolute SD paths where possible.
- Temporary files use `.tmp` and are renamed only after a successful flush.
- Apps must not write outside `/paperscreen/`.

## App Candidates

Strong candidates:
- `TRMNL`: persistent last-good PNG and metadata so desk mode can render after reboot or failed Wi-Fi.
- `Reader`: imported TXT/MD books, pagination cache, bookmarks, and last position.
- `Tasks`: local task backup, task import/export, and completion history when the task list outgrows NVS.
- `Sync`: durable outbox/inbox staging for Wi-Fi sync and conflict recovery.
- `Settings`: import/export bundles, diagnostics export, and storage status.

Medium candidates:
- `Focus timer`: session history and optional daily summary export.
- `Desk board`: cached widgets, rendered snapshots, and schedule data.
- `Diagnostics`: boot logs, network failure logs, and display/storage test outputs.

Poor candidates:
- high-frequency UI state
- secrets such as Wi-Fi passwords or TRMNL API keys
- LVGL object state
- data required before the board storage layer is initialized

## Mount Policy

Startup flow:

```text
Board begin
  -> initialize shared SPI
  -> set SD_CS high and LORA_CS high
  -> initialize touch/display/other board devices
  -> attempt SD mount when StorageService begins
  -> create /paperscreen directories if card is mounted
```

Runtime states:

```cpp
enum class StorageStatus {
    NotStarted,
    Mounted,
    CardMissing,
    MountFailed,
    UnsupportedFormat,
    IoError,
};
```

Rules:
- Boot continues when SD is missing.
- Settings should show `No card`, `Mounted`, or `Error`.
- Apps may request storage and receive a typed failure.
- Failed mount should not be retried in a tight loop.
- A manual remount action can be added later in Settings.

## File Formats

Use boring, portable formats:
- JSON for snapshots and metadata.
- JSON Lines for append-only history and logs.
- Original file formats for reader imports.
- PNG for TRMNL cache.

Avoid:
- binary structs written directly from memory
- hidden host-specific path assumptions
- storing pointers, enum ordinals without names, or UI internals

## Atomic Writes

Structured writes must use a temp-and-rename pattern:

```text
write /paperscreen/tasks/tasks.json.tmp
flush and close
rename existing tasks.json to tasks.json.bak when useful
rename tasks.json.tmp to tasks.json
```

Rules:
- Keep the previous good file when a write fails.
- Validate JSON before replacing important files.
- Never delete the last-good TRMNL image because a refresh failed.
- Use bounded file sizes for logs and caches.

## Error Handling

Required behavior:
- Missing card: app continues with RAM/NVS fallback when available.
- Read failure: app reports degraded mode and keeps current in-memory state.
- Write failure: app keeps in-memory state and reports that persistence failed.
- Corrupt JSON: app attempts `.bak` or defaults, then surfaces a recovery status.
- Full card: app refuses cache growth and preserves essential state.

Storage failures should not trigger full-screen refresh loops. Use one stable status message or a Settings row.

## Power And E-Paper Constraints

- Do not keep Wi-Fi or SD active only to poll files.
- Batch writes after user-visible state changes where data loss risk is acceptable.
- Write critical events immediately when needed, then return to idle.
- Avoid refreshing the display for background storage maintenance.
- For autonomous desk/TRMNL mode, write the cache before scheduling sleep.

## Security And Privacy

- Do not store TRMNL API keys, Wi-Fi passwords, or auth tokens on SD by default.
- Redact signed URLs in metadata; store a hash or redacted identity instead.
- Imported settings should require explicit user action.
- Diagnostics export should avoid secrets and full URLs.

## Implementation Phases

### Phase 1: Board Storage Bring-Up

Status: planned.

- Add `board_storage`.
- Mount SD using the known-good LilyGO example pins.
- Report card type, size, and mount status.
- Keep SD and LoRa chip-select lines unselected when idle.
- Add a serial diagnostic command or boot log line.

Done when:
- Firmware boots with and without a card.
- A smoke test can create, read, rename, and delete a file under `/paperscreen/`.

### Phase 2: Storage Service

Status: planned.

- Add `StorageService`.
- Create the directory layout lazily.
- Add atomic write helper.
- Add path validation for `/paperscreen/`.
- Add Settings storage status.

Done when:
- Services can use storage without including `SD.h` or `SPI.h`.

### Phase 3: TRMNL Persistent Cache

Status: planned.

- Save latest successful PNG under `/paperscreen/cache/trmnl/current.png`.
- Save metadata under `/paperscreen/cache/trmnl/current.meta.json`.
- Render cached image before network fetch.
- Replace cache only after successful download and decode.

Done when:
- TRMNL can show the previous screen after reboot with Wi-Fi unavailable.

### Phase 4: Reader Import

Status: planned.

- List `/paperscreen/import/reader/`.
- Import TXT/MD into `/paperscreen/reader/books/`.
- Store library and positions in `/paperscreen/reader/state/`.

Done when:
- Reader can open an imported text file and resume its last page.

### Phase 5: App Export And Sync Staging

Status: planned.

- Export task snapshots to `/paperscreen/export/tasks/`.
- Stage sync payloads under `/paperscreen/sync/outbox/`.
- Archive successfully synced payloads when useful.
- Follow the backend sync contract in `techspecs/backend-sync.md`.

Done when:
- A failed sync does not lose local pending changes.

## Acceptance Criteria

- Boot succeeds with no SD card inserted.
- Board storage uses verified SD pins and shared SPI chip-select discipline.
- UI and domain services do not include SD/SPI headers.
- All PaperScreen files live under `/paperscreen/`.
- Structured writes use temp-and-rename.
- TRMNL, Reader, Tasks, Sync, and Settings have clear candidate storage uses.
- Storage failures are visible but do not crash the app shell.

## Open Questions

- Should the first implementation use Arduino `SD` because the current firmware is PlatformIO/Arduino, or move directly to an ESP-IDF VFS wrapper?
- Should Settings expose a manual remount action in v1?
- What maximum card size and filesystem format should be officially supported?
- Should task state live primarily in NVS first and SD as backup, or move directly to SD-backed JSON?
- Should storage be mounted at boot or lazily on first app request?
