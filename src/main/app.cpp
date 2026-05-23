#include "app.h"

#include <Arduino.h>
#include <new>

namespace paper_screen {

namespace {

constexpr unsigned long kBootSleepDebounceMs = 80;
constexpr unsigned long kLockedDeepSleepTimeoutMs = 10UL * 60UL * 1000UL;
constexpr int kDropdownSwipeMinDy = 72;
constexpr int kDropdownSwipeMaxDx = 180;

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

    Serial.println("[app] board begin");
    const BoardStatus board_status = board_.begin();
    Serial.println("[app] board begin done");

    Serial.println("[app] settings begin");
    settings_.begin();
    Serial.println("[app] settings begin done");

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
        delay(100);
        return;
    }

    handle_power_button();
    handle_touch();
    handle_home_button();

    delay(100);
}

void App::handle_home_button()
{
    if (!board_.consume_home_button_pressed() || screen_ == Screen::Home) {
        return;
    }

    Serial.println("[app] gt911 home button returning home");
    return_home_from_app();
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
    const TouchPoint point = board_.touch_point();
    if (!point.pressed) {
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

        if (dropdown_visible_) {
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
        }

        return;
    }

    if (!touch_active_) {
        touch_active_ = true;
        touch_consumed_ = false;
        touch_start_x_ = point.x;
        touch_start_y_ = point.y;
        touch_last_x_ = point.x;
        touch_last_y_ = point.y;
        Serial.printf("[app] touch start x=%d y=%d\n", point.x, point.y);
        return;
    }

    touch_last_x_ = point.x;
    touch_last_y_ = point.y;

    if (touch_consumed_ || touch_start_y_ >= display_.status_bar_height()) {
        return;
    }

    const int dx = abs(point.x - touch_start_x_);
    const int dy = point.y - touch_start_y_;
    if (dy >= kDropdownSwipeMinDy && dx <= kDropdownSwipeMaxDx) {
        show_dropdown();
        touch_consumed_ = true;
    }
}

void App::open_app(AppIcon icon)
{
    Serial.printf("[app] open app %s\n", app_icon_label(icon));
    close_current_app();
    current_app_ = new (scaffold_app_storage_) ScaffoldApp(icon);
    dropdown_visible_ = false;
    screen_ = Screen::App;
    render_current_app(true);
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
    screen_ = Screen::Home;
    boot_release_guard_ = true;
    boot_press_latched_ = false;
    boot_press_started_ms_ = 0;
    render_home_content();
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
    } else {
        render_current_app(true);
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
    } else {
        render_current_app(false);
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

}  // namespace paper_screen
