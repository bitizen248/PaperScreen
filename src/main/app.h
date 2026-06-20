#pragma once

#include "apps/reader/reader_app.h"
#include "apps/trmnl/trmnl_app.h"
#include "apps/trmnl/trmnl_cache.h"
#include "board/board.h"
#include "board/board_battery.h"
#include "board/board_rtc.h"
#include "board/board_storage.h"
#include "board/board_usb_storage.h"
#include "display/display.h"
#include "services/battery_service.h"
#include "services/power_service.h"
#include "services/settings_service.h"
#include "services/storage_service.h"
#include "services/time_service.h"
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
        GenericApp,
        UsbDrive,
    };

    void handle_power_button();
    void handle_touch();
    void handle_touch_event(const BoardInputEvent& event);
    void handle_home_button();
    void handle_trmnl_home_button();
    void handle_trmnl_home_press_state();
    AppEventResult dispatch_trmnl_event(const AppEvent& event);
    void handle_trmnl_app_result(const AppEventResult& result);
    bool handle_settings_swipe(int16_t start_x, int16_t start_y, int16_t end_x, int16_t end_y);
    bool handle_trmnl_swipe(int16_t start_x, int16_t start_y, int16_t end_x, int16_t end_y);
    bool handle_trmnl_menu_touch(int16_t x, int16_t y);
    void open_app(AppIcon icon);
    void open_settings();
    void open_trmnl();
    void open_generic_app(PaperApp& app);
    void close_current_app();
    void close_generic_app();
    void render_generic_app(bool full_refresh);
    AppEventResult dispatch_generic_app_event(const AppEvent& event);
    void handle_generic_app_result(const AppEventResult& result);
    void open_usb_drive();
    void render_usb_drive();
    void return_home_from_app();
    void show_dropdown();
    void hide_dropdown();
    void handle_dropdown_action(DropdownAction action);
    void enter_sleep();
    void handle_locked_state();
    void handle_settings_action(SettingRowAction action);
    void mark_activity();
    void check_idle_sleep_timeout();
    void update_settings_screen();
    void refresh_trmnl(bool full_refresh,
                       bool special_function = false,
                       bool advance_playlist = false,
                       bool refetch_current = false,
                       bool show_action_label = true);
    void check_trmnl_refresh_schedule();
    void schedule_trmnl_refresh(uint32_t refresh_seconds);
    void clear_trmnl_refresh_schedule();
    void enter_trmnl_autonomous_sleep(uint32_t refresh_seconds);
    bool trmnl_desk_mode_active() const;
    bool trmnl_manual_action_allowed(const char* label);
    void clear_trmnl_cached_fallback();
    void generate_wallpaper();
    void render_home();
    void render_home_content();
    void render_current_app(bool preserve_status_bar);
    void render_settings(bool preserve_status_bar);
    void render_trmnl(bool preserve_status_bar);
    void apply_status_bar_time(StatusBarViewModel& status_bar);
    void apply_status_bar_battery(StatusBarViewModel& status_bar);
    void refresh_status_clock_if_needed();
    StatusBarViewModel current_status_bar_model();
    TrmnlSettings current_trmnl_settings() const;

    Board board_;
    BoardRtc rtc_;
    BoardBattery battery_board_;
    BoardStorage board_storage_;
    BoardUsbStorage usb_storage_;
    Display display_;
    BatteryService battery_;
    PowerService power_;
    SettingsService settings_;
    StorageService storage_;
    WifiService wifi_;
    TimeService time_;
    TrmnlService trmnl_;
    TrmnlApp trmnl_app_;
    TrmnlCache trmnl_cache_;
    ReaderApp reader_app_;
    PaperApp* generic_app_ = nullptr;
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
    unsigned long last_activity_ms_ = 0;
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
    bool backlight_enabled_ = false;
    char status_bar_time_[8] = "--:--";
    char rendered_status_bar_time_[8] = "";
    char status_bar_battery_[6] = "--%";
    char rendered_status_bar_battery_[6] = "";
    unsigned long last_status_clock_check_ms_ = 0;
    unsigned long last_battery_fallback_update_ms_ = 0;
    unsigned long last_trmnl_manual_action_ms_ = 0;
    uint8_t* trmnl_cached_fallback_image_ = nullptr;
    size_t trmnl_cached_fallback_size_ = 0;
};

}  // namespace paper_screen
