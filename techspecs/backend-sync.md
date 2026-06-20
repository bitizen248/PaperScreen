# PaperScreen Backend Sync Technical Spec

## Purpose

Define the backend service for PaperScreen. The backend is the primary sync path for the device and the integration layer between external productivity apps and PaperScreen's own database.

The device remains offline-first. The backend makes sync reliable, normalizes third-party data, stores PaperScreen-owned state, and provides a small device-friendly API over Wi-Fi.

## Product Role

The backend owns:
- PaperScreen cloud database
- user/device identity
- sync cursors and change logs
- external app connectors
- normalized domain records
- conflict detection and merge policy
- device API

The device owns:
- local state needed to work offline
- e-paper UI and interaction
- local persistence on NVS/microSD
- sync scheduling and retry timing
- last known good state when the backend is unavailable

External apps own:
- their source data and permissions
- app-specific identifiers
- provider-specific webhooks, APIs, and rate limits

## Goals

- Make the backend the primary sync partner for PaperScreen.
- Keep the firmware from talking directly to many third-party APIs.
- Provide one stable API optimized for a slow, battery-powered e-paper device.
- Support local-first behavior with durable change replay.
- Allow app integrations to evolve without firmware updates.
- Keep secrets and OAuth tokens off the device where possible.

## Non-Goals

- Generic automation platform.
- Real-time chatty sync.
- Browser-based full app replacement.
- Direct device-to-provider sync for every integration.
- LoRa mesh sync.
- Treating the backend as required for basic offline Tasks, Reader, or Timer use.

## Architecture

```text
External apps
  -> provider APIs / webhooks
  -> connector workers
  -> normalized backend database
  -> device sync API
  -> PaperScreen SyncService
  -> local services and microSD/NVS
```

Recommended backend modules:

```text
backend/
  api/
    device_api
    user_api
    webhook_api
  connectors/
    todoist
    google_tasks
    calendar
    readwise
    rss
    trmnl
  domain/
    tasks
    reader
    timer
    deskboard
    settings
  sync/
    changelog
    cursors
    conflict_resolution
  storage/
    database
    blob_store
  jobs/
    connector_pollers
    cleanup
```

The firmware should see only the `device_api`, not connector-specific APIs.

## Data Ownership

PaperScreen database is the canonical store for PaperScreen-normalized records.

For synced external data:
- backend stores external provider IDs
- backend stores normalized PaperScreen fields
- backend stores provider revision/version where available
- backend records source ownership per field where needed
- backend can push updates back to providers only when a connector supports it and the user enabled it

For device-created data:
- backend stores the device change as a first-class PaperScreen record
- connector workers may later propagate it to configured external apps
- failed propagation must not erase the PaperScreen record

## Domain Model

### Shared Fields

Every synced entity should include:

```json
{
  "id": "psn_01H...",
  "type": "task",
  "created_at": "2026-05-24T10:00:00Z",
  "updated_at": "2026-05-24T10:05:00Z",
  "deleted_at": null,
  "source": "paperscreen",
  "source_id": null,
  "version": 12
}
```

Rules:
- Use backend-generated stable IDs for PaperScreen entities.
- Keep provider IDs as separate source mapping fields.
- Use soft deletes so devices can observe deletes through sync.
- Use monotonically increasing backend versions for the device sync cursor.

### Tasks

Minimum task fields:

```json
{
  "id": "task_01H...",
  "type": "task",
  "title": "Draft outline",
  "notes": "",
  "status": "open",
  "priority": "normal",
  "due_date": "2026-05-25",
  "scheduled_for": "2026-05-24",
  "completed_at": null,
  "project": "Inbox",
  "tags": ["writing"],
  "source": "todoist",
  "source_id": "provider-task-id",
  "version": 42
}
```

V1 task sync should support:
- create
- update title/notes/status/due date
- complete/reopen
- soft delete
- list by changed version

### Reader

Reader records:
- library item metadata
- imported document manifest
- bookmarks
- last position
- optional highlights later

The backend should not require full document upload in V1. For TXT/MD files, device import through microSD can remain local while the backend stores metadata and reading position.

### Timer

Timer records:
- focus session start/end
- planned duration
- completion status
- optional linked task

Timer sync is append-heavy. Use JSON-like events on the device and normalized session rows in the backend.

### Desk Board

Desk board records:
- selected widgets
- compact summaries from Tasks/Calendar/Timer
- rendered or precomputed widget payloads

The backend may prepare a compact desk-board payload, but final e-paper layout remains firmware-owned unless a TRMNL-style image integration is explicitly used.

