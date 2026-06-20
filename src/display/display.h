#pragma once

#include <stdint.h>

#include <epdiy.h>

#include "display/refresh_policy.h"
#include "display/render_context.h"
#include "sdk/app_result.h"
#include "ui/home_screen.h"
#include "ui/lock_screen.h"
#include "ui/settings_screen.h"
#include "ui/trmnl_screen.h"

namespace paper_screen {

enum class TrmnlMenuAction : uint8_t {
    None,
    NextScreen,
    RefetchCurrent,
    SpecialFunction,
    ToggleLight,
    ReturnHome,
    Cancel,
};

enum class DropdownAction : uint8_t {
    None,
    ToggleLight,
};

class Display {
public:
    void begin(RefreshPolicy refresh_policy);
    void full_refresh();
    void render_boot_screen(const char* message);
    void render_lock_screen(const LockScreenViewModel& view_model);
    void render(const HomeViewModel& view_model);
    void render(const AppScreenViewModel& view_model);
    void render(const SettingsViewModel& view_model);
    void render(const TrmnlViewModel& view_model);
    void render_status_bar_only(const StatusBarViewModel& view_model);
    void render_content(const HomeViewModel& view_model);
    void render_content(const AppScreenViewModel& view_model);
    void render_content(const SettingsViewModel& view_model);
    void render_content(const TrmnlViewModel& view_model);
    // Generic SDK PaperApp render path: caller draws into the returned
    // framebuffer-backed context, then end_app_frame() flushes it.
    DisplayRenderContext begin_app_frame(bool full_refresh);
    void end_app_frame(AppRefreshHint hint);
    void render_dropdown();
    void render_trmnl_overlay(const char* label);
    void render_trmnl_activity_overlay(TrmnlFetchStatus status, const char* battery);
    void render_trmnl_menu();
    void identify_flash();
    int hit_test_home_app(const HomeViewModel& view_model, int x, int y) const;
    SettingRowAction hit_test_settings_action(const SettingsViewModel& view_model, int x, int y) const;
    DropdownAction hit_test_dropdown_action(int x, int y) const;
    TrmnlMenuAction hit_test_trmnl_menu_action(int x, int y) const;
    int width() const;
    int height() const;
    int status_bar_height() const;

private:
    void apply_rotation(enum EpdRotation rotation);
    enum EpdDrawError update_screen(enum EpdDrawMode mode);
    enum EpdDrawError update_area(EpdRect area, enum EpdDrawMode mode);
    void update_screen_logged(enum EpdDrawMode mode, const char* prefix);
    void clear_content_area(DisplayRenderContext ctx);
    enum EpdDrawMode settings_content_mode() const;

    RefreshPolicy refresh_policy_ = RefreshPolicy::Conservative;
    bool initialized_ = false;
    enum EpdRotation rotation_ = EPD_ROT_INVERTED_PORTRAIT;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace paper_screen
