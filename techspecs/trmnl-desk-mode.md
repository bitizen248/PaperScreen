# TRMNL Desk Mode Integration Technical Spec

## Purpose

Integrate TRMNL as the content source for PaperScreen's Desk mode while preserving the existing local-first firmware architecture.

PaperScreen remains the device firmware and owns:
- board initialization
- display driving
- touch/navigation
- sleep/wake behavior
- local app modes such as Tasks, Reader, Focus, Settings, and Sync

TRMNL provides rendered desk-board content as an e-paper friendly image fetched over Wi-Fi.

## Product Behavior

Desk mode should behave like a calm e-paper appliance:
- wake or enter Desk mode
- fetch the current TRMNL screen
- render it full-screen on the 960x540 ED047TC1 panel
- keep Wi-Fi active only while fetching unless configured otherwise
- wait or sleep until the next refresh window
- fall back to cached content or an offline screen when networking fails

TRMNL must not become a dependency for the rest of the product. Tasks, Reader, Focus, and Settings should remain usable without TRMNL.

## TRMNL Model

TRMNL's hosted service renders plugin output server-side into a PNG image. The device asks the TRMNL API for display content and receives JSON containing an image URL and refresh timing.

Relevant documented endpoints:

- `GET https://trmnl.com/api/display`
  - Advances playlist content.
  - Requires header: `access-token: <api_key>`.
  - Returns fields such as `status`, `image_url`, `image_name`, `refresh_rate`, `update_firmware`, `firmware_url`, and `reset_firmware`.

- `GET https://trmnl.com/api/current_screen`
  - Returns the currently rendered screen without advancing the playlist.
  - Requires header: `access-token: <api_key>`.
  - Intended for mirroring current screen content.

Sources:
- https://docs.trmnl.com/go/how-it-works
- https://docs.trmnl.com/go/private-api/screens
- https://docs.trmnl.com/go/diy/byod
- https://docs.trmnl.com/go/diy/byod-s

Related local specs:
- `techspecs/wifi-service.md`
- `techspecs/settings-app.md`

## Integration Mode

Support two TRMNL fetch modes:

```cpp
enum class TrmnlMode {
    PlaylistAdvance,
    MirrorCurrent,
};
```

`PlaylistAdvance` calls `/api/display`. Use this when PaperScreen should behave like its own TRMNL display device.

`MirrorCurrent` calls `/api/current_screen`. Use this when PaperScreen should mirror the current screen from an existing TRMNL/BYOD setup.

Recommended initial default: `MirrorCurrent`, because it is less surprising during development and does not advance a playlist.

## Firmware Layers

Follow the repository architecture:

```text
src/
  board/
  display/
  services/
  ui/
  main/
```

Add:

```text
src/services/trmnl_service.h
src/services/trmnl_service.cpp
src/ui/desk_screen.h
src/ui/desk_screen.cpp
src/display/image_renderer.h
src/display/image_renderer.cpp
```

Wi-Fi is specified separately in `techspecs/wifi-service.md`.
Settings app behavior is specified separately in `techspecs/settings-app.md`.

Optional later:

```text
src/services/image_cache_service.h
src/services/image_cache_service.cpp
```

## Ownership Boundaries

### `WifiService`

Responsibilities:
- connect to configured Wi-Fi
- disconnect after fetch when requested
- report connection state
- report RSSI for TRMNL request headers if needed

Must not:
- know about TRMNL
- know about display rendering
- own app mode transitions

### `TrmnlService`

Responsibilities:
- store TRMNL fetch state
- build TRMNL API requests
- parse API response JSON
- download the image from `image_url`
- expose refresh timing
- expose errors in product terms

Must not:
- draw directly to the display
- own Wi-Fi credentials
- own app navigation
- decide when the whole device enters deep sleep

### `DeskScreen`

Responsibilities:
- translate TRMNL service state into a Desk mode view state
- request refreshes
- decide whether to show loading, offline, auth error, cached image, or fresh image
- notify app shell when the next refresh is scheduled

