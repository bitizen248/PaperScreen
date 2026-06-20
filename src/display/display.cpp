#include "display.h"

#include <Arduino.h>
#include <epdiy.h>

#include "display/drawing.h"
#include "display/image_renderer.h"
#include "display/screens/boot_screen.h"
#include "display/screens/lock_screen.h"
#include "display/terrain_renderer.h"
#include "display/widgets/app_grid.h"
#include "display/widgets/status_bar.h"

namespace {
#define PAPER_SCREEN_WAVEFORM EPD_BUILTIN_WAVEFORM
#define PAPER_SCREEN_BOARD epd_board_v7

    EpdiyHighlevelState g_epd;

    constexpr EpdRotation kNormalRotation = EPD_ROT_INVERTED_PORTRAIT;
    constexpr EpdRotation kTrmnlRotation = EPD_ROT_LANDSCAPE;
    constexpr EpdDrawMode kTrmnlUpdateMode = MODE_GC16;

    static inline void log_draw_error(enum EpdDrawError err) {
        if (err != EPD_DRAW_SUCCESS) {
            Serial.printf("[display] draw error: 0x%02x\n", err);
        }
    }

    EpdRect content_area(int width, int height) {
        return {
            .x = 0,
            .y = paper_screen::kStatusBarHeight,
            .width = width,
            .height = height - paper_screen::kStatusBarHeight,
        };
    }

    constexpr int kDropdownPanelTopPadding = 60;
    constexpr int kDropdownPanelRowHeight = 72;

    EpdRect dropdown_panel_rect(int width, int height)
    {
        return {
            .x = 0,
            .y = paper_screen::kStatusBarHeight,
            .width = width,
            .height = (height / 2) - paper_screen::kStatusBarHeight,
        };
    }

    EpdRect dropdown_cell_rect(EpdRect panel, int column, int row)
    {
        const int col_w = panel.width / 2;
        return {
            .x = panel.x + (column * col_w),
            .y = panel.y + kDropdownPanelTopPadding + (row * kDropdownPanelRowHeight),
            .width = col_w,
            .height = kDropdownPanelRowHeight,
        };
    }

    paper_screen::DropdownAction dropdown_action_at(int width, int height, int x, int y)
    {
        const EpdRect light_cell = dropdown_cell_rect(dropdown_panel_rect(width, height), 0, 2);
        if (x >= light_cell.x && x < light_cell.x + light_cell.width
            && y >= light_cell.y && y < light_cell.y + light_cell.height) {
            return paper_screen::DropdownAction::ToggleLight;
        }

        return paper_screen::DropdownAction::None;
    }

    constexpr int kSettingsRowHeight = 56;
    constexpr int kSettingsGroupHeight = 36;
    constexpr int kSettingsTopPadding = 18;
    constexpr int kSettingsXPadding = 28;

    void render_settings_rows(paper_screen::DisplayRenderContext ctx,
                              const paper_screen::SettingsViewModel &view_model) {
        int y = paper_screen::kStatusBarHeight + kSettingsTopPadding;
        for (int i = 0; i < view_model.row_count; ++i) {
            const paper_screen::SettingRowViewModel &row = view_model.rows[i];
            const bool group = row.action == paper_screen::SettingRowAction::None
                               && row.value[0] == '\0'
                               && !row.enabled;
            const int row_h = group ? kSettingsGroupHeight : kSettingsRowHeight;

            if (group) {
                paper_screen::draw_text_12(
                    ctx,
                    row.label,
                    kSettingsXPadding,
                    y + 24,
                    EPD_DRAW_ALIGN_LEFT,
                    paper_screen::kDarkGray,
                    paper_screen::kWhite
                );
                y += row_h;
                continue;
            }

            EpdRect row_rect = {
                .x = 0,
                .y = y,
                .width = ctx.width,
                .height = row_h,
            };
            epd_draw_hline(0, row_rect.y + row_rect.height - 1, ctx.width,
                           paper_screen::epd_gray(paper_screen::kLightGray), ctx.framebuffer);
            paper_screen::draw_text_20(ctx, row.label, kSettingsXPadding, y + 36, EPD_DRAW_ALIGN_LEFT);
            paper_screen::draw_text_20(
                ctx,
                row.value,
                ctx.width - kSettingsXPadding,
                y + 36,
                EPD_DRAW_ALIGN_RIGHT,
                row.enabled ? paper_screen::kBlack : paper_screen::kMidGray,
                paper_screen::kWhite
            );

            y += row_h;
        }
    }

