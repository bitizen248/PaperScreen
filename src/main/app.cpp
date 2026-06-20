#include "app.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_system.h>
#include <new>

#include "apps/trmnl/trmnl_settings_adapter.h"
#include "display/drawing.h"
#include "sdk/app_platform_context.h"

namespace paper_screen {

namespace {

// Survives esp_restart() (unlike statically-initialized globals, which get
// re-zeroed) so open_usb_drive() can request that the *next* boot register
// the USB MSC class before the USB stack comes up. TinyUSB only builds its
// descriptor once, at the first USB.begin() call (which Serial.begin()
// triggers implicitly under ARDUINO_USB_CDC_ON_BOOT=1), so MSC must be
// registered before that point or it never enumerates.
RTC_NOINIT_ATTR uint32_t g_usb_drive_boot_request;
constexpr uint32_t kUsbDriveBootMagic = 0x55534244;  // "USBD"

constexpr unsigned long kBootSleepDebounceMs = 80;
constexpr unsigned long kIdleSleepTimeoutMs = 60UL * 1000UL;
constexpr unsigned long kLockedDeepSleepTimeoutMs = 10UL * 60UL * 1000UL;
constexpr unsigned long kTrmnlHomeReleaseTimeoutMs = 140;
constexpr unsigned long kLoopDelayMs = 20;
constexpr int kDropdownSwipeMinDy = 72;
constexpr int kDropdownSwipeMaxDx = 180;
constexpr int kSettingsBackSwipeMinDx = 96;
constexpr int kSettingsBackSwipeMaxDy = 110;
constexpr int kTrmnlBacklightSwipeMinDy = 86;
constexpr int kTrmnlBacklightSwipeMaxDx = 220;
constexpr unsigned long kTrmnlManualActionCooldownMs = 10UL * 1000UL;
constexpr unsigned long kStatusClockCheckMs = 1000;
constexpr unsigned long kBatteryFallbackUpdateMs = 60UL * 1000UL;

}  // namespace

namespace {

TouchPoint trmnl_touch_to_display(TouchPoint raw, int display_width, int display_height)
{
    TouchPoint mapped = raw;
    mapped.x = raw.y;
    mapped.y = display_height - 1 - raw.x;
    if (mapped.x < 0) {
        mapped.x = 0;
    } else if (mapped.x >= display_width) {
        mapped.x = display_width - 1;
    }
    if (mapped.y < 0) {
        mapped.y = 0;
    } else if (mapped.y >= display_height) {
        mapped.y = display_height - 1;
    }
    return mapped;
}

TrmnlAppMenuAction trmnl_menu_action_to_app_action(TrmnlMenuAction action)
{
    switch (action) {
    case TrmnlMenuAction::None:
        return TrmnlAppMenuAction::None;
    case TrmnlMenuAction::NextScreen:
        return TrmnlAppMenuAction::NextScreen;
    case TrmnlMenuAction::RefetchCurrent:
        return TrmnlAppMenuAction::RefetchCurrent;
    case TrmnlMenuAction::SpecialFunction:
        return TrmnlAppMenuAction::SpecialFunction;
    case TrmnlMenuAction::ToggleLight:
        return TrmnlAppMenuAction::ToggleLight;
    case TrmnlMenuAction::ReturnHome:
        return TrmnlAppMenuAction::ReturnHome;
    case TrmnlMenuAction::Cancel:
        return TrmnlAppMenuAction::Cancel;
    }
    return TrmnlAppMenuAction::None;
}

}  // namespace

App::~App()
{
    close_current_app();
    clear_trmnl_cached_fallback();
}

void App::setup()
{
    if (g_usb_drive_boot_request == kUsbDriveBootMagic) {
        run_usb_drive_boot();
        return;
    }

    Serial.begin(115200);
    delay(50);

    Serial.println();
    Serial.println("[app] setup begin");
    Serial.printf("[app] reset reason=%d free_heap=%u psram=%u\n",
                  static_cast<int>(esp_reset_reason()),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getFreePsram()));
    Serial.printf("[app] wifi mac=%s\n", WiFi.macAddress().c_str());

    Serial.println("[app] board begin");
    const BoardStatus board_status = board_.begin();
    Serial.println("[app] board begin done");

    Serial.println("[app] settings begin");
    settings_.begin();
    Serial.println("[app] settings begin done");

    Serial.println("[app] storage begin");
    storage_.begin(board_storage_);
    if (storage_.available()) {
        storage_.append_text("/paperscreen/logs/boot.jsonl", "{\"event\":\"boot\"}\n");
    } else {
        Serial.println("[app] storage unavailable; boot log skipped");
    }
    Serial.println("[app] storage begin done");

    Serial.println("[app] wifi begin");
    wifi_.set_enabled(settings_.wifi_enabled());
    wifi_.begin();
    Serial.println("[app] wifi begin done");

    Serial.println("[app] rtc begin");
    rtc_.begin();
    Serial.println("[app] rtc begin done");

    Serial.println("[app] time begin");
    time_.begin(rtc_, settings_, wifi_);
    Serial.println("[app] time begin done");

    Serial.println("[app] battery begin");
    battery_board_.begin();
    battery_.begin(battery_board_);
    battery_.update(time_.now_epoch());
    trmnl_app_.state().autonomous_start_pending = power_.woke_from_timer() && trmnl_desk_mode_active();
    if (trmnl_app_.state().autonomous_start_pending) {
        Serial.println("[app] scheduled wake; TRMNL charger desk start pending");
    }
    Serial.println("[app] battery begin done");

    Serial.println("[app] display begin");
    display_.begin(settings_.refresh_policy());
    Serial.println("[app] display begin done");

    Serial.println("[app] init full refresh");
    display_.full_refresh();
    Serial.println("[app] init full refresh done");

    Serial.println("[app] boot screen render");
    display_.render_boot_screen("Loading...");
    Serial.println("[app] boot screen render done");

    Serial.println("[app] post-loading full refresh");
    display_.full_refresh();
    Serial.println("[app] post-loading full refresh done");

    Serial.println("[app] home state update");
    home_.set_board_status(board_status);
    home_.set_time_status(time_.status());
    last_activity_ms_ = millis();
    Serial.println("[app] home state update done");

    Serial.println("[app] wallpaper generate begin");
    generate_wallpaper();
    Serial.println("[app] wallpaper generate done");

    Serial.println("[app] setup done");
}