Must not:
- perform HTTP requests directly
- parse PNG data directly
- own hardware sleep configuration

### `Display` / `ImageRenderer`

Responsibilities:
- decode or accept decoded image data
- scale/crop/pad to 960x540
- convert pixels to the e-paper framebuffer format
- render via `epdiy`

Must not:
- perform network requests
- know about TRMNL access tokens

### `PowerService`

Responsibilities:
- provide a Desk mode sleep path
- configure wake source
- optionally sleep until the next TRMNL refresh window when RTC wake is available

Must not:
- decide which TRMNL endpoint to use
- parse TRMNL JSON

## Data Types

```cpp
enum class TrmnlFetchStatus {
    Idle,
    ConnectingWifi,
    RequestingMetadata,
    DownloadingImage,
    Ready,
    Offline,
    Unauthorized,
    ServerError,
    DecodeError,
    NoContent,
};

enum class TrmnlMode {
    PlaylistAdvance,
    MirrorCurrent,
};

struct TrmnlSettings {
    bool enabled = false;
    TrmnlMode mode = TrmnlMode::MirrorCurrent;
    char api_key[96] = {};
    uint32_t fallback_refresh_seconds = 1800;
    bool sleep_between_refreshes = true;
    bool disconnect_wifi_after_fetch = true;
};

struct TrmnlDisplayResponse {
    int status = 0;
    char image_url[384] = {};
    char image_name[128] = {};
    uint32_t refresh_seconds = 1800;
    bool update_firmware = false;
    bool reset_firmware = false;
};

struct TrmnlSnapshot {
    TrmnlFetchStatus status = TrmnlFetchStatus::Idle;
    TrmnlDisplayResponse response;
    uint32_t fetched_at_epoch = 0;
    uint32_t next_refresh_epoch = 0;
    bool has_cached_image = false;
};
```

## Desk Mode State Machine

```text
EnterDesk
  -> Disabled        when TRMNL is not configured
  -> ShowCached      when cached image exists
  -> ConnectWifi
  -> FetchMetadata
  -> DownloadImage
  -> DecodeImage
  -> RenderImage
  -> ScheduleRefresh
  -> IdleOrSleep
```

Error transitions:

```text
ConnectWifi failed
  -> ShowOffline
  -> RetryLater

HTTP 401 / 403
  -> ShowAuthError
  -> DoNotRetryAggressively

No image_url
  -> ShowNoContent
  -> RetryLater

Image download/decode failed
  -> ShowCached if available
  -> ShowDecodeError if no cache
```

## API Request Details

### Playlist Advance

```http
GET /api/display HTTP/1.1
Host: trmnl.com
access-token: <api_key>
```

Optional headers if available:

```http
User-Agent: PaperScreen/<firmware_version>
X-Firmware-Version: <firmware_version>
X-Wifi-RSSI: <rssi>
```

### Mirror Current

```http
GET /api/current_screen HTTP/1.1
Host: trmnl.com
access-token: <api_key>
```

### Response Handling

Expected useful fields:

```json
{
  "status": 0,
  "image_url": "https://...",
  "image_name": "plugin-...",
  "refresh_rate": "1800",
  "update_firmware": false,
  "firmware_url": null,
  "reset_firmware": false
}
```

Implementation notes:
- Treat `refresh_rate` as seconds.
- If `refresh_rate` is missing or invalid, use `fallback_refresh_seconds`.
- Ignore TRMNL firmware update fields for v1. PaperScreen firmware updates are out of scope.
- Do not log the API key.
- Redact query strings from logged image URLs unless needed for debugging.

## Image Pipeline

TRMNL serves PNG images. PaperScreen must convert them to the board display.

Pipeline:

```text
image_url
  -> HTTPS download
  -> PNG decode
  -> orientation check
  -> fit to 960x540
  -> grayscale conversion
  -> e-paper framebuffer
  -> full refresh
```