    void render_trmnl_status(paper_screen::DisplayRenderContext ctx, const paper_screen::TrmnlViewModel &view_model) {
        paper_screen::draw_text_20(
            ctx,
            view_model.message,
            ctx.width / 2,
            (ctx.height / 2) - 12,
            EPD_DRAW_ALIGN_CENTER
        );
        if (view_model.detail != nullptr && view_model.detail[0] != '\0') {
            paper_screen::draw_text_12(
                ctx,
                view_model.detail,
                ctx.width / 2,
                (ctx.height / 2) + 26,
                EPD_DRAW_ALIGN_CENTER,
                paper_screen::kDarkGray,
                paper_screen::kWhite
            );
        }
    }

    void render_trmnl_content(paper_screen::DisplayRenderContext ctx, const paper_screen::TrmnlViewModel &view_model) {
        bool rendered_image = false;
        if (view_model.status == paper_screen::TrmnlFetchStatus::Ready
            && view_model.image_data != nullptr
            && view_model.image_size > 0) {
            if (paper_screen::render_png_image(ctx, view_model.image_data, view_model.image_size)) {
                rendered_image = true;
            }
        }

        if (!rendered_image) {
            render_trmnl_status(ctx, view_model);
        }

        if (view_model.show_exit_prompt) {
            const EpdRect prompt = {
                .x = (ctx.width - 430) / 2,
                .y = (ctx.height - 150) / 2,
                .width = 430,
                .height = 150,
            };
            epd_fill_rect(prompt, paper_screen::epd_gray(paper_screen::kWhite), ctx.framebuffer);
            epd_draw_rect(prompt, paper_screen::epd_gray(paper_screen::kBlack), ctx.framebuffer);
            paper_screen::draw_text_20(ctx, "Exit TRMNL?", ctx.width / 2, prompt.y + 58, EPD_DRAW_ALIGN_CENTER);
            paper_screen::draw_text_12(
                ctx,
                "Press Home again",
                ctx.width / 2,
                prompt.y + 96,
                EPD_DRAW_ALIGN_CENTER,
                paper_screen::kDarkGray,
                paper_screen::kWhite
            );
        }
    }

    EpdRect trmnl_overlay_rect()
    {
        return {
            .x = 10,
            .y = 10,
            .width = 96,
            .height = 54,
        };
    }

    void render_trmnl_overlay_content(paper_screen::DisplayRenderContext ctx, const char* label)
    {
        const EpdRect rect = trmnl_overlay_rect();
        epd_fill_rect(rect, paper_screen::epd_gray(paper_screen::kWhite), ctx.framebuffer);
        epd_draw_rect(rect, paper_screen::epd_gray(paper_screen::kBlack), ctx.framebuffer);
        paper_screen::draw_text_12(
            ctx,
            label == nullptr ? "" : label,
            rect.x + rect.width / 2,
            rect.y + 34,
            EPD_DRAW_ALIGN_CENTER,
            paper_screen::kBlack,
            paper_screen::kWhite
        );
    }

    constexpr int kTrmnlMenuRows = 4;
    constexpr int kTrmnlMenuPaddingX = 34;
    constexpr int kTrmnlMenuPaddingY = 20;
    constexpr int kTrmnlMenuItemGap = 16;

    struct TrmnlMenuItem {
        const char* label;
        paper_screen::TrmnlMenuAction action;
    };

    constexpr TrmnlMenuItem kTrmnlMenuItems[kTrmnlMenuRows] = {
        {"Refresh", paper_screen::TrmnlMenuAction::Refresh},
        {"Switch light", paper_screen::TrmnlMenuAction::ToggleLight},
        {"Return home", paper_screen::TrmnlMenuAction::ReturnHome},
        {"Cancel", paper_screen::TrmnlMenuAction::Cancel},
    };