void App::loop()
{
    if (!first_render_done_) {
        Serial.println("[app] first render begin");
        if (trmnl_app_.state().autonomous_start_pending) {
            trmnl_app_.state().autonomous_start_pending = false;
            open_trmnl();
        } else {
            render_home();
        }
        first_render_done_ = true;
        Serial.println("[app] first render done");
    }

    if (locked_) {
        handle_locked_state();
        delay(kLoopDelayMs);
        return;
    }

    handle_power_button();
    handle_touch();
    handle_home_button();
    handle_trmnl_home_press_state();
    check_trmnl_refresh_schedule();
    refresh_status_clock_if_needed();
    check_idle_sleep_timeout();

    delay(kLoopDelayMs);
}

void App::handle_home_button()
{
    if (!board_.consume_home_button_pressed()) {
        return;
    }
    mark_activity();

    if (screen_ == Screen::Home) {
        return;
    }

    if (screen_ == Screen::Trmnl) {
        handle_trmnl_home_button();
        return;
    }

    Serial.println("[app] gt911 home button returning home");
    return_home_from_app();
}

void App::handle_trmnl_home_button()
{
    AppEvent event;
    event.type = AppEventType::HomeButton;
    event.timestamp_ms = millis();
    handle_trmnl_app_result(dispatch_trmnl_event(event));
}

void App::handle_trmnl_home_press_state()
{
    if (screen_ != Screen::Trmnl) {
        return;
    }

    AppEvent event;
    event.type = AppEventType::Tick;
    event.timestamp_ms = millis();
    event.value = kTrmnlHomeReleaseTimeoutMs;
    handle_trmnl_app_result(dispatch_trmnl_event(event));
}

AppEventResult App::dispatch_trmnl_event(const AppEvent& event)
{
    AppPlatformContext trmnl_context(trmnl_app_.descriptor().id, storage_, wifi_, time_, battery_, power_);
    return trmnl_app_.on_event(trmnl_context.context, event);
}

void App::handle_trmnl_app_result(const AppEventResult& result)
{
    if (!result.handled && result.action.command == 0 && !result.action.return_home) {
        return;
    }

    switch (static_cast<TrmnlAppCommand>(result.action.command)) {
    case TrmnlAppCommand::None:
        if (result.action.return_home) {
            return_home_from_app();
        }
        return;
    case TrmnlAppCommand::RenderScreen:
        Serial.println("[app] trmnl app render screen");
        render_trmnl(true);
        return;
    case TrmnlAppCommand::ShowMenu:
        Serial.println("[app] trmnl app show menu");
        display_.render_trmnl_menu();
        return;
    case TrmnlAppCommand::Refresh:
        Serial.println("[app] trmnl app refresh");
        render_trmnl(true);
        refresh_trmnl(true);
        return;
    case TrmnlAppCommand::NextScreen:
        Serial.println("[app] trmnl app next screen");
        if (!trmnl_manual_action_allowed("next screen")) {
            render_trmnl(true);
            return;
        }
        render_trmnl(true);
        refresh_trmnl(true, false, true);
        return;
    case TrmnlAppCommand::RefetchCurrent:
        Serial.println("[app] trmnl app refetch current");
        if (!trmnl_manual_action_allowed("refetch current")) {
            render_trmnl(true);
            return;
        }
        render_trmnl(true);
        refresh_trmnl(true, false, false, true);
        return;
    case TrmnlAppCommand::SpecialFunction:
        Serial.println("[app] trmnl app special function");
        render_trmnl(true);
        refresh_trmnl(true, true);
        return;
    case TrmnlAppCommand::ToggleLight:
        backlight_enabled_ = !backlight_enabled_;
        Serial.printf("[app] trmnl app light %s\n", backlight_enabled_ ? "on" : "off");
        board_.set_backlight_enabled(backlight_enabled_);
        render_trmnl(true);
        return;
    case TrmnlAppCommand::ReturnHome:
        Serial.println("[app] trmnl app return home");
        return_home_from_app();
        return;
    }
}

void App::handle_power_button()
{
    const bool pressed = board_.function_button_pressed();

    if (boot_release_guard_) {
        if (!pressed) {
            board_.clear_function_button_interrupt();
            Serial.println("[app] function button released; sleep trigger armed");
            boot_release_guard_ = false;
        }
        return;
    }

    if (!pressed) {
        boot_press_latched_ = false;
        boot_press_started_ms_ = 0;
        return;
    }

    const unsigned long now = millis();
    if (!boot_press_latched_) {
        mark_activity();
        boot_press_latched_ = true;
        boot_press_started_ms_ = now;
        Serial.println("[app] function button press detected");
        return;
    }

    if (now - boot_press_started_ms_ >= kBootSleepDebounceMs) {
        enter_sleep();
    }
}

void App::handle_touch()
{
    board_.poll_input();

    BoardInputEvent event;
    while (board_.next_input_event(event)) {
        if (event.type != BoardInputEventType::None) {
            mark_activity();
        }
        handle_touch_event(event);
    }
}