Fit policy:

```cpp
enum class ImageFit {
    Contain,
    Cover,
    Stretch,
};
```

Recommended default: `Contain`.

For v1:
- center the image
- preserve aspect ratio
- fill margins with white
- avoid text clipping

For later:
- allow `Cover` if users prefer full-bleed dashboard rendering
- allow server-side target dimensions if TRMNL/BYOS supports it

## Caching

Cache the most recent successful TRMNL image.

Preferred cache location:
- SD card when mounted and reliable
- fallback to RAM-only cache if SD is not initialized

Suggested paths:

```text
/trmnl/current.png
/trmnl/current.meta.json
```

Metadata:

```json
{
  "image_name": "plugin-...",
  "fetched_at": 1747596567,
  "refresh_seconds": 1800,
  "source": "current_screen"
}
```

Rules:
- render cached image immediately when entering Desk mode
- fetch in the background only after cached content is visible
- replace cache only after a full successful download and decode
- never delete the last good cache because a new fetch failed

## Settings And Secrets

TRMNL API key must not be hard-coded.

Initial setup options:
- temporary serial command during development
- SD config import
- later Settings screen input
- later local captive portal

Preferred persistent storage:
- NVS for API key and mode
- SD card only for import/export and image cache

Do not print secrets to Serial logs.

Example SD import file:

```json
{
  "trmnl": {
    "enabled": true,
    "mode": "mirror_current",
    "api_key": "redacted",
    "fallback_refresh_seconds": 1800,
    "sleep_between_refreshes": true
  }
}
```

## Sleep And Refresh

Desk mode should avoid continuous Wi-Fi.

Recommended behavior:
- connect Wi-Fi
- fetch metadata
- download image
- disconnect Wi-Fi
- render
- sleep or idle until next refresh

If RTC wake is implemented:
- schedule wake at `now + refresh_seconds`
- enter deep sleep
- on wake, return to Desk mode and refresh

If RTC wake is not implemented yet:
- remain in locked idle mode
- use `millis()` to trigger refresh
- keep loop delay coarse

Minimum refresh interval:
- enforce a local minimum such as 5 minutes
- default to 30 minutes if API response is invalid

## UI States

Desk mode must show clear but minimal states:

- Disabled: "TRMNL not configured"
- Connecting: "Connecting"
- Loading: "Updating"
- Offline: "Offline - showing last screen"
- Auth error: "TRMNL key rejected"
- No content: "No TRMNL screen"
- Decode error: "Image unsupported"

When cached content exists, prefer showing cached content plus a small status marker over replacing the whole screen with an error.

## Failure Policy

Network failures:
- keep last image
- retry at next scheduled interval
- allow manual refresh from touch/dropdown later

Auth failures:
- do not retry frequently
- show settings/action hint

Server failures:
- back off using fallback interval
- keep last image

Image failures:
- keep last image
- log image name and decode result

## Security And Privacy

- Store the TRMNL API key in NVS.
- Redact API key in logs.
- Do not log full signed image URLs by default.
- Use HTTPS.
- Do not accept arbitrary remote URLs from local user input except the TRMNL/BYOS base URL setting.
- If BYOS base URL is supported later, default to HTTPS but allow HTTP for local development only behind an explicit setting.

## BYOS Support

TRMNL BYOD/S and BYOS patterns allow a compatible server to return the same kind of JSON with an `image_url`.

Add later:

```cpp
struct TrmnlServerSettings {
    char base_url[160] = "https://trmnl.com";
    bool allow_insecure_http = false;
};
```

Then build endpoint URLs from `base_url`:

```text
<base_url>/api/display
<base_url>/api/current_screen
```

For v1, hardcode hosted TRMNL endpoints and leave BYOS as a documented future extension.

## Dependencies

Likely needed:
- Arduino `WiFi`
- Arduino `HTTPClient` or lower-level HTTPS client
- JSON parser such as ArduinoJson if already acceptable in the project
- PNG decoder compatible with ESP32-S3 memory constraints