    EpdRect trmnl_menu_rect(int width, int height)
    {
        return {
            .x = width / 2,
            .y = 0,
            .width = width - (width / 2),
            .height = height,
        };
    }

    int trmnl_menu_row_height(int height)
    {
        return (height - (2 * kTrmnlMenuPaddingY) - ((kTrmnlMenuRows - 1) * kTrmnlMenuItemGap)) / kTrmnlMenuRows;
    }

    EpdRect trmnl_menu_item_rect(EpdRect menu_rect, int index)
    {
        const int row_h = trmnl_menu_row_height(menu_rect.height);
        return {
            .x = menu_rect.x + kTrmnlMenuPaddingX,
            .y = menu_rect.y + kTrmnlMenuPaddingY + index * (row_h + kTrmnlMenuItemGap),
            .width = menu_rect.width - (2 * kTrmnlMenuPaddingX),
            .height = row_h,
        };
    }

    void render_trmnl_menu_content(paper_screen::DisplayRenderContext ctx)
    {
        const EpdRect rect = trmnl_menu_rect(ctx.width, ctx.height);
        epd_fill_rect(rect, paper_screen::epd_gray(paper_screen::kWhite), ctx.framebuffer);
        epd_draw_rect(rect, paper_screen::epd_gray(paper_screen::kBlack), ctx.framebuffer);
        for (int i = 0; i < kTrmnlMenuRows; ++i) {
            const EpdRect item = trmnl_menu_item_rect(rect, i);
            epd_draw_rect(item, paper_screen::epd_gray(paper_screen::kBlack), ctx.framebuffer);
            paper_screen::draw_text_20(
                ctx,
                kTrmnlMenuItems[i].label,
                item.x + item.width / 2,
                item.y + (item.height / 2) + 8,
                EPD_DRAW_ALIGN_CENTER,
                paper_screen::kBlack,
                paper_screen::kWhite
            );
        }
    }

    paper_screen::SettingRowAction settings_action_at(const paper_screen::SettingsViewModel &view_model,
                                                      int display_height, int x, int y) {
        if (x < 0 || y < paper_screen::kStatusBarHeight || y >= display_height) {
            return paper_screen::SettingRowAction::None;
        }

        int row_y = paper_screen::kStatusBarHeight + kSettingsTopPadding;
        for (int i = 0; i < view_model.row_count; ++i) {
            const paper_screen::SettingRowViewModel &row = view_model.rows[i];
            const bool group = row.action == paper_screen::SettingRowAction::None
                               && row.value[0] == '\0'
                               && !row.enabled;
            const int row_h = group ? kSettingsGroupHeight : kSettingsRowHeight;
            if (y >= row_y && y < row_y + row_h) {
                return row.enabled ? row.action : paper_screen::SettingRowAction::None;
            }
            row_y += row_h;
        }

        return paper_screen::SettingRowAction::None;
    }

    paper_screen::TrmnlMenuAction trmnl_menu_action_at(int width, int height, int x, int y)
    {
        const EpdRect rect = trmnl_menu_rect(width, height);
        if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) {
            return paper_screen::TrmnlMenuAction::None;
        }

        for (int i = 0; i < kTrmnlMenuRows; ++i) {
            const EpdRect item = trmnl_menu_item_rect(rect, i);
            if (x >= item.x && x < item.x + item.width && y >= item.y && y < item.y + item.height) {
                return kTrmnlMenuItems[i].action;
            }
        }
        return paper_screen::TrmnlMenuAction::None;
    }
} // namespace