void App::handle_touch_event(const BoardInputEvent& event)
{
    switch (event.type) {
    case BoardInputEventType::None:
        return;
    case BoardInputEventType::TouchDown:
        touch_active_ = true;
        touch_consumed_ = false;
        touch_start_x_ = event.touch.x;
        touch_start_y_ = event.touch.y;
        touch_last_x_ = event.touch.x;
        touch_last_y_ = event.touch.y;
        Serial.printf("[app] touch start x=%d y=%d\n", event.touch.x, event.touch.y);
        return;
    case BoardInputEventType::TouchUp: {
        if (!touch_active_) {
            return;
        }

        const int16_t start_x = touch_start_x_;
        const int16_t start_y = touch_start_y_;
        const int16_t end_x = touch_last_x_;
        const int16_t end_y = touch_last_y_;
        const bool consumed = touch_consumed_;

        touch_active_ = false;
        touch_consumed_ = false;

        if (consumed) {
            return;
        }

        if (screen_ == Screen::Trmnl && trmnl_app_.state().home_menu_visible) {
            handle_trmnl_menu_touch(end_x, end_y);
            return;
        }

        if (screen_ == Screen::Trmnl && handle_trmnl_swipe(start_x, start_y, end_x, end_y)) {
            return;
        }

        if (screen_ == Screen::GenericApp && generic_app_ != nullptr) {
            AppEvent event;
            event.type = AppEventType::TouchUp;
            event.timestamp_ms = millis();
            event.touch.x = end_x;
            event.touch.y = end_y;
            handle_generic_app_result(dispatch_generic_app_event(event));
            return;
        }

        if (dropdown_visible_) {
            const DropdownAction action = display_.hit_test_dropdown_action(end_x, end_y);
            if (action != DropdownAction::None) {
                handle_dropdown_action(action);
                return;
            }
            if (start_y > display_.height() / 2) {
                hide_dropdown();
            }
            return;
        }

        if (screen_ == Screen::Settings && handle_settings_swipe(start_x, start_y, end_x, end_y)) {
            return;
        }

        if (screen_ == Screen::Home) {
            const HomeViewModel model = home_.view_model();
            const int app_index = display_.hit_test_home_app(model, end_x, end_y);
            if (app_index >= 0 && app_index < model.app_count) {
                open_app(model.apps[app_index].icon);
            }
        } else if (screen_ == Screen::Settings) {
            const SettingRowAction action = display_.hit_test_settings_action(settings_screen_.view_model(), end_x, end_y);
            handle_settings_action(action);
        }

        return;
    }
    case BoardInputEventType::TouchMove:
        if (!touch_active_) {
            return;
        }

        touch_last_x_ = event.touch.x;
        touch_last_y_ = event.touch.y;
        break;
    }

    if (screen_ == Screen::Trmnl || screen_ == Screen::GenericApp) {
        return;
    }

    if (touch_consumed_ || touch_start_y_ >= display_.status_bar_height()) {
        return;
    }

    const int dx = abs(event.touch.x - touch_start_x_);
    const int dy = event.touch.y - touch_start_y_;
    if (dy >= kDropdownSwipeMinDy && dx <= kDropdownSwipeMaxDx) {
        show_dropdown();
        touch_consumed_ = true;
    }
}

bool App::handle_settings_swipe(int16_t start_x, int16_t start_y, int16_t end_x, int16_t end_y)
{
    const int dx = end_x - start_x;
    const int dy = end_y - start_y;
    if (dx < kSettingsBackSwipeMinDx || abs(dy) > kSettingsBackSwipeMaxDy) {
        return false;
    }

    if (settings_screen_.page() == SettingsPage::Main) {
        Serial.println("[app] settings root swipe: return home");
        return_home_from_app();
        return true;
    }

    Serial.println("[app] settings swipe: back");
    settings_screen_.open_main_page();
    update_settings_screen();
    render_settings(true);
    return true;
}

bool App::handle_trmnl_swipe(int16_t start_x, int16_t start_y, int16_t end_x, int16_t end_y)
{
    if (!trmnl_app_.state().image_rendered || trmnl_app_.state().home_menu_visible) {
        return false;
    }

    TouchPoint raw_start;
    raw_start.x = start_x;
    raw_start.y = start_y;
    raw_start.pressed = true;
    TouchPoint raw_end;
    raw_end.x = end_x;
    raw_end.y = end_y;
    raw_end.pressed = true;
    const TouchPoint start = trmnl_touch_to_display(raw_start, display_.width(), display_.height());
    const TouchPoint end = trmnl_touch_to_display(raw_end, display_.width(), display_.height());

    const int dx = end.x - start.x;
    const int dy = end.y - start.y;
    if (abs(dx) >= kTrmnlBacklightSwipeMinDy && abs(dy) <= kTrmnlBacklightSwipeMaxDx) {
        if (dx > 0) {
            Serial.println("[app] trmnl right swipe: next screen");
            if (!trmnl_manual_action_allowed("next screen swipe")) {
                return true;
            }
            refresh_trmnl(true, false, true, false, false);
            return true;
        }
        return false;
    }

    if (abs(dy) < kTrmnlBacklightSwipeMinDy || abs(dx) > kTrmnlBacklightSwipeMaxDx) {
        return false;
    }

    const bool enable = dy < 0;
    if (backlight_enabled_ == enable) {
        Serial.printf("[app] trmnl backlight swipe ignored: already %s\n", enable ? "on" : "off");
        return true;
    }

    backlight_enabled_ = enable;
    Serial.printf("[app] trmnl vertical swipe %s; backlight %s\n", enable ? "up" : "down", enable ? "on" : "off");
    board_.set_backlight_enabled(backlight_enabled_);
    return true;
}

bool App::handle_trmnl_menu_touch(int16_t x, int16_t y)
{
    TouchPoint raw;
    raw.x = x;
    raw.y = y;
    raw.pressed = true;
    const TouchPoint mapped = trmnl_touch_to_display(raw, display_.width(), display_.height());
    const TrmnlMenuAction action = display_.hit_test_trmnl_menu_action(mapped.x, mapped.y);
    if (action == TrmnlMenuAction::None) {
        Serial.printf("[app] trmnl menu miss raw=%d,%d mapped=%d,%d\n", x, y, mapped.x, mapped.y);
        return false;
    }

    Serial.printf("[app] trmnl menu hit raw=%d,%d mapped=%d,%d action=%d\n",
                  x, y, mapped.x, mapped.y, static_cast<int>(action));
    AppEvent event;
    event.type = AppEventType::TouchUp;
    event.timestamp_ms = millis();
    event.touch.x = mapped.x;
    event.touch.y = mapped.y;
    event.value = static_cast<uint32_t>(trmnl_menu_action_to_app_action(action));
    handle_trmnl_app_result(dispatch_trmnl_event(event));
    return true;
}

void App::open_app(AppIcon icon)
{
    Serial.printf("[app] open app %s\n", app_icon_label(icon));
    if (icon == AppIcon::Settings) {
        open_settings();
        return;
    }
    if (icon == AppIcon::Trmnl) {
        open_trmnl();
        return;
    }
    if (icon == AppIcon::Reader) {
        open_generic_app(reader_app_);
        return;
    }

    close_current_app();
    current_app_ = new (scaffold_app_storage_) ScaffoldApp(icon);
    dropdown_visible_ = false;
    screen_ = Screen::App;
    render_current_app(true);
}

void App::open_settings()
{
    close_current_app();
    dropdown_visible_ = false;
    screen_ = Screen::Settings;
    settings_screen_.open_main_page();
    update_settings_screen();
    render_settings(false);
}

void App::open_trmnl()
{
    close_current_app();
    dropdown_visible_ = false;
    screen_ = Screen::Trmnl;
    last_trmnl_manual_action_ms_ = 0;
    AppPlatformContext trmnl_context(trmnl_app_.descriptor().id, storage_, wifi_, time_, battery_, power_);
    trmnl_app_.on_open(trmnl_context.context);
    trmnl_cache_.cleanup(trmnl_context.context);
    trmnl_screen_.set_exit_prompt(false);

    refresh_trmnl(true);
}

