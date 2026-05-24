#include "app.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <new>

namespace paper_screen {

namespace {

constexpr unsigned long kBootSleepDebounceMs = 80;
constexpr unsigned long kLockedDeepSleepTimeoutMs = 10UL * 60UL * 1000UL;
constexpr unsigned long kTrmnlHomeReleaseTimeoutMs = 140;
constexpr unsigned long kTrmnlHomeDoubleTapWindowMs = 1000;
constexpr unsigned long kLoopDelayMs = 20;
constexpr int kDropdownSwipeMinDy = 72;
constexpr int kDropdownSwipeMaxDx = 180;
constexpr int kTrmnlBacklightSwipeMinDy = 86;
constexpr int kTrmnlBacklightSwipeMaxDx = 220;

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

}  // namespace

App::~App()
{
    close_current_app();
}

void App::setup()
{
    Serial.begin(115200);
    delay(50);

    Serial.println();
    Serial.println("[app] setup begin");
    Serial.printf("[app] wifi mac=%s\n", WiFi.macAddress().c_str());

    Serial.println("[app] board begin");
    const BoardStatus board_status = board_.begin();
    Serial.println("[app] board begin done");

    Serial.println("[app] settings begin");
    settings_.begin();
    Serial.println("[app] settings begin done");

    Serial.println("[app] wifi begin");
    wifi_.set_enabled(settings_.wifi_enabled());
    wifi_.begin();
    Serial.println("[app] wifi begin done");

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
    Serial.println("[app] home state update done");

    Serial.println("[app] setup done");
}

void App::loop()
{
    if (!first_render_done_) {
        Serial.println("[app] first render begin");
        render_home();
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

    delay(kLoopDelayMs);
}

void App::handle_home_button()
{
    if (!board_.consume_home_button_pressed() || screen_ == Screen::Home) {
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
    const unsigned long now = millis();
    if (trmnl_home_menu_visible_) {
        Serial.println("[app] trmnl home closes menu");
        trmnl_home_menu_visible_ = false;
        trmnl_home_press_active_ = false;
        trmnl_home_tap_pending_ = false;
        trmnl_home_ignore_until_release_ = true;
        trmnl_home_last_seen_ms_ = now;
        render_trmnl(true);
        return;
    }

    if (trmnl_home_ignore_until_release_) {
        trmnl_home_last_seen_ms_ = now;
        return;
    }

    if (!trmnl_home_press_active_) {
        if (trmnl_home_tap_pending_) {
            Serial.println("[app] trmnl double home tap: show menu");
            trmnl_home_tap_pending_ = false;
            trmnl_home_tap_due_ms_ = 0;
            trmnl_home_menu_visible_ = true;
            trmnl_home_ignore_until_release_ = true;
            trmnl_home_last_seen_ms_ = now;
            display_.render_trmnl_menu();
            return;
        }

        Serial.println("[app] trmnl home press begin");
        trmnl_home_press_active_ = true;
    }
    trmnl_home_last_seen_ms_ = now;
}

void App::handle_trmnl_home_press_state()
{
    if (screen_ != Screen::Trmnl) {
        return;
    }

    const unsigned long now = millis();

    if (trmnl_home_ignore_until_release_) {
        if (now - trmnl_home_last_seen_ms_ >= kTrmnlHomeReleaseTimeoutMs) {
            trmnl_home_ignore_until_release_ = false;
            trmnl_home_last_seen_ms_ = 0;
        }
        return;
    }

    if (trmnl_home_press_active_ && now - trmnl_home_last_seen_ms_ < kTrmnlHomeReleaseTimeoutMs) {
        return;
    }

    if (trmnl_home_press_active_) {
        trmnl_home_press_active_ = false;
        trmnl_home_last_seen_ms_ = 0;

        Serial.println("[app] trmnl home tap: refresh pending");
        trmnl_home_tap_pending_ = true;
        trmnl_home_tap_due_ms_ = now + kTrmnlHomeDoubleTapWindowMs;
        return;
    }

    if (!trmnl_home_tap_pending_ || static_cast<long>(now - trmnl_home_tap_due_ms_) < 0) {
        return;
    }

    Serial.println("[app] trmnl single home tap: refresh");
    trmnl_home_tap_pending_ = false;
    trmnl_home_tap_due_ms_ = 0;
    refresh_trmnl(true);
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

        if (screen_ == Screen::Trmnl && trmnl_home_menu_visible_) {
            handle_trmnl_menu_touch(end_x, end_y);
            return;
        }

        if (screen_ == Screen::Trmnl && handle_trmnl_swipe(start_x, start_y, end_x, end_y)) {
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

    if (screen_ == Screen::Trmnl) {
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

bool App::handle_trmnl_swipe(int16_t start_x, int16_t start_y, int16_t end_x, int16_t end_y)
{
    if (!trmnl_image_rendered_ || trmnl_home_menu_visible_) {
        return false;
    }

    const int raw_dx = end_x - start_x;
    const int raw_dy = end_y - start_y;
    if (abs(raw_dx) < kTrmnlBacklightSwipeMinDy || abs(raw_dy) > kTrmnlBacklightSwipeMaxDx) {
        return false;
    }

    const bool enable = raw_dx > 0;
    if (backlight_enabled_ == enable) {
        Serial.printf("[app] trmnl backlight swipe ignored: already %s\n", enable ? "on" : "off");
        return true;
    }

    backlight_enabled_ = enable;
    Serial.printf("[app] trmnl side swipe %s; backlight %s\n", enable ? "up" : "down", enable ? "on" : "off");
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
    handle_trmnl_menu_action(action);
    return true;
}

void App::handle_trmnl_menu_action(TrmnlMenuAction action)
{
    trmnl_home_menu_visible_ = false;
    trmnl_home_press_active_ = false;
    trmnl_home_last_seen_ms_ = 0;
    trmnl_home_tap_pending_ = false;
    trmnl_home_tap_due_ms_ = 0;
    trmnl_home_ignore_until_release_ = false;

    switch (action) {
    case TrmnlMenuAction::None:
        return;
    case TrmnlMenuAction::Refresh:
        Serial.println("[app] trmnl menu refresh");
        render_trmnl(true);
        refresh_trmnl(true);
        return;
    case TrmnlMenuAction::ToggleLight:
        backlight_enabled_ = !backlight_enabled_;
        Serial.printf("[app] trmnl menu light %s\n", backlight_enabled_ ? "on" : "off");
        board_.set_backlight_enabled(backlight_enabled_);
        render_trmnl(true);
        return;
    case TrmnlMenuAction::ReturnHome:
        Serial.println("[app] trmnl menu return home");
        return_home_from_app();
        return;
    case TrmnlMenuAction::Cancel:
        Serial.println("[app] trmnl menu cancel");
        render_trmnl(true);
        return;
    }
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
    update_settings_screen();
    render_settings(false);
}

void App::open_trmnl()
{
    close_current_app();
    dropdown_visible_ = false;
    screen_ = Screen::Trmnl;
    trmnl_image_rendered_ = false;
    trmnl_last_image_url_[0] = '\0';
    trmnl_home_press_active_ = false;
    trmnl_home_menu_visible_ = false;
    trmnl_home_last_seen_ms_ = 0;
    trmnl_home_tap_pending_ = false;
    trmnl_home_tap_due_ms_ = 0;
    trmnl_home_ignore_until_release_ = false;
    trmnl_screen_.set_exit_prompt(false);

    refresh_trmnl(true);
}

void App::close_current_app()
{
    if (current_app_ == nullptr) {
        return;
    }

    current_app_->~ScaffoldApp();
    current_app_ = nullptr;
}

void App::return_home_from_app()
{
    close_current_app();
    dropdown_visible_ = false;
    trmnl_image_rendered_ = false;
    trmnl_last_image_url_[0] = '\0';
    trmnl_home_press_active_ = false;
    trmnl_home_menu_visible_ = false;
    trmnl_home_last_seen_ms_ = 0;
    trmnl_home_tap_pending_ = false;
    trmnl_home_tap_due_ms_ = 0;
    trmnl_home_ignore_until_release_ = false;
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

    LockScreenViewModel lock_screen;
    lock_screen.message = "Sleeping";
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
    boot_release_guard_ = true;
    boot_press_latched_ = false;
    boot_press_started_ms_ = 0;
    if (screen_ == Screen::Home) {
        render_home();
    } else if (screen_ == Screen::Settings) {
        render_settings(false);
    } else if (screen_ == Screen::Trmnl) {
        render_trmnl(false);
    } else {
        render_current_app(false);
    }
}

void App::handle_settings_action(SettingRowAction action)
{
    switch (action) {
    case SettingRowAction::None:
        return;
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
        settings_screen_.set_state(settings_, wifi_.settings(), test_status);
        render_settings(true);
        return;
    }
    case SettingRowAction::ToggleTrmnlEnabled:
        settings_.set_trmnl_enabled(!settings_.trmnl_enabled());
        break;
    case SettingRowAction::ToggleTrmnlMode:
        settings_.set_trmnl_mode(settings_.trmnl_mode() == TrmnlMode::Mirror ? TrmnlMode::Playlist : TrmnlMode::Mirror);
        break;
    }

    update_settings_screen();
    render_settings(true);
}

void App::update_settings_screen()
{
    settings_screen_.set_state(settings_, wifi_.settings(), wifi_.status());
}

void App::refresh_trmnl(bool full_refresh)
{
    const bool had_rendered_image = trmnl_image_rendered_;
    if (had_rendered_image) {
        display_.render_trmnl_overlay("UPDATING");
    } else {
        TrmnlSnapshot loading;
        loading.status = TrmnlFetchStatus::RequestingMetadata;
        trmnl_screen_.set_snapshot(loading, nullptr, 0);
        render_trmnl(full_refresh);
    }

    const TrmnlSnapshot snapshot = trmnl_.fetch(current_trmnl_settings(), wifi_);
    trmnl_screen_.set_snapshot(snapshot, trmnl_.image_data(), trmnl_.image_size());
    render_trmnl(full_refresh);
    trmnl_image_rendered_ = snapshot.status == TrmnlFetchStatus::Ready && trmnl_.image_data() != nullptr && trmnl_.image_size() > 0;
    if (trmnl_image_rendered_) {
        std::strncpy(trmnl_last_image_url_, snapshot.response.image_url, sizeof(trmnl_last_image_url_) - 1);
        trmnl_last_image_url_[sizeof(trmnl_last_image_url_) - 1] = '\0';
    } else {
        trmnl_last_image_url_[0] = '\0';
    }
}

void App::render_home()
{
    screen_ = Screen::Home;
    dropdown_visible_ = false;
    display_.full_refresh();
    display_.render(home_.view_model());
}

void App::render_home_content()
{
    screen_ = Screen::Home;
    display_.render_content(home_.view_model());
}

void App::render_current_app(bool preserve_status_bar)
{
    if (current_app_ == nullptr) {
        render_home();
        return;
    }

    if (preserve_status_bar) {
        display_.render_content(current_app_->view_model());
    } else {
        display_.full_refresh();
        display_.render(current_app_->view_model());
    }
}

void App::render_settings(bool preserve_status_bar)
{
    screen_ = Screen::Settings;
    if (preserve_status_bar) {
        display_.render_content(settings_screen_.view_model());
    } else {
        display_.full_refresh();
        display_.render(settings_screen_.view_model());
    }
}

void App::render_trmnl(bool preserve_status_bar)
{
    screen_ = Screen::Trmnl;
    if (preserve_status_bar) {
        display_.render_content(trmnl_screen_.view_model());
    } else {
        display_.render(trmnl_screen_.view_model());
    }
}

TrmnlSettings App::current_trmnl_settings() const
{
    TrmnlSettings trmnl_settings;
    trmnl_settings.enabled = settings_.trmnl_enabled();
    trmnl_settings.mode = settings_.trmnl_mode();
    trmnl_settings.api_key = settings_.trmnl_api_key();
    trmnl_settings.fallback_refresh_seconds = 1800;
    trmnl_settings.disconnect_wifi_after_fetch = screen_ != Screen::Trmnl && wifi_.settings().disconnect_after_network_task;
    trmnl_settings.allow_insecure_https = true;
    trmnl_settings.previous_image_url = trmnl_last_image_url_;
    trmnl_settings.skip_unchanged_image = trmnl_image_rendered_;
    return trmnl_settings;
}

}  // namespace paper_screen
