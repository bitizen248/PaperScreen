# Battery Percentage Service Technical Spec

## Purpose

Add a small battery percentage service so PaperScreen can show battery state in the shared status bar, Settings, desk-board mode, and future sync policy without coupling UI screens to board power hardware.

The status should update on minute changes, using the same low-frequency cadence as the status-bar clock. Battery reads must not create a high-frequency polling loop or force unnecessary e-paper refreshes.

## Scope

V1 includes:
- initialize the board fuel-gauge path
- read battery percentage from the BQ27220 fuel gauge
- expose a cached battery status to the app shell and UI view models
- refresh the cached value at boot and when the displayed minute changes
- redraw the status bar only when the visible battery text changes
- report unknown battery state as `--%`
- provide low battery and charging flags when the gauge can supply them

Out of scope for V1:
- battery calibration UI
- fuel-gauge data-memory tuning
- charger configuration changes
- charge-time or runtime prediction
- historical battery graphs
- ADC voltage estimation as the primary path
- wake from deep sleep based on battery threshold
- sync throttling based on battery level

## Architecture Position

Add:

```text
src/board/board_battery.h
src/board/board_battery.cpp
src/services/battery_service.h
src/services/battery_service.cpp
```

Expected existing dependencies:

```text
lib/BQ27220/bq27220.h
src/main/app.h
src/display/widgets/status_bar.cpp
src/ui/home_screen.*
src/ui/settings_screen.*
src/ui/scaffold_app.*
src/ui/trmnl_screen.*
```

Layer ownership:
- `board_battery` owns direct BQ27220 access on the shared I2C bus.
- `BatteryService` owns cached status, update cadence, formatting, and error policy.
- The app shell decides when to call `BatteryService::update()`.
- UI screens consume formatted status through view models only.
- Display code renders the string it is given and does not read battery hardware.

Do not add battery reads directly to `HomeScreen`, `SettingsScreen`, status-bar drawing, TRMNL rendering, or sync code.

## Hardware Source

Use the fuel gauge documented in `docs/pinmap.md`:

```text
BQ27220YZFR  0x55  Fuel gauge
BQ25896      0x6B  Battery charger
```

V1 percentage should come from BQ27220 `StateOfCharge`.

The charger can be used later for richer charge-source state, but V1 should avoid changing charger registers. If charging state is needed, prefer BQ27220 battery-status flags first. If those are unreliable on the target board, document the limitation and add a separate charger read path in a follow-up spec or patch.

Do not use the older `BATT_PIN 36` ADC example as the primary implementation for this board. It is acceptable as a documented fallback only after the fuel gauge path is proven unavailable on a board revision.

## Public API

Board layer:

```cpp
enum class BoardBatteryState {
    Unknown,
    Discharging,
    Charging,
    Full,
};

struct BoardBatteryStatus {
    bool initialized = false;
    bool present = false;
    bool valid = false;
    uint8_t percentage = 0;
    uint16_t voltage_mv = 0;
    int16_t current_ma = 0;
    BoardBatteryState state = BoardBatteryState::Unknown;
};

class BoardBattery {
public:
    bool begin();
    BoardBatteryStatus read();
};
```

Service layer:

```cpp
struct BatteryStatus {
    bool initialized = false;
    bool valid = false;
    uint8_t percentage = 0;
    bool charging = false;
    bool full = false;
    bool low = false;
    int64_t last_update_epoch = 0;
};

class BatteryService {
public:
    void begin(BoardBattery& battery);

    BatteryStatus status() const;
    bool update(int64_t now_epoch);
    bool format_percentage(char* out, size_t out_size) const;

private:
    BoardBattery* battery_ = nullptr;
    BatteryStatus status_;
};
```

Rules:
- Clamp visible percentage to `0..100`.
- Treat out-of-range or failed reads as invalid and keep showing the previous valid value if one exists.
- If no valid value has ever been read, format as `--%`.
- `update()` returns `true` when formatted visible status may have changed.
- `format_percentage()` should produce short status-bar text such as `87%`, `5%`, or `--%`.

## Update Cadence

Battery update should attach to the minute update already used by the status-bar clock.

Current app behavior:

```text
App::loop()
  -> refresh_status_clock_if_needed()
  -> checks once per second
  -> formats local HH:MM
  -> redraws status bar only when HH:MM changes
```

Required V1 behavior:

```text
Boot
  -> BatteryService.begin(board_battery)
  -> BatteryService.update(time.now_epoch())
  -> first render uses cached battery text

Loop
  -> refresh_status_clock_if_needed()
  -> detect minute text change
  -> BatteryService.update(time.now_epoch())
  -> rebuild current StatusBarViewModel
  -> apply clock and battery text
  -> render status bar only if time text or battery text changed
```

Do not poll the fuel gauge every second. The one-second check may remain as the cheap minute-boundary detector, but hardware battery reads should happen only:
- at boot
- when the displayed minute changes
- after explicit future actions that materially affect power state, if needed

When RTC time is not configured, the minute boundary can still be detected from the status-bar clock string. If the clock is `--:--`, use a bounded fallback of one battery read per 60 seconds while awake.

## App Integration

Add members:

```cpp
BoardBattery battery_board_;
BatteryService battery_;
char status_bar_battery_[6] = "--%";
char rendered_status_bar_battery_[6] = "";
unsigned long last_battery_fallback_update_ms_ = 0;
```

Startup ordering:

```text
board_.begin()
settings_.begin()
wifi_.begin()
rtc_.begin()
time_.begin(...)
battery_board_.begin()
battery_.begin(battery_board_)
battery_.update(time_.now_epoch())
display_.begin(...)
```

Status-bar helpers:

```cpp
void apply_status_bar_battery(StatusBarViewModel& status_bar);
bool refresh_status_bar_if_needed();
```

`current_status_bar_model()` should continue to ask the active screen for its base model, then the app shell applies global status fields. This keeps screens from depending on `BatteryService`.

## UI Behavior

Status bar:

```text
Home       14:30       87%
Settings   14:30       --%
Reader     14:30        5%
```

Settings can add a Power page later. V1 only needs the shared status bar unless implementation work already touches Settings rows.

If a Settings Power row is added in the same patch, keep it read-only:

```text
Settings
  Power
    Battery     87%
    State       Discharging / Charging / Full / Unknown
```

Desk-board mode should read the same `BatteryStatus` snapshot when it adds battery display. It must not schedule its own battery polling loop.

## Error Handling

Initialization failure:
- `BoardBattery::begin()` returns false.
- `BatteryService::status().initialized` is false.
- UI displays `--%`.
- Log one concise message such as `[battery] fuel gauge init failed`.

Read failure:
- retain the previous valid display value if available
- set `valid = false` only if there is no usable cached value
- avoid repeated noisy logs; at most one failure log per minute update is acceptable

Suspicious values:
- clamp `StateOfCharge > 100` to `100`
- treat obviously invalid all-zero reads with `present == false` as unknown
- do not infer a percentage from voltage in V1

Low battery:
- `low` should be true at or below 15 percent by default
- no shutdown or sleep action is required in V1

## E-Paper Refresh Policy

Battery percentage changes should redraw only the status-bar region.

Rules:
- Do not refresh the full screen for a routine battery update.
- Batch battery and minute changes into one status-bar redraw.
- Do not redraw if formatted time and battery strings are unchanged.
- Avoid charging glyph animation; a static percentage is enough for V1.

If future UI adds a battery icon, use discrete static levels and update them on the same minute cadence.

## Logging

Suggested logs:

```text
[battery] begin
[battery] ready soc=87 voltage=4012 current=-42 state=discharging
[battery] read failed
```

Do not print every loop iteration. Per-minute logs are acceptable during bring-up, but should be easy to remove or gate behind a debug flag.

## Testing

Minimum verification:
- firmware compiles
- boot with gauge present shows a percentage instead of `--%`
- boot with gauge unavailable does not crash and shows `--%`
- status bar updates battery at most once per minute while awake
- status bar does not redraw when minute and battery strings are unchanged
- Home, Settings, scaffold apps, and TRMNL screens all receive the same global battery text

Useful host-testable units:
- `format_percentage()` returns `--%`, `0%`, `5%`, `87%`, and `100%`
- invalid reads preserve the previous formatted value
- low-battery threshold triggers at 15 percent and below

## Implementation Notes

Keep the first patch narrow:
1. Add `BoardBattery` and `BatteryService`.
2. Wire them into `App`.
3. Replace hard-coded `"--%"` status-bar values through app-shell global formatting.
4. Attach `BatteryService::update()` to minute-change handling.
5. Compile and verify on device.

Do not combine this with charger policy, sync throttling, sleep behavior, or Settings restructuring.

## Open Questions

- Does the target unit require BQ27220 data-memory initialization before `StateOfCharge` is trustworthy?
- Are BQ27220 charging/full flags reliable on the current board revision, or should BQ25896 be read for charger state later?
- Should `low` be user-configurable after Settings grows a Power page?