void App::open_generic_app(PaperApp& app)
{
    close_current_app();
    dropdown_visible_ = false;
    screen_ = Screen::GenericApp;
    generic_app_ = &app;

    AppPlatformContext app_context(app.descriptor().id, storage_, wifi_, time_, battery_, power_);
    app.on_open(app_context.context);

    render_generic_app(true);
}

void App::close_current_app()
{
    if (current_app_ == nullptr) {
        return;
    }

    current_app_->~ScaffoldApp();
    current_app_ = nullptr;
}

void App::close_generic_app()
{
    if (generic_app_ == nullptr) {
        return;
    }

    AppPlatformContext app_context(generic_app_->descriptor().id, storage_, wifi_, time_, battery_, power_);
    generic_app_->on_close(app_context.context);
    generic_app_ = nullptr;
}

void App::render_generic_app(bool full_refresh)
{
    if (generic_app_ == nullptr) {
        return;
    }

    AppPlatformContext app_context(generic_app_->descriptor().id, storage_, wifi_, time_, battery_, power_);
    DisplayRenderContext render_ctx = display_.begin_app_frame(full_refresh);
    const AppRenderResult result = generic_app_->render(app_context.context, render_ctx);
    display_.end_app_frame(full_refresh ? AppRefreshHint::Full : result.refresh_hint);
}

AppEventResult App::dispatch_generic_app_event(const AppEvent& event)
{
    AppPlatformContext app_context(generic_app_->descriptor().id, storage_, wifi_, time_, battery_, power_);
    return generic_app_->on_event(app_context.context, event);
}

void App::handle_generic_app_result(const AppEventResult& result)
{
    if (result.action.return_home) {
        return_home_from_app();
        return;
    }

    if (result.refresh_hint != AppRefreshHint::None) {
        render_generic_app(result.refresh_hint == AppRefreshHint::Full);
    }
}

void App::open_usb_drive()
{
    Serial.println("[app] usb drive: rebooting to register USB MSC before the USB stack starts");
    g_usb_drive_boot_request = kUsbDriveBootMagic;
    esp_restart();
}

void App::render_usb_drive()
{
    DisplayRenderContext ctx = display_.begin_app_frame(true);
    draw_text_20(ctx, "USB Drive", 16, 80, EPD_DRAW_ALIGN_LEFT);
    draw_text_12(ctx, "Connect a cable to your computer to access the SD card.", 16, 120, EPD_DRAW_ALIGN_LEFT);
    draw_text_12(ctx, "Tap anywhere to disconnect and restart.", 16, 148, EPD_DRAW_ALIGN_LEFT);
    display_.end_app_frame(AppRefreshHint::Full);
}

void App::run_usb_drive_boot()
{
    g_usb_drive_boot_request = 0;

    const BoardStatus board_status = board_.begin();
    (void)board_status;
    settings_.begin();
    storage_.begin(board_storage_);
    display_.begin(settings_.refresh_policy());

    Serial.begin(115200);
    delay(50);
    Serial.println("[app] usb drive boot mode");

    if (!usb_storage_.begin(board_storage_)) {
        Serial.println("[app] usb drive: SD card not mounted; staying on message screen");
    }

    render_usb_drive();

    while (true) {
        board_.poll_input();
        BoardInputEvent event;
        while (board_.next_input_event(event)) {
            if (event.type == BoardInputEventType::TouchUp) {
                Serial.println("[app] usb drive: tap to disconnect; restarting");
                esp_restart();
            }
        }
        delay(20);
    }
}

void App::return_home_from_app()
{
    if (screen_ == Screen::Trmnl) {
        AppPlatformContext trmnl_context(trmnl_app_.descriptor().id, storage_, wifi_, time_, battery_, power_);
        trmnl_app_.on_close(trmnl_context.context);
    }
    if (screen_ == Screen::GenericApp) {
        close_generic_app();
    }
    close_current_app();
    dropdown_visible_ = false;
    trmnl_screen_.set_exit_prompt(false);
    screen_ = Screen::Home;
    boot_release_guard_ = true;
    boot_press_latched_ = false;
    boot_press_started_ms_ = 0;
    render_home();
}

void App::show_dropdown()
{
    if (dropdown_visible_) {
        return;
    }

    Serial.println("[app] status bar swipe down; showing dropdown");
    dropdown_visible_ = true;
    display_.render_dropdown();
}

void App::hide_dropdown()
{
    if (!dropdown_visible_) {
        return;
    }

    Serial.println("[app] hiding dropdown");
    dropdown_visible_ = false;
    if (screen_ == Screen::Home) {
        render_home_content();
    } else if (screen_ == Screen::Settings) {
        render_settings(true);
    } else if (screen_ == Screen::Trmnl) {
        render_trmnl(true);
    } else {
        render_current_app(true);
    }
}

void App::handle_dropdown_action(DropdownAction action)
{
    switch (action) {
    case DropdownAction::None:
        return;
    case DropdownAction::ToggleLight:
        backlight_enabled_ = !backlight_enabled_;
        Serial.printf("[app] dropdown light %s\n", backlight_enabled_ ? "on" : "off");
        board_.set_backlight_enabled(backlight_enabled_);
        hide_dropdown();
        return;
    }
}

void App::enter_sleep()
{
    Serial.println("[app] lock begin");

    backlight_enabled_ = false;
    board_.set_backlight_enabled(false);
    battery_.update(time_.now_epoch());

    LockScreenViewModel lock_screen;
    lock_screen.message = "Sleeping";
    if (time_.format_local_clock(time_.now_epoch(), status_bar_time_, sizeof(status_bar_time_))) {
        lock_screen.time = status_bar_time_;
    } else {
        lock_screen.time = "--:--";
    }
    battery_.format_percentage(status_bar_battery_, sizeof(status_bar_battery_));
    lock_screen.battery = status_bar_battery_;
    lock_screen.wake_hint = "Press to wake up";
    display_.render_lock_screen(lock_screen);

    Serial.println("[app] waiting for function button release before locking");
    while (board_.function_button_pressed()) {
        delay(20);
    }
    board_.clear_function_button_interrupt();
    Serial.println("[app] function button released; locked idle active");

    locked_ = true;
    locked_release_guard_ = true;
    locked_started_ms_ = millis();
    boot_release_guard_ = true;
    boot_press_latched_ = false;
    boot_press_started_ms_ = 0;
}

