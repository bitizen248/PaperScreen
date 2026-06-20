#pragma once

#include "board/board.h"
#include "display/display.h"
#include "services/power_service.h"
#include "services/settings_service.h"
#include "services/trmnl_service.h"
#include "services/wallpaper_service.h"
#include "services/wifi_service.h"
#include "ui/home_screen.h"
#include "ui/lock_screen.h"
#include "ui/scaffold_app.h"
#include "ui/settings_screen.h"
#include "ui/trmnl_screen.h"

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
        Settings,
        Trmnl,
    };

    void handle_power_button();
    void handle_touch();
    void handle_touch_event(const BoardInputEvent& event);
    void handle_home_button();
    void handle_trmnl_home_button();
    void handle_trmnl_home_press_state();
    bool handle_trmnl_swipe(int16_t start_x, int16_t start_y, int16_t end_x, int16_t end_y);
    bool handle_trmnl_menu_touch(int16_t x, int16_t y);
    void handle_trmnl_menu_action(TrmnlMenuAction action);
    void open_app(AppIcon icon);
    void open_settings();
    void open_trmnl();
    void close_current_app();
    void return_home_from_app();
    void show_dropdown();
    void hide_dropdown();
    void handle_dropdown_action(DropdownAction action);
    void enter_sleep();
    void handle_locked_state();
    void handle_settings_action(SettingRowAction action);
    void update_settings_screen();
    void refresh_trmnl(bool full_refresh);
    void generate_wallpaper();
    void render_home();
    void render_home_content();
    void render_current_app(bool preserve_status_bar);
    void render_settings(bool preserve_status_bar);
    void render_trmnl(bool preserve_status_bar);
    TrmnlSettings current_trmnl_settings() const;

    Board board_;
    Display display_;
    PowerService power_;
    SettingsService settings_;
    WifiService wifi_;
    TrmnlService trmnl_;
    WallpaperService wallpaper_;
    HomeScreen home_;
    SettingsScreen settings_screen_;
    TrmnlScreen trmnl_screen_;
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
    bool trmnl_image_rendered_ = false;
    char trmnl_last_image_url_[384] = {};
    bool trmnl_home_press_active_ = false;
    bool trmnl_home_menu_visible_ = false;
    unsigned long trmnl_home_last_seen_ms_ = 0;
    bool trmnl_home_tap_pending_ = false;
    unsigned long trmnl_home_tap_due_ms_ = 0;
    bool trmnl_home_ignore_until_release_ = false;
    bool backlight_enabled_ = false;
};

}  // namespace paper_screen