### Settings

Backend-syncable settings:
- enabled integrations
- selected task source behavior
- sync cadence preferences
- desk-board widget preferences

Settings that should not sync through normal records:
- Wi-Fi password
- device private key
- raw OAuth tokens
- TRMNL API key unless intentionally stored server-side for a connector

## Sync Model

Use versioned incremental sync.

Device keeps:
- `last_server_version`
- local outbox of unsent changes
- last successful sync timestamp
- per-domain status

Backend keeps:
- global changelog version per user
- entity rows
- tombstones for deletes
- per-device metadata
- idempotency keys for device pushes

Normal sync:

```text
Device wakes or user requests sync
  -> connect Wi-Fi
  -> POST local outbox changes
  -> GET changes since last_server_version
  -> apply server changes locally
  -> persist new last_server_version
  -> disconnect Wi-Fi when policy requires
```

The device should batch sync work to avoid repeated Wi-Fi sessions.

## Device API

Base path:

```text
/api/v1/device
```

Required headers:

```http
Authorization: Bearer <device_token>
X-Device-Id: <device_id>
X-Firmware-Version: <firmware_version>
Idempotency-Key: <uuid>   # for write requests
```

### Register Device

```http
POST /api/v1/device/register
```

Request:

```json
{
  "pairing_code": "ABCD-1234",
  "device_model": "lilygo_t5_epaper_s3_pro_4_7",
  "firmware_version": "0.1.0"
}
```

Response:

```json
{
  "device_id": "dev_01H...",
  "device_token": "redacted",
  "user_id": "usr_01H...",
  "sync_base_url": "https://api.example.com"
}
```

### Pull Changes

```http
GET /api/v1/device/sync?since=42&limit=100
```

Response:

```json
{
  "changes": [
    {
      "version": 43,
      "entity_type": "task",
      "entity_id": "task_01H...",
      "operation": "upsert",
      "entity": {
        "id": "task_01H...",
        "type": "task",
        "title": "Draft outline",
        "status": "open",
        "version": 43
      }
    }
  ],
  "next_since": 43,
  "has_more": false,
  "server_time": "2026-05-24T10:10:00Z"
}
```

### Push Changes

```http
POST /api/v1/device/sync
```

Request:

```json
{
  "base_version": 42,
  "changes": [
    {
      "client_change_id": "dev_01H:000001",
      "entity_type": "task",
      "operation": "update",
      "entity_id": "task_01H...",
      "patch": {
        "status": "done",
        "completed_at": "2026-05-24T10:12:00Z"
      }
    }
  ]
}
```

Response:

```json
{
  "accepted": [
    {
      "client_change_id": "dev_01H:000001",
      "entity_id": "task_01H...",
      "version": 44
    }
  ],
  "rejected": [],
  "server_version": 44
}
```

### Get Snapshot

```http
GET /api/v1/device/snapshot
```

Use this after pairing, recovery, or local storage loss. The response should be paginated by domain and must be compact enough for the device to process in chunks.

### Health

```http
GET /api/v1/device/health
```

Response:

```json
{
  "ok": true,
  "server_time": "2026-05-24T10:15:00Z",
  "min_supported_firmware": "0.1.0"
}
```

## Connector API Boundary

External app connectors should write normalized changes into the backend database rather than sending directly to the device.

Connector responsibilities:
- OAuth/token lifecycle
- provider polling and webhooks
- provider rate-limit handling
- mapping provider objects to PaperScreen entities
- outbound propagation for user-approved fields
- source-specific error reporting

Initial connector candidates:
- task app: Todoist, Google Tasks, Microsoft To Do, or a simple CalDAV/VTODO source
- calendar: Google Calendar or CalDAV read-only summaries
- read-later/reader: Readwise Reader, Pocket-style export, RSS
- TRMNL: optional metadata bridge, not required for core sync

V1 should start with one task connector or a simple first-party web UI/API before adding many providers.

## Conflict Policy

Default policy:
- device changes are accepted if the entity still exists
- backend assigns a new version
- server-side connector conflicts are resolved per field when possible
- deletes win over stale updates unless the update explicitly recreates the entity

Recommended task rules:
- completing a task is safe to merge even if title changed elsewhere
- title/notes use last-writer-wins with conflict audit
- due date changes use last-writer-wins
- provider delete creates a tombstone unless the user has configured PaperScreen as authoritative

Every rejected device change must return a reason:

```json
{
  "client_change_id": "dev_01H:000002",
  "reason": "entity_deleted",
  "server_entity": {
    "id": "task_01H...",
    "deleted_at": "2026-05-24T10:00:00Z"
  }
}
```