void App::handle_locked_state()
{
    const unsigned long now = millis();
    if (now - locked_started_ms_ >= kLockedDeepSleepTimeoutMs) {
        Serial.println("[app] locked timeout; escalating to deep sleep");
        power_.enter_deep_sleep();
    }

    const bool pressed = board_.function_button_pressed();
    if (locked_release_guard_) {
        if (!pressed) {
            locked_release_guard_ = false;
        }
        return;
    }

    if (!pressed) {
        return;
    }

    Serial.println("[app] function button unlock detected");
    while (board_.function_button_pressed()) {
        delay(20);
    }
    board_.clear_function_button_interrupt();

    Serial.println("[app] returning from locked idle to home");
    locked_ = false;
    locked_release_guard_ = false;
    last_activity_ms_ = millis();
    boot_release_guard_ = true;
    boot_press_latched_ = false;
    boot_press_started_ms_ = 0;
    if (screen_ == Screen::Home) {
        render_home();
    } else if (screen_ == Screen::Settings) {
        render_settings(false);
    } else if (screen_ == Screen::Trmnl) {
        render_trmnl(false);
    } else if (screen_ == Screen::GenericApp) {
        render_generic_app(true);
    } else {
        render_current_app(false);
    }
}

void App::mark_activity()
{
    last_activity_ms_ = millis();
}

void App::check_idle_sleep_timeout()
{
    if (locked_) {
        return;
    }
    if (screen_ == Screen::Trmnl) {
        return;
    }

    const unsigned long now = millis();
    if (now - last_activity_ms_ < kIdleSleepTimeoutMs) {
        return;
    }

    Serial.println("[app] idle timeout; entering locked idle");
    enter_sleep();
}

void App::handle_settings_action(SettingRowAction action)
{
    switch (action) {
    case SettingRowAction::None:
        return;
    case SettingRowAction::OpenTimeSettings:
        settings_screen_.open_page(SettingsPage::Time);
        break;
    case SettingRowAction::OpenWifiSettings:
        settings_screen_.open_page(SettingsPage::Wifi);
        break;
    case SettingRowAction::OpenTrmnlSettings:
        settings_screen_.open_page(SettingsPage::Trmnl);
        break;
    case SettingRowAction::BackToSettings:
        settings_screen_.open_main_page();
        break;
    case SettingRowAction::ToggleWifiEnabled:
        settings_.set_wifi_enabled(!settings_.wifi_enabled());
        wifi_.set_enabled(settings_.wifi_enabled());
        break;
    case SettingRowAction::TestWifiConnection: {
        const bool connected = wifi_.connect();
        bool internet_access = false;
        if (connected) {
            internet_access = wifi_.confirm_internet_access();
        }

        WifiStatus test_status = wifi_.status();
        test_status.internet_access = internet_access;
        if (wifi_.settings().disconnect_after_network_task) {
            wifi_.disconnect();
        }
        settings_screen_.set_state(settings_, wifi_.settings(), test_status, time_);
        render_settings(true);
        return;
    }
    case SettingRowAction::SelectTimezone:
        time_.select_next_timezone();
        home_.set_time_status(time_.status());
        break;
    case SettingRowAction::SyncRtcTime:
        settings_screen_.set_state(settings_, wifi_.settings(), wifi_.status(), time_);
        render_settings(true);
        time_.sync_now();
        home_.set_time_status(time_.status());
        settings_screen_.set_state(settings_, wifi_.settings(), wifi_.status(), time_);
        render_settings(true);
        return;
    case SettingRowAction::ToggleTrmnlEnabled:
        settings_.set_trmnl_enabled(!settings_.trmnl_enabled());
        break;
    case SettingRowAction::ToggleTrmnlMode:
        settings_.set_trmnl_mode(settings_.trmnl_mode() == TrmnlMode::Mirror ? TrmnlMode::Playlist : TrmnlMode::Mirror);
        break;
    case SettingRowAction::ToggleTrmnlAutonomous:
        settings_.set_trmnl_autonomous_enabled(!settings_.trmnl_autonomous_enabled());
        break;
    case SettingRowAction::ClearTrmnlCache: {
        AppPlatformContext trmnl_context(trmnl_app_.descriptor().id, storage_, wifi_, time_, battery_, power_);
        trmnl_cache_.clear(trmnl_context.context);
        clear_trmnl_cached_fallback();
        trmnl_app_.mark_image_result(false, 0);
        break;
    }
    case SettingRowAction::OpenUsbDrive:
        open_usb_drive();
        return;
    }

    update_settings_screen();
    render_settings(true);
}

void App::update_settings_screen()
{
    home_.set_time_status(time_.status());
    settings_screen_.set_state(settings_, wifi_.settings(), wifi_.status(), time_);
}