Existing vendor libraries include image-related libraries such as `PNG`/`JPEG` examples in the tree, but choose based on actual build compatibility and memory use.

Avoid:
- browser engines
- HTML/Liquid rendering on-device
- LVGL ownership of the Desk mode data model

## Implementation Phases

### Phase 1: Service Skeleton

- Add `TrmnlSettings`.
- Add `TrmnlService` with mocked response.
- Add `DeskScreen` placeholder states.
- Add Desk tile routing to open real Desk screen instead of generic scaffold.

Done when:
- Desk mode can show Disabled, Loading, Ready, and Error states without network.

### Phase 2: Cached Image Rendering

- Add image renderer abstraction.
- Decode and render a local PNG test asset.
- Fit image to 960x540.
- Render via full refresh.

Done when:
- Desk mode can render a known local TRMNL-like PNG.

### Phase 3: TRMNL Metadata Fetch

- Add `WifiService`.
- Fetch `/api/current_screen`.
- Parse `image_url` and `refresh_rate`.
- Handle auth and network errors.

Done when:
- Serial logs show successful metadata fetch with redacted sensitive values.

### Phase 4: Image Download And Cache

- Download PNG from `image_url`.
- Decode and render.
- Save successful image and metadata to SD when available.
- Show cache on next Desk entry.

Done when:
- Desk mode shows TRMNL content and survives a later network failure using cache.

### Phase 5: Refresh Scheduling

- Use TRMNL `refresh_rate`.
- Disconnect Wi-Fi after fetch.
- Add idle refresh loop.
- Later add RTC/deep sleep wake.

Done when:
- Desk mode updates on schedule without keeping Wi-Fi continuously active.

### Phase 6: Settings UI

- Configure API key.
- Configure mode: Mirror Current or Playlist Advance.
- Configure refresh fallback.
- Configure sleep behavior.

Done when:
- Device can be configured without recompiling firmware.

## Code Touchpoints

Existing files likely touched:

```text
src/main/app.h
src/main/app.cpp
src/ui/home_screen.h
src/ui/home_screen.cpp
src/display/display.h
src/display/display.cpp
src/services/settings_service.h
src/services/settings_service.cpp
src/services/power_service.h
src/services/power_service.cpp
platformio.ini
```

New files:

```text
src/services/wifi_service.h
src/services/wifi_service.cpp
src/services/trmnl_service.h
src/services/trmnl_service.cpp
src/ui/desk_screen.h
src/ui/desk_screen.cpp
src/display/image_renderer.h
src/display/image_renderer.cpp
```

## Acceptance Criteria

The integration is complete when:

- Desk mode can be selected from Home.
- TRMNL can be disabled without affecting other modes.
- API key is configurable and not hard-coded.
- The firmware can fetch TRMNL metadata over HTTPS.
- The firmware can download and render the returned PNG.
- The rendered image fits the 960x540 display without clipping by default.
- Last successful image is cached.
- Network failure shows cached content or a clear offline state.
- Auth failure is visible and does not cause aggressive retry loops.
- Refresh timing respects TRMNL `refresh_rate` with a local fallback.
- Wi-Fi is disconnected after fetch when configured.
- The change compiles in PlatformIO.
- Core board bring-up remains unchanged except for explicit integration wiring.

## Open Questions

- Should PaperScreen default to `MirrorCurrent` or `PlaylistAdvance` for the first user-facing release?
- Where should initial Wi-Fi provisioning live: serial, SD import, captive portal, or Settings screen?
- Should the first implementation cache on SD immediately, or start with RAM-only cache?
- Which PNG decoder is most reliable with the current PlatformIO/Arduino dependency set?
- Should Desk mode use RTC deep sleep wake in v1, or wait until board RTC support is fully wrapped?
- Should BYOS base URL be supported in v1 or kept as a later extension?
