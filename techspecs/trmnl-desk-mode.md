# TRMNL Integration Technical Spec

## Purpose

Integrate TRMNL/Terminus as an e-paper content source for PaperScreen without making the rest of the firmware depend on TRMNL.

PaperScreen owns:
- board initialization
- display driving
- touch/navigation
- backlight control
- Wi-Fi lifecycle
- local app shell and settings

TRMNL/Terminus owns:
- playlist/plugin selection
- server-side screen rendering
- image URL generation
- backend refresh cadence

## Implemented Scope

The current implementation is a working TRMNL app mode, not a complete autonomous desk mode.

Implemented firmware files:

```text
src/services/wifi_service.h
src/services/wifi_service.cpp
src/services/trmnl_service.h
src/services/trmnl_service.cpp
src/ui/trmnl_screen.h
src/ui/trmnl_screen.cpp
src/display/image_renderer.h
src/display/image_renderer.cpp
```

Integrated app/display files:

```text
src/main/app.h
src/main/app.cpp
src/display/display.h
src/display/display.cpp
src/services/settings_service.h
src/services/settings_service.cpp
src/ui/home_screen.h
src/ui/home_screen.cpp
src/ui/settings_screen.h
src/ui/settings_screen.cpp
```

Current behavior:
- Home has a `TRMNL` app entry.
- TRMNL can be enabled/disabled from Settings.
- Settings can switch between Playlist and Mirror mode.
- Firmware fetches TRMNL metadata JSON.
- Firmware downloads the returned PNG from `image_url`.
- Firmware targets the official TRMNL cloud API at `https://trmnl.com`.
- Metadata and image download requests include `ID`, `Access-Token`, `Refresh-Rate`, `Battery-Voltage`, `FW-Version`, `RSSI`, `Width`, and `Height` headers where available.
- Firmware decodes PNG to RGBA through LVGL/lodepng.
- Firmware renders to the 960x540 epdiy framebuffer in landscape.
- PNGs are rendered at native size when they fit the panel, centered with white margins.
- Larger PNGs are contained to fit the panel while preserving aspect ratio.
- 4-bit grayscale framebuffer output is used.
- Non-white pixels are darkened before quantization to improve e-paper contrast.
- TRMNL image renders use a thorough refresh path: physical white clear,
  full grayscale image update, then a second grayscale settle update for lower
  ghosting and stronger contrast.
- TRMNL mode keeps Wi-Fi connected while the user remains in TRMNL mode.
- Non-TRMNL network tasks still respect the normal disconnect-after-task setting.
- Manual refresh reuses the cached image buffer when metadata returns the same `image_url`.
- Existing content shows a small `UPDATING` overlay while refresh is in progress.
- Home button opens a TRMNL menu.
- Another Home press closes the menu.
- Menu actions are Next screen, Refetch current, Special button, Switch light, Return home, and Cancel.
- Display-right swipe advances to the next playlist screen.
- Display-up/down swipe controls backlight in TRMNL mode.
- Successful images and metadata are saved as a last-good cache under the TRMNL app directory.
- If a live fetch fails, firmware attempts to render the cached last-good image and schedules the next retry from cached/fallback cadence.

Current limitations:
- API key is supplied at build time with `PAPER_SCREEN_TRMNL_API_KEY`; runtime provisioning is not implemented yet.
- Refresh rate from the backend is parsed and used for foreground automatic refresh.
- Playlist mode only means "call `/api/display`"; there is no local playlist state.
- Mirror mode only means "call `/api/current_screen`".
- Autonomous desk mode uses ESP32 timer wake from deep sleep, then re-enters TRMNL and fetches again while the battery gauge reports charging or full.
- Firmware update/reset fields from TRMNL responses are ignored.
- Only PNG is supported.
- Full image URLs are redacted in normal serial logs; image URL hashes may be logged for diagnostics.

## Terminus Screen Model

Recommended Terminus/TRMNL model for this board:

```json
{
  "keyname": "lilygo_t5_epaper_s3_pro_4_7",
  "name": "LilyGO T5 E-Paper S3 Pro 4.7",
  "width": 960,
  "height": 540,
  "color_depth": 4,
  "colour_depth": 4,
  "colors": 16,
  "colours": 16,
  "format": "png",
  "scale_factor": 1,
  "offset_x": 0,
  "offset_y": 0,
  "rotate": 0,
  "palette": "grayscale",
  "palette_id": "gray-4bit",
  "framework_class": "screen--4bit"
}
```

Notes:
- `width` and `height` are landscape panel pixels.
- `rotate` should stay `0`; firmware switches epdiy to landscape for TRMNL rendering.
- Use PNG. Firmware currently rejects BMP/unknown image formats.
- Use 4-bit / 16-color grayscale because the display path uses a 4-bit epdiy framebuffer.
- Terminus should render directly to `960x540` for best sharpness. Smaller images render centered and will not be upscaled by firmware.

## API Contract

Current firmware endpoints are selected by `TrmnlMode`:

```cpp
enum class TrmnlMode {
    Mirror,
    Playlist,
};
```

Playlist mode:

```http
GET /api/display HTTP/1.1
Host: trmnl.com
access-token: <api_key>
ID: <wifi-mac-address>
```

Mirror mode:

```http
GET /api/current_screen HTTP/1.1
Host: trmnl.com
access-token: <api_key>
ID: <wifi-mac-address>
```

Expected response fields:

```json
{
  "status": 0,
  "image_url": "http://192.168.50.147:2300/uploads/current.png",
  "image_name": "plugin-or-playlist-name.png",
  "refresh_rate": "1800",
  "update_firmware": false,
  "firmware_url": null,
  "reset_firmware": false
}
```

Implemented response handling:
- `image_url` is required.
- `image_name` is optional and logged.
- `refresh_rate` is parsed as seconds.
- Invalid or too-small `refresh_rate` falls back to local fallback timing.
- Local minimum refresh interval is currently 300 seconds.
- `update_firmware`, `firmware_url`, and `reset_firmware` are ignored.

## Desired Tighter Integration

The next integration target is to make PaperScreen behave like a proper TRMNL-compatible display, not just a manual image fetcher.

Needed behavior:
- Terminus should know the PaperScreen model and render exactly for `960x540`, 4-bit grayscale PNG.
- Playlist mode should advance according to backend playlist rules.
- Mirror mode should remain available for development and diagnostics.
- Firmware schedules the next foreground refresh from backend `refresh_rate`.
- Manual refresh should be allowed but should not permanently override backend cadence.
- If backend returns the same `image_url`, firmware should keep the current image and avoid re-downloading.
- If backend returns a new `image_url`, firmware should download and render it.
- Wi-Fi should stay connected while actively in TRMNL foreground mode.
- In autonomous desk mode, Wi-Fi disconnects between scheduled refreshes.
- Last successful image should survive failed refreshes.

Backend/Terminus requirements:
- Store device model capabilities:
  - width: `960`
  - height: `540`
  - bit depth: `4`
  - palette: 16-level grayscale
  - format: PNG
- Return `refresh_rate` for each display response.
- Return stable image URLs or content hashes so firmware can detect unchanged content.
- Support `/api/display` for playlist advance.
- Support `/api/current_screen` for mirror/debug mode.
- Prefer server-side scaling/layout over firmware-side scaling.

Firmware requirements:
- Persist TRMNL settings in NVS:
  - enabled
  - mode
  - API key
  - base URL
  - fallback refresh seconds
  - foreground Wi-Fi behavior
  - autonomous/sleep behavior
- Track next refresh due time from `refresh_rate`. Implemented for foreground TRMNL mode.
- Add automatic refresh in TRMNL mode. Implemented.
- Persistent last-image cache is implemented for successful PNG responses and failed-fetch fallback rendering.
- Keep API keys and sensitive image URLs out of logs.
- Keep TRMNL fetch logic inside `TrmnlService`.
- Keep display decode/render logic inside `ImageRenderer`/`Display`.

## Refresh Policy