void App::refresh_trmnl(bool full_refresh,
                        bool special_function,
                        bool advance_playlist,
                        bool refetch_current,
                        bool show_action_label)
{
    const bool had_rendered_image = trmnl_app_.state().image_rendered;
    if (had_rendered_image) {
        const char* overlay = "UPDATING";
        if (special_function) {
            overlay = "SPECIAL";
        } else if (advance_playlist) {
            overlay = "NEXT";
        } else if (refetch_current) {
            overlay = "REFETCH";
        }
        if (show_action_label) {
            display_.render_trmnl_overlay(overlay);
        } else {
            char battery_text[sizeof(status_bar_battery_)] = {};
            battery_.format_percentage(battery_text, sizeof(battery_text));
            display_.render_trmnl_activity_overlay(TrmnlFetchStatus::RequestingMetadata, battery_text);
        }
    } else {
        TrmnlSnapshot loading;
        loading.status = TrmnlFetchStatus::RequestingMetadata;
        trmnl_screen_.set_snapshot(loading, nullptr, 0);
        render_trmnl(full_refresh);
    }

    TrmnlSettings trmnl_settings = current_trmnl_settings();
    AppPlatformContext trmnl_context(trmnl_app_.descriptor().id, storage_, wifi_, time_, battery_, power_);
    TrmnlSnapshot cached_metadata[kTrmnlPreviousImageCapacity] = {};
    const size_t cached_metadata_count = trmnl_cache_.load_recent_metadata(trmnl_context.context,
                                                                           cached_metadata,
                                                                           kTrmnlPreviousImageCapacity);
    const bool cached_metadata_available = cached_metadata_count > 0
        && (cached_metadata[0].image_url_hash != 0
            || cached_metadata[0].image_visual_hash != 0
            || cached_metadata[0].response.image_name[0] != '\0'
            || cached_metadata[0].response.metadata_etag[0] != '\0'
            || cached_metadata[0].response.image_etag[0] != '\0');
    if (cached_metadata_available) {
        trmnl_settings.previous_image_url_hash = cached_metadata[0].image_url_hash;
        trmnl_settings.previous_image_name = cached_metadata[0].response.image_name;
        trmnl_settings.previous_metadata_etag = cached_metadata[0].response.metadata_etag;
        trmnl_settings.previous_image_etag = cached_metadata[0].response.image_etag;
        trmnl_settings.previous_image_count = cached_metadata_count;
        for (size_t i = 0; i < cached_metadata_count && i < kTrmnlPreviousImageCapacity; ++i) {
            trmnl_settings.previous_image_url_hashes[i] = cached_metadata[i].image_url_hash;
            trmnl_settings.previous_image_names[i] = cached_metadata[i].response.image_name;
            trmnl_settings.previous_metadata_etags[i] = cached_metadata[i].response.metadata_etag;
            trmnl_settings.previous_image_etags[i] = cached_metadata[i].response.image_etag;
        }
        trmnl_settings.cached_image_available = true;
        trmnl_settings.skip_unchanged_image = true;
    }
    if (special_function) {
        trmnl_settings.special_function = true;
        trmnl_settings.mode = TrmnlMode::Playlist;
        trmnl_settings.skip_unchanged_image = false;
    }
    if (advance_playlist) {
        trmnl_settings.mode = TrmnlMode::Playlist;
        trmnl_settings.force_playlist_advance = true;
    }
    if (refetch_current) {
        trmnl_settings.mode = TrmnlMode::Mirror;
        trmnl_settings.force_current_screen = true;
    }
    const TrmnlSnapshot& snapshot = trmnl_.fetch(trmnl_settings, wifi_);

    if (snapshot.response.reset_firmware) {
        Serial.println("[app] trmnl server requested firmware reset; restarting");
        esp_restart();
        return;
    }

    if (snapshot.response.special_fn[0] != '\0') {
        if (std::strcmp(snapshot.response.special_fn, "sleep") == 0) {
            Serial.println("[app] trmnl special: sleep — skipping render");
            const uint32_t secs = snapshot.response.refresh_seconds > 0
                ? snapshot.response.refresh_seconds
                : trmnl_settings.fallback_refresh_seconds;
            enter_trmnl_autonomous_sleep(secs);
            return;
        }
        if (std::strcmp(snapshot.response.special_fn, "identify") == 0) {
            Serial.println("[app] trmnl special: identify — flashing display");
            display_.identify_flash();
            // Continue to normal render after the flash.
        }
    }

    const bool live_image_ready = snapshot.status == TrmnlFetchStatus::Ready
        && trmnl_.image_data() != nullptr
        && trmnl_.image_size() > 0;
    bool cached_image_ready = false;
    uint32_t cached_refresh_seconds = trmnl_settings.fallback_refresh_seconds;
    bool live_visual_duplicate = false;
    if (live_image_ready && cached_metadata_available && snapshot.image_visual_hash != 0) {
        for (size_t i = 0; i < cached_metadata_count; ++i) {
            if (cached_metadata[i].image_visual_hash != 0
                && snapshot.image_visual_hash == cached_metadata[i].image_visual_hash) {
                live_visual_duplicate = true;
                break;
            }
        }
    }

    if (live_visual_duplicate) {
        Serial.printf("[app] trmnl visual duplicate; keeping cached image visual_hash=%lu bytes=%lu\n",
                      static_cast<unsigned long>(snapshot.image_visual_hash),
                      static_cast<unsigned long>(trmnl_.image_size()));
        TrmnlSnapshot cached_snapshot;
        uint8_t* cached_image = nullptr;
        size_t cached_size = 0;
        if (trmnl_cache_.load_matching(trmnl_context.context, snapshot, &cached_snapshot, &cached_image, &cached_size)
            || trmnl_cache_.load_last_good(trmnl_context.context, &cached_snapshot, &cached_image, &cached_size)) {
            clear_trmnl_cached_fallback();
            trmnl_cached_fallback_image_ = cached_image;
            trmnl_cached_fallback_size_ = cached_size;
            trmnl_screen_.set_snapshot(cached_snapshot, trmnl_cached_fallback_image_, trmnl_cached_fallback_size_);
            render_trmnl(full_refresh);
            trmnl_app_.mark_image_result(true, cached_snapshot.image_url_hash);
            cached_image_ready = true;
            cached_refresh_seconds = snapshot.response.refresh_seconds > 0
                ? snapshot.response.refresh_seconds
                : cached_snapshot.response.refresh_seconds;
        }
    } else if (live_image_ready) {
        clear_trmnl_cached_fallback();
        trmnl_screen_.set_snapshot(snapshot, trmnl_.image_data(), trmnl_.image_size());
        render_trmnl(full_refresh);
        trmnl_app_.mark_image_result(true, snapshot.image_url_hash);
    } else {
        TrmnlSnapshot cached_snapshot;
        uint8_t* cached_image = nullptr;
        size_t cached_size = 0;
        if (trmnl_cache_.load_matching(trmnl_context.context, snapshot, &cached_snapshot, &cached_image, &cached_size)
            || trmnl_cache_.load_last_good(trmnl_context.context, &cached_snapshot, &cached_image, &cached_size)) {
            clear_trmnl_cached_fallback();
            trmnl_cached_fallback_image_ = cached_image;
            trmnl_cached_fallback_size_ = cached_size;
            if (snapshot.status == TrmnlFetchStatus::Ready) {
                Serial.printf("[app] trmnl metadata unchanged; rendering cached image bytes=%lu hash=%lu\n",
                              static_cast<unsigned long>(cached_size),
                              static_cast<unsigned long>(cached_snapshot.image_url_hash));
            } else {
                Serial.printf("[app] trmnl live fetch failed status=%s; rendering cached image bytes=%lu\n",
                              trmnl_status_label(snapshot.status),
                              static_cast<unsigned long>(cached_size));
            }
            trmnl_screen_.set_snapshot(cached_snapshot, trmnl_cached_fallback_image_, trmnl_cached_fallback_size_);
            render_trmnl(full_refresh);
            trmnl_app_.mark_image_result(true, cached_snapshot.image_url_hash);
            cached_image_ready = true;
            cached_refresh_seconds = snapshot.status == TrmnlFetchStatus::Ready && snapshot.response.refresh_seconds > 0
                ? snapshot.response.refresh_seconds
                : cached_snapshot.response.refresh_seconds > 0
                ? cached_snapshot.response.refresh_seconds
                : trmnl_settings.fallback_refresh_seconds;
        } else {
            trmnl_screen_.set_snapshot(snapshot, trmnl_.image_data(), trmnl_.image_size());
            render_trmnl(full_refresh);
            trmnl_app_.mark_image_result(false, 0);
        }
    }

    if (live_image_ready && snapshot.image_downloaded) {
        trmnl_cache_.save_last_good(trmnl_context.context, snapshot, trmnl_.image_data(), trmnl_.image_size());
    }

    if (snapshot.status == TrmnlFetchStatus::Ready) {
        Serial.printf("[app] trmnl using response refresh=%lu sec for next image refresh\n",
                      static_cast<unsigned long>(snapshot.response.refresh_seconds));
        schedule_trmnl_refresh(snapshot.response.refresh_seconds);
    } else if (cached_image_ready) {
        Serial.printf("[app] trmnl using cached/fallback refresh=%lu sec after failed live fetch\n",
                      static_cast<unsigned long>(cached_refresh_seconds));
        schedule_trmnl_refresh(cached_refresh_seconds);
    } else {
        clear_trmnl_refresh_schedule();
    }

    Serial.println("[app] trmnl battery update begin");
    const bool battery_changed = battery_.update(time_.now_epoch());
    Serial.printf("[app] trmnl battery update done changed=%s\n", battery_changed ? "yes" : "no");
    const BatteryStatus battery_status = battery_.status();
    Serial.printf("[app] trmnl desk check enabled=%s autonomous=%s battery_valid=%s charging=%s full=%s\n",
                  settings_.trmnl_enabled() ? "yes" : "no",
                  settings_.trmnl_autonomous_enabled() ? "yes" : "no",
                  battery_status.valid ? "yes" : "no",
                  battery_status.charging ? "yes" : "no",
                  battery_status.full ? "yes" : "no");
    if (trmnl_desk_mode_active() && screen_ == Screen::Trmnl) {
        uint32_t sleep_seconds = trmnl_settings.fallback_refresh_seconds;
        if (snapshot.status == TrmnlFetchStatus::Ready && snapshot.response.refresh_seconds > 0) {
            sleep_seconds = snapshot.response.refresh_seconds;
        } else if (cached_image_ready) {
            sleep_seconds = cached_refresh_seconds;
        }
        enter_trmnl_autonomous_sleep(sleep_seconds);
    }
}