namespace paper_screen {
    void Display::begin(RefreshPolicy refresh_policy) {
        Serial.println("[display] begin");
        refresh_policy_ = refresh_policy;
        Serial.print("[display] refresh policy set: ");
        Serial.println(refresh_policy_ == RefreshPolicy::Conservative ? "conservative" : "partial-when-safe");

        Serial.println("[display] epd_init begin");
        epd_init(&PAPER_SCREEN_BOARD, &ED047TC1, EPD_LUT_64K);
        Serial.println("[display] epd_init done");

        Serial.println("[display] vcom set 1560mV");
        epd_set_vcom(1560);

        Serial.println("[display] highlevel init begin");
        g_epd = epd_hl_init(PAPER_SCREEN_WAVEFORM);
        Serial.println("[display] highlevel init done");

        apply_rotation(kNormalRotation);
        epd_set_lcd_pixel_clock_MHz(17);

        initialized_ = true;
        Serial.println("[display] ready");
    }

    void Display::full_refresh() {
        Serial.println("[display] full refresh begin");
        if (!initialized_) {
            Serial.println("[display] full refresh skipped: display not initialized");
            return;
        }

        Serial.println("[display] low-level clear");
        epd_poweron();
        epd_clear();
        epd_poweroff();

        epd_hl_set_all_white(&g_epd);
        update_screen(MODE_GC16);

        Serial.println("[display] full refresh done");
    }