Current:
- Refresh runs automatically in TRMNL foreground mode after a successful response.
- Menu Next screen calls `/api/display` immediately.
- Menu Refetch current calls `/api/current_screen` immediately.
- Backend `refresh_rate` is parsed and used by the app loop.
- Successful live responses log and schedule the next image refresh from `response.refresh_seconds`.

Target foreground behavior:

```text
Enter TRMNL
  -> fetch metadata/image
  -> render image, or render last-good cached image if live fetch fails
  -> next_refresh_due = now + response.refresh_seconds for live success
  -> next_refresh_due = now + cached/fallback refresh seconds after cached fallback
  -> keep Wi-Fi connected while TRMNL is foreground
  -> when due, show UPDATING overlay and refresh
```

Current autonomous behavior:

```text
Enable TRMNL Desk on charge
  -> Place device on charger
  -> Enter TRMNL or wake from scheduled timer
  -> fetch metadata/image
  -> render image, or render last-good cached image if live fetch fails
  -> schedule ESP32 timer wake at now + response.refresh_seconds for live success
  -> use cached/fallback refresh seconds if live fetch failed but cache rendered
  -> disconnect Wi-Fi
  -> sleep
```

Refresh constraints:
- Enforce a minimum refresh interval, currently 300 seconds.
- Use local fallback, currently 1800 seconds, when backend value is missing or invalid.
- Manual next/refetch actions are cooldown-limited to avoid accidental repeated playlist churn.
- The TRMNL menu includes a special-function action. It sends the official
  `special_function: true` request header to `/api/display`; when the device's
  TRMNL web settings configure Special Function as Restart Playlist, this acts
  as a replay-from-top control.
- RTC alarm wake is still a future board-level enhancement; current autonomous sleep uses ESP32 timer wake plus the existing button wake.
- Desk mode is armed by the TRMNL setting but only active when the battery status is charging or full.

## Image Pipeline

Implemented pipeline:

```text
image_url
  -> HTTP/HTTPS download
  -> PNG signature check
  -> lodepng RGBA decode
  -> luminance conversion
  -> contrast darkening
  -> 4-bit grayscale quantization
  -> native-size centered render when possible
  -> contain-fit render when image is larger than panel
  -> epdiy update
```

Rendering policy:
- Panel is `960x540`.
- TRMNL display rotation is landscape.
- Margins are white.
- Images smaller than the panel are not upscaled.
- This favors crispness over filling the panel.
- Terminus should render full-size `960x540` images when full-panel output is desired.

## UI Contract

TRMNL foreground controls:
- Home button: open menu.
- Home while menu is open: close menu.
- Menu:
  - Refresh
  - Special button
  - Switch light
  - Return home
  - Cancel
- Side swipe:
  - one direction enables backlight
  - opposite direction disables backlight

Status overlay:
- Only shown while an update is happening and an image is already rendered.
- It should not be used for backlight or other unrelated status.

## Caching

Current:
- RAM cache only.
- Current image buffer is kept in `TrmnlService`.
- If metadata returns the same `image_url`, the image download is skipped.

Target:
- Persist latest successful PNG and metadata.
- Prefer the shared microSD filesystem defined in `techspecs/microsd-filesystem.md`.
- Use NVS only for small metadata/settings, not full image data.

Suggested paths:

```text
/paperscreen/cache/trmnl/current.png
/paperscreen/cache/trmnl/current.meta.json
```

Suggested metadata:

```json
{
  "image_url": "redacted-or-hash",
  "image_name": "plugin-name.png",
  "fetched_at": 1747596567,
  "refresh_seconds": 1800,
  "mode": "playlist"
}
```

Rules:
- Render cached content immediately on TRMNL entry when available.
- Replace cache only after successful download and decode.
- Never delete the last good cache because a refresh failed.
- Store secrets such as the TRMNL API key in NVS, not on microSD.
- Store a redacted URL or stable hash in cache metadata instead of a signed image URL.

## microSD Filesystem Candidate

TRMNL is a strong first consumer for the shared microSD filesystem because its cached PNG is too large for NVS and has clear offline value.