void App::check_trmnl_refresh_schedule()
{
    if (screen_ != Screen::Trmnl) {
        return;
    }

    AppEvent event;
    event.type = AppEventType::BackgroundRefresh;
    event.timestamp_ms = millis();
    const uint32_t scheduled_seconds = trmnl_app_.state().next_refresh_seconds;
    const AppEventResult result = dispatch_trmnl_event(event);
    if (!result.handled) {
        return;
    }

    Serial.printf("[app] trmnl scheduled refresh due after %lu sec\n",
                  static_cast<unsigned long>(scheduled_seconds));
    handle_trmnl_app_result(result);
}

void App::schedule_trmnl_refresh(uint32_t refresh_seconds)
{
    AppEvent event;
    event.type = AppEventType::ScheduledWake;
    event.timestamp_ms = millis();
    event.value = refresh_seconds;
    dispatch_trmnl_event(event);
    if (refresh_seconds == 0) {
        return;
    }
    Serial.printf("[app] trmnl next refresh in %lu sec\n",
                  static_cast<unsigned long>(refresh_seconds));
}

void App::clear_trmnl_refresh_schedule()
{
    AppEvent event;
    event.type = AppEventType::ScheduledWake;
    event.timestamp_ms = millis();
    event.value = 0;
    dispatch_trmnl_event(event);
}

void App::enter_trmnl_autonomous_sleep(uint32_t refresh_seconds)
{
    backlight_enabled_ = false;
    board_.set_backlight_enabled(false);
    wifi_.disconnect();
    clear_trmnl_refresh_schedule();

    Serial.printf("[app] trmnl autonomous sleep for %lu sec\n",
                  static_cast<unsigned long>(refresh_seconds));
    power_.enter_deep_sleep_for(refresh_seconds);
}

bool App::trmnl_desk_mode_active() const
{
    const BatteryStatus battery_status = battery_.status();
    const bool charger_present = battery_status.valid && (battery_status.charging || battery_status.full);
    return settings_.trmnl_enabled()
        && settings_.trmnl_autonomous_enabled()
        && charger_present;
}

bool App::trmnl_manual_action_allowed(const char* label)
{
    const unsigned long now = millis();
    if (last_trmnl_manual_action_ms_ != 0
        && now - last_trmnl_manual_action_ms_ < kTrmnlManualActionCooldownMs) {
        Serial.printf("[app] trmnl manual action ignored: %s cooldown %lu sec remaining\n",
                      label != nullptr ? label : "action",
                      static_cast<unsigned long>((kTrmnlManualActionCooldownMs - (now - last_trmnl_manual_action_ms_) + 999) / 1000));
        return false;
    }

    last_trmnl_manual_action_ms_ = now;
    return true;
}

void App::clear_trmnl_cached_fallback()
{
    if (trmnl_cached_fallback_image_ != nullptr) {
        trmnl_cache_.release_image(trmnl_cached_fallback_image_);
        trmnl_cached_fallback_image_ = nullptr;
    }
    trmnl_cached_fallback_size_ = 0;
}

void App::generate_wallpaper()
{
    const int wallpaper_width = display_.width();
    const int wallpaper_height = display_.height() - display_.status_bar_height();
    const uint32_t seed = static_cast<uint32_t>(millis()) ^ static_cast<uint32_t>(micros()) ^ 0x9E3779B9u;
    // if (wallpaper_.generate(wallpaper_width, wallpaper_height, seed)) {
    //     home_.set_wallpaper(wallpaper_.data(), wallpaper_.width(), wallpaper_.height());
    // }
}