    void Display::render_boot_screen(const char *message) {
        Serial.println("[display] boot render begin");
        if (!initialized_) {
            Serial.println("[display] boot render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        epd_hl_set_all_white(&g_epd);
        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        ::paper_screen::render_boot_screen(ctx, message);

        update_screen(MODE_GC16);
        Serial.println("[display] boot render done");
    }

    void Display::render_lock_screen(const LockScreenViewModel &view_model) {
        Serial.println("[display] lock render begin");
        if (!initialized_) {
            Serial.println("[display] lock render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        epd_hl_set_all_white(&g_epd);
        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        ::paper_screen::render_lock_screen(ctx, view_model);

        update_screen(MODE_GC16);
        Serial.println("[display] lock render done");
    }

    void Display::render(const HomeViewModel &view_model) {
        Serial.println("[display] render begin");
        Serial.println();
        Serial.println("== PaperScreen ==");
        Serial.println(view_model.title);
        Serial.println(view_model.status_line);
        Serial.print("Refresh policy: ");
        Serial.println(refresh_policy_ == RefreshPolicy::Conservative ? "conservative" : "partial-when-safe");

        if (!initialized_) {
            Serial.println("[display] render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        Serial.println("[display] e-paper draw begin");
        epd_hl_set_all_white(&g_epd);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        if (view_model.wallpaper != nullptr) {
            render_heightmap_region(ctx, view_model.wallpaper, view_model.wallpaper_width,
                                     view_model.wallpaper_height, kStatusBarHeight);
        }
        render_status_bar(ctx, view_model.status_bar);
        render_app_grid(ctx, view_model, kStatusBarHeight + 28);

        Serial.println("[display] e-paper update begin");
        update_screen_logged(MODE_GC16, "[display] e-paper");
        Serial.println("[display] e-paper update done");
        Serial.println("[display] render done");
    }

    void Display::render(const AppScreenViewModel &view_model) {
        Serial.println("[display] app render begin");
        if (!initialized_) {
            Serial.println("[display] app render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        epd_hl_set_all_white(&g_epd);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        render_status_bar(ctx, view_model.status_bar);
        draw_text_20(
            ctx,
            view_model.app_name,
            ctx.width / 2,
            kStatusBarHeight + ((ctx.height - kStatusBarHeight) / 2),
            EPD_DRAW_ALIGN_CENTER
        );

        update_screen_logged(MODE_GC16, "[display] app");
        Serial.println("[display] app render done");
    }

    void Display::render(const SettingsViewModel &view_model) {
        Serial.println("[display] settings render begin");
        if (!initialized_) {
            Serial.println("[display] settings render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        epd_hl_set_all_white(&g_epd);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        render_status_bar(ctx, view_model.status_bar);
        render_settings_rows(ctx, view_model);

        update_screen_logged(MODE_GC16, "[display] settings");
        Serial.println("[display] settings render done");
    }

    void Display::render(const TrmnlViewModel &view_model) {
        Serial.println("[display] trmnl render begin");
        if (!initialized_) {
            Serial.println("[display] trmnl render skipped: display not initialized");
            return;
        }
        apply_rotation(kTrmnlRotation);

        Serial.println("[display] trmnl clean clear");
        epd_poweron();
        epd_clear();
        epd_poweroff();
        epd_hl_set_all_white(&g_epd);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        render_trmnl_content(ctx, view_model);

        update_screen(MODE_GC16);
        Serial.println("[display] trmnl render done");
    }

    void Display::render_content(const HomeViewModel &view_model) {
        Serial.println("[display] home content render begin");
        if (!initialized_) {
            Serial.println("[display] home content render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        clear_content_area(ctx);
        if (view_model.wallpaper != nullptr) {
            render_heightmap_region(ctx, view_model.wallpaper, view_model.wallpaper_width,
                                     view_model.wallpaper_height, kStatusBarHeight);
        }
        render_app_grid(ctx, view_model, kStatusBarHeight + 28);
        update_area(content_area(width_, height_), MODE_GC16);
        Serial.println("[display] home content render done");
    }

    void Display::render_content(const AppScreenViewModel &view_model) {
        Serial.println("[display] app content render begin");
        if (!initialized_) {
            Serial.println("[display] app content render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        clear_content_area(ctx);
        draw_text_20(
            ctx,
            view_model.app_name,
            ctx.width / 2,
            kStatusBarHeight + ((ctx.height - kStatusBarHeight) / 2),
            EPD_DRAW_ALIGN_CENTER
        );
        update_area(content_area(width_, height_), MODE_GC16);
        Serial.println("[display] app content render done");
    }

    void Display::render_content(const SettingsViewModel &view_model) {
        Serial.println("[display] settings content render begin");
        if (!initialized_) {
            Serial.println("[display] settings content render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        clear_content_area(ctx);
        render_settings_rows(ctx, view_model);
        update_area(content_area(width_, height_), MODE_GC16);
        Serial.println("[display] settings content render done");
    }

    void Display::render_content(const TrmnlViewModel &view_model) {
        Serial.println("[display] trmnl content render begin");
        if (!initialized_) {
            Serial.println("[display] trmnl content render skipped: display not initialized");
            return;
        }
        apply_rotation(kTrmnlRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        epd_fill_rect({0, 0, ctx.width, ctx.height}, epd_gray(kWhite), ctx.framebuffer);
        Serial.println("[display] trmnl framebuffer clear done");

        render_trmnl_content(ctx, view_model);
        Serial.println("[display] trmnl e-paper update begin");
        Serial.printf("[display] trmnl update_screen mode=%d temp=%d begin\n",
                      static_cast<int>(kTrmnlUpdateMode), epd_ambient_temperature());
        update_screen(kTrmnlUpdateMode);
        Serial.printf("[display] trmnl update_screen mode=%d done\n",
              static_cast<int>(kTrmnlUpdateMode));
        Serial.println("[display] trmnl e-paper update done");

        Serial.println("[display] trmnl content render done");
    }

    void Display::render_dropdown() {
        Serial.println("[display] dropdown render begin");
        if (!initialized_) {
            Serial.println("[display] dropdown render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        const EpdRect panel = dropdown_panel_rect(ctx.width, ctx.height);
        epd_fill_rect(panel, epd_gray(kWhite), ctx.framebuffer);
        epd_draw_rect(panel, epd_gray(kBlack), ctx.framebuffer);

        const int col_w = panel.width / 2;
        const int row_y = panel.y + kDropdownPanelTopPadding + 12;
        draw_text_20(ctx, "Quick", panel.x + 34, panel.y + 46, EPD_DRAW_ALIGN_LEFT);
        draw_text_12(ctx, "Wi-Fi", panel.x + 42, row_y + 44, EPD_DRAW_ALIGN_LEFT);
        draw_text_12(ctx, "Battery --%", panel.x + col_w + 42, row_y + 44, EPD_DRAW_ALIGN_LEFT);
        draw_text_12(ctx, "Refresh", panel.x + 42, row_y + 116, EPD_DRAW_ALIGN_LEFT);
        draw_text_12(ctx, "Sleep", panel.x + col_w + 42, row_y + 116, EPD_DRAW_ALIGN_LEFT);
        draw_text_12(ctx, "Switch light", panel.x + 42, row_y + 188, EPD_DRAW_ALIGN_LEFT);

        update_area(panel, MODE_GC16);
        Serial.println("[display] dropdown render done");
    }

    void Display::render_trmnl_overlay(const char* label) {
        Serial.println("[display] trmnl overlay render begin");
        if (!initialized_) {
            Serial.println("[display] trmnl overlay skipped: display not initialized");
            return;
        }
        apply_rotation(kTrmnlRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        render_trmnl_overlay_content(ctx, label);
        update_area(trmnl_overlay_rect(), kTrmnlUpdateMode);
        Serial.println("[display] trmnl overlay render done");
    }

    void Display::render_trmnl_menu() {
        Serial.println("[display] trmnl menu render begin");
        if (!initialized_) {
            Serial.println("[display] trmnl menu skipped: display not initialized");
            return;
        }
        apply_rotation(kTrmnlRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        render_trmnl_menu_content(ctx);
        update_area(trmnl_menu_rect(ctx.width, ctx.height), kTrmnlUpdateMode);
        Serial.println("[display] trmnl menu render done");
    }

    int Display::hit_test_home_app(const HomeViewModel &view_model, int x, int y) const {
        return app_grid_hit_test(view_model, width_, height_, kStatusBarHeight + 28, x, y);
    }

    SettingRowAction Display::hit_test_settings_action(const SettingsViewModel &view_model, int x, int y) const {
        return settings_action_at(view_model, height_, x, y);
    }

    DropdownAction Display::hit_test_dropdown_action(int x, int y) const {
        return dropdown_action_at(width_, height_, x, y);
    }

    TrmnlMenuAction Display::hit_test_trmnl_menu_action(int x, int y) const {
        return trmnl_menu_action_at(width_, height_, x, y);
    }

    int Display::width() const {
        return width_;
    }

    int Display::height() const {
        return height_;
    }

    int Display::status_bar_height() const {
        return kStatusBarHeight;
    }

    void Display::apply_rotation(enum EpdRotation rotation) {
        if (rotation_ != rotation || width_ == 0 || height_ == 0) {
            rotation_ = rotation;
            epd_set_rotation(rotation_);
            width_ = epd_rotated_display_width();
            height_ = epd_rotated_display_height();
            Serial.printf("[display] rotation=%d dimensions %d x %d\n", static_cast<int>(rotation_), width_, height_);
        }
    }

    enum EpdDrawError Display::update_screen(enum EpdDrawMode mode) {
        epd_poweron();
        const EpdDrawError err = epd_hl_update_screen(&g_epd, mode, epd_ambient_temperature());
        log_draw_error(err);
        epd_poweroff();
        return err;
    }

    enum EpdDrawError Display::update_area(EpdRect area, enum EpdDrawMode mode) {
        Serial.printf("[display] dirty update requested x=%d y=%d w=%d h=%d\n", area.x, area.y, area.width,
                      area.height);
        epd_poweron();
        const EpdDrawError err = epd_hl_update_area(&g_epd, mode, epd_ambient_temperature(), area);
        log_draw_error(err);
        epd_poweroff();
        return err;
    }

    void Display::clear_content_area(DisplayRenderContext ctx) {
        epd_fill_rect(content_area(ctx.width, ctx.height), epd_gray(kWhite), ctx.framebuffer);
    }

    void Display::update_screen_logged(enum EpdDrawMode mode, const char *prefix) {
        if (prefix != nullptr && prefix[0] != '\0') {
            Serial.printf("%s update_screen mode=%d temp=%d begin\n",
                          prefix, static_cast<int>(mode), epd_ambient_temperature());
        }
        update_screen(mode);
        if (prefix != nullptr && prefix[0] != '\0') {
            Serial.printf("%s update_screen mode=%d done\n",
                          prefix, static_cast<int>(mode));
        }
    }
} // namespace paper_screen