TRMNL should use storage for:
- last-good PNG cache
- last-good metadata needed to decide freshness and refresh cadence
- optional diagnostics for failed metadata/image fetches

TRMNL should not use storage for:
- API key
- Wi-Fi credentials
- signed image URLs with private query strings
- playlist state owned by Terminus

Integration boundary:
- `TrmnlService` may depend on `StorageService`.
- `TrmnlService` must not include `SD.h`, `SPI.h`, or board pin definitions.
- `ImageRenderer` should render bytes provided by `TrmnlService`; it should not own cache paths.

## Security And Privacy

Required before release:
- Remove hard-coded API key.
- Store API key in NVS.
- Do not print API key.
- Redact signed image URLs or query strings.
- Allow HTTP only for local Terminus development through an explicit setting.
- Prefer HTTPS for hosted TRMNL.

## Implementation Phases

### Phase 1: Current Working Integration

Status: implemented.

- TRMNL app tile.
- Settings toggles.
- Playlist/Mirror endpoint selection.
- Metadata fetch.
- PNG download.
- PNG render.
- Manual refresh.
- RAM image reuse on unchanged `image_url`.
- TRMNL menu.
- Backlight control.

### Phase 2: Backend Cadence

Status: next.

- Add `next_refresh_due_ms_` or equivalent app state.
- Store `snapshot.response.refresh_seconds` after successful fetch.
- Auto-refresh while TRMNL is foreground.
- Keep manual refresh available.
- Display `UPDATING` overlay only during actual refresh.

Done when:
- Leaving TRMNL open automatically refreshes based on backend `refresh_rate`.

### Phase 3: Real Configuration

Status: next.

- Move API key out of source.
- Add Terminus/TRMNL base URL setting.
- Persist TRMNL enabled/mode/key/base URL in NVS.
- Keep local HTTP allowed for development.

Done when:
- Firmware can be configured without recompiling.

### Phase 4: Persistent Cache

Status: planned.

- Add dependency on the shared microSD filesystem spec.
- Save latest successful PNG to `/paperscreen/cache/trmnl/current.png`.
- Save metadata to `/paperscreen/cache/trmnl/current.meta.json`.
- Render cached image before network fetch.
- Keep last image on network/server/decode failure.
- Replace cache through temp-and-rename only after successful download and decode.

Done when:
- TRMNL can show the previous screen after reboot with Wi-Fi unavailable.

### Phase 5: Autonomous Desk Mode

Status: planned.

- Add a distinct desk/autonomous mode or TRMNL sleep policy.
- Schedule refresh using backend `refresh_rate`.
- Disconnect Wi-Fi between scheduled refreshes.
- Use RTC wake when board RTC service is ready.

Done when:
- Device can behave like a low-power TRMNL display without user interaction.

## Acceptance Criteria

Current acceptance:
- TRMNL opens from Home.
- Metadata fetch succeeds.
- PNG downloads and renders.
- Playlist/Mirror mode can be toggled.
- Manual refresh works.
- Same-image refresh skips PNG download.
- Menu actions hit the correct visible rows.
- Build passes for `T5_E_PAPER_S3_V7`.

Target acceptance:
- Device model in Terminus renders full-size `960x540` 4-bit grayscale PNG.
- Backend `refresh_rate` drives automatic foreground refresh.
- Playlist mode advances according to backend rules.
- Mirror mode fetches current screen without advancing.
- Settings persist without recompilation.
- Last successful image survives reboot/network failure.
- Autonomous mode can sleep and refresh on schedule.

## Open Questions

- Should Playlist or Mirror be the default user-facing mode?
- Should automatic foreground refresh start immediately, or only after the first manual refresh?
- Should TRMNL foreground mode always keep Wi-Fi connected, or should it be configurable?
- Should autonomous TRMNL be a separate Home app or a mode inside TRMNL?
- What exact Terminus field should be treated as the stable image identity: `image_url`, `image_name`, an ETag, or a dedicated content hash?
- Should TRMNL cache metadata store only a hash of the image URL, or a redacted URL plus server-provided image name?