## Device Storage Integration

The device should use the microSD filesystem spec for sync staging:

```text
/paperscreen/sync/outbox/
/paperscreen/sync/inbox/
/paperscreen/sync/archive/
```

Rules:
- Local changes are written to an outbox before network sync.
- Successfully accepted changes may be archived or deleted after local state is updated.
- Pulled server changes can be staged in inbox before applying if the batch is large.
- `last_server_version` should be persisted in the local settings/state store.
- Sync must be resumable after power loss.

## Firmware Integration

Suggested firmware service:

```text
src/services/sync_service.h
src/services/sync_service.cpp
```

Dependencies:
- `WifiService` for connection lifecycle
- `StorageService` for outbox/inbox staging
- domain services for applying changes
- `SettingsService` for sync enabled/cadence/device token status

The firmware should not include connector-specific code. It should speak only the device API.

## Security

Device auth:
- each device has a unique device ID and token
- tokens are revocable
- device token is stored in NVS or secure local settings, not on SD
- API requires HTTPS outside explicit local development

User auth:
- OAuth tokens for external apps stay in the backend
- user grants connector permissions through web/mobile setup, not on the e-paper device

Logging:
- do not log device tokens
- do not log OAuth tokens
- redact provider URLs with signed parameters
- diagnostic exports should include sync status without secrets

## Sync Cadence

Suggested defaults:
- manual sync from Settings or Sync app
- automatic sync on wake if stale by more than a configured interval
- automatic sync after local task edits when Wi-Fi is enabled and battery is acceptable
- desk/TRMNL image refresh remains separate from record sync unless explicitly unified later

Avoid:
- always-on Wi-Fi
- sub-minute polling
- display refresh for background sync status unless user is viewing Sync

## Error Handling

Backend unavailable:
- keep local outbox
- show last sync age
- retry later with backoff

Auth expired or revoked:
- stop retrying aggressively
- show "Pair again" or "Sync auth expired"

Provider connector failure:
- backend reports connector health
- device sync can still succeed for local records
- Settings/Sync can show connector issue as a compact status

Conflict:
- accept non-conflicting changes
- return rejected changes with reasons
- keep rejected local change available for user-visible recovery if needed

## Implementation Phases

### Phase 1: Backend Core

Status: planned.

- User table.
- Device table.
- Device registration.
- Task entity table.
- Global changelog.
- Pull changes endpoint.
- Push changes endpoint.

Done when:
- A paired device can push and pull task changes without any third-party connector.

### Phase 2: Firmware Sync Service

Status: planned.

- Add `SyncService`.
- Persist `last_server_version`.
- Persist local outbox.
- Connect through `WifiService`.
- Push outbox then pull changes.
- Apply task changes to local task state.

Done when:
- Device task changes survive offline use and later sync to backend.

### Phase 3: First External Connector

Status: planned.

- Add one task provider connector or first-party web task UI.
- Map external tasks into backend task records.
- Store provider IDs and revisions.
- Push supported PaperScreen edits back to provider.

Done when:
- A task created or completed outside the device appears on PaperScreen after sync.

### Phase 4: Reader And Timer Domains

Status: planned.

- Add reader metadata and position sync.
- Add timer session upload.
- Keep document blobs optional.

Done when:
- Reader position and timer history sync without affecting task sync.

### Phase 5: Connector Health And Recovery

Status: planned.

- Add connector status endpoint.
- Add conflict audit.
- Add snapshot restore.
- Add backend admin diagnostics.

Done when:
- Users can recover a device from backend snapshot and see connector failures separately from device sync failures.

## Acceptance Criteria

- Backend is the device's primary sync API.
- Device does not call third-party productivity APIs directly.
- Backend stores PaperScreen-normalized records in its own database.
- External connectors map provider data into the backend database.
- Device sync supports push, pull, pagination, and idempotent writes.
- Device remains usable offline and can replay local outbox later.
- Secrets stay off microSD and provider tokens stay off the device.
- Sync errors are recoverable and do not block local app use.

## Open Questions

- Which first connector should define the task mapping: Todoist, Google Tasks, Microsoft To Do, CalDAV/VTODO, or first-party web tasks?
- Should the backend expose a user web app for editing PaperScreen records directly?
- Should device pairing use QR code, short pairing code, or USB/serial provisioning?
- Should the backend keep full reader documents, or only metadata and positions for V1?
- Should conflict resolution be global last-writer-wins for V1, or field-level from the start?
- What is the expected deployment target: local home server, hosted service, or both?
