#pragma once

#include "board/board.h"
#include "display/display.h"
#include "services/power_service.h"
#include "services/settings_service.h"
#include "ui/home_screen.h"
#include "ui/lock_screen.h"
#include "ui/scaffold_app.h"

namespace paper_screen {

class App {
public:
    ~App();

    void setup();
    void loop();

private:
    enum class Screen {
        Home,
        App,
    };

    void handle_power_button();
    void handle_touch();
    void handle_home_button();
    void open_app(AppIcon icon);
    void close_current_app();
    void return_home_from_app();
    void show_dropdown();
    void hide_dropdown();
    void enter_sleep();
    void handle_locked_state();
    void render_home();
    void render_home_content();
    void render_current_app(bool preserve_status_bar);

    Board board_;
    Display display_;
    PowerService power_;
    SettingsService settings_;
    HomeScreen home_;
    bool first_render_done_ = false;
    bool boot_release_guard_ = true;
    bool boot_press_latched_ = false;
    unsigned long boot_press_started_ms_ = 0;
    bool locked_ = false;
    bool locked_release_guard_ = false;
    unsigned long locked_started_ms_ = 0;
    Screen screen_ = Screen::Home;
    alignas(ScaffoldApp) uint8_t scaffold_app_storage_[sizeof(ScaffoldApp)];
    ScaffoldApp* current_app_ = nullptr;
    bool touch_active_ = false;
    bool touch_consumed_ = false;
    bool dropdown_visible_ = false;
    int16_t touch_start_x_ = 0;
    int16_t touch_start_y_ = 0;
    int16_t touch_last_x_ = 0;
    int16_t touch_last_y_ = 0;
};

}  // namespace paper_screen