void App::render_home()
{
    screen_ = Screen::Home;
    dropdown_visible_ = false;
    home_.set_time_status(time_.status());
    HomeViewModel model = home_.view_model();
    apply_status_bar_time(model.status_bar);
    apply_status_bar_battery(model.status_bar);
    std::strncpy(rendered_status_bar_time_, model.status_bar.time, sizeof(rendered_status_bar_time_) - 1);
    rendered_status_bar_time_[sizeof(rendered_status_bar_time_) - 1] = '\0';
    std::strncpy(rendered_status_bar_battery_, model.status_bar.battery, sizeof(rendered_status_bar_battery_) - 1);
    rendered_status_bar_battery_[sizeof(rendered_status_bar_battery_) - 1] = '\0';
    display_.full_refresh();
    display_.render(model);
}

void App::render_home_content()
{
    screen_ = Screen::Home;
    home_.set_time_status(time_.status());
    HomeViewModel model = home_.view_model();
    apply_status_bar_time(model.status_bar);
    apply_status_bar_battery(model.status_bar);
    display_.render_content(model);
}

void App::render_current_app(bool preserve_status_bar)
{
    if (current_app_ == nullptr) {
        render_home();
        return;
    }

    AppScreenViewModel model = current_app_->view_model();
    apply_status_bar_time(model.status_bar);
    apply_status_bar_battery(model.status_bar);
    if (preserve_status_bar) {
        display_.render_content(model);
    } else {
        std::strncpy(rendered_status_bar_time_, model.status_bar.time, sizeof(rendered_status_bar_time_) - 1);
        rendered_status_bar_time_[sizeof(rendered_status_bar_time_) - 1] = '\0';
        std::strncpy(rendered_status_bar_battery_, model.status_bar.battery, sizeof(rendered_status_bar_battery_) - 1);
        rendered_status_bar_battery_[sizeof(rendered_status_bar_battery_) - 1] = '\0';
        display_.full_refresh();
        display_.render(model);
    }
}

void App::render_settings(bool preserve_status_bar)
{
    screen_ = Screen::Settings;
    SettingsViewModel model = settings_screen_.view_model();
    apply_status_bar_time(model.status_bar);
    apply_status_bar_battery(model.status_bar);
    if (preserve_status_bar) {
        display_.render_content(model);
    } else {
        std::strncpy(rendered_status_bar_time_, model.status_bar.time, sizeof(rendered_status_bar_time_) - 1);
        rendered_status_bar_time_[sizeof(rendered_status_bar_time_) - 1] = '\0';
        std::strncpy(rendered_status_bar_battery_, model.status_bar.battery, sizeof(rendered_status_bar_battery_) - 1);
        rendered_status_bar_battery_[sizeof(rendered_status_bar_battery_) - 1] = '\0';
        display_.render_status_bar_only(model.status_bar);
        display_.render_content(model);
    }
}

void App::render_trmnl(bool preserve_status_bar)
{
    screen_ = Screen::Trmnl;
    TrmnlViewModel model = trmnl_screen_.view_model();
    apply_status_bar_time(model.status_bar);
    apply_status_bar_battery(model.status_bar);
    if (preserve_status_bar) {
        display_.render_content(model);
    } else {
        display_.render(model);
    }
}

void App::apply_status_bar_time(StatusBarViewModel& status_bar)
{
    const int64_t now = time_.now_epoch();
    if (time_.format_local_clock(now, status_bar_time_, sizeof(status_bar_time_))) {
        status_bar.time = status_bar_time_;
        return;
    }

    status_bar.time = "--:--";
}

void App::apply_status_bar_battery(StatusBarViewModel& status_bar)
{
    battery_.format_percentage(status_bar_battery_, sizeof(status_bar_battery_));
    status_bar.battery = status_bar_battery_;
}

void App::refresh_status_clock_if_needed()
{
    if (locked_ || dropdown_visible_ || screen_ == Screen::Trmnl || screen_ == Screen::GenericApp) {
        return;
    }

    const unsigned long now_ms = millis();
    if (now_ms - last_status_clock_check_ms_ < kStatusClockCheckMs) {
        return;
    }
    last_status_clock_check_ms_ = now_ms;

    StatusBarViewModel status_bar = current_status_bar_model();
    apply_status_bar_time(status_bar);
    const bool minute_changed = std::strcmp(status_bar.time, rendered_status_bar_time_) != 0;
    bool battery_changed = false;

    if (minute_changed) {
        battery_changed = battery_.update(time_.now_epoch());
    } else if (std::strcmp(status_bar.time, "--:--") == 0
               && now_ms - last_battery_fallback_update_ms_ >= kBatteryFallbackUpdateMs) {
        last_battery_fallback_update_ms_ = now_ms;
        battery_changed = battery_.update(time_.now_epoch());
    }

    apply_status_bar_battery(status_bar);
    const bool battery_text_changed = std::strcmp(status_bar.battery, rendered_status_bar_battery_) != 0;
    if (!minute_changed && !battery_changed && !battery_text_changed) {
        return;
    }

    std::strncpy(rendered_status_bar_time_, status_bar.time, sizeof(rendered_status_bar_time_) - 1);
    rendered_status_bar_time_[sizeof(rendered_status_bar_time_) - 1] = '\0';
    std::strncpy(rendered_status_bar_battery_, status_bar.battery, sizeof(rendered_status_bar_battery_) - 1);
    rendered_status_bar_battery_[sizeof(rendered_status_bar_battery_) - 1] = '\0';
    display_.render_status_bar_only(status_bar);
}

StatusBarViewModel App::current_status_bar_model()
{
    switch (screen_) {
    case Screen::Home: {
        home_.set_time_status(time_.status());
        return home_.view_model().status_bar;
    }
    case Screen::App:
        if (current_app_ != nullptr) {
            return current_app_->view_model().status_bar;
        }
        return home_.view_model().status_bar;
    case Screen::Settings:
        return settings_screen_.view_model().status_bar;
    case Screen::Trmnl:
        return trmnl_screen_.view_model().status_bar;
    case Screen::GenericApp:
        return {};
    }

    return {};
}

TrmnlSettings App::current_trmnl_settings() const
{
    return make_trmnl_settings(settings_,
                               wifi_,
                               battery_,
                               trmnl_app_.state(),
                               static_cast<uint16_t>(display_.width()),
                               static_cast<uint16_t>(display_.height()),
                               screen_ != Screen::Trmnl && wifi_.settings().disconnect_after_network_task);
}

}  // namespace paper_screen
