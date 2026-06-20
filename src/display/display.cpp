#include "display.h"

#include <Arduino.h>
#include <cstdlib>
#include <epdiy.h>

#include "display/drawing.h"
#include "display/image_renderer.h"
#include "display/screens/boot_screen.h"
#include "display/screens/lock_screen.h"
#include "display/terrain_renderer.h"
#include "display/widgets/app_grid.h"
#include "display/widgets/status_bar.h"

// Counters persisted across deep-sleep for anti-ghosting waveform selection.
// Cleared on power-off; survive timer-wakeup cycles.
RTC_DATA_ATTR static uint16_t s_trmnl_du_counter = 0;
RTC_DATA_ATTR static uint16_t s_trmnl_gl_counter = 0;

namespace {
#define PAPER_SCREEN_WAVEFORM EPD_BUILTIN_WAVEFORM
#define PAPER_SCREEN_BOARD epd_board_v7

    EpdiyHighlevelState g_epd;

    constexpr EpdRotation kNormalRotation = EPD_ROT_INVERTED_PORTRAIT;
    constexpr EpdRotation kTrmnlRotation = EPD_ROT_LANDSCAPE;
    constexpr bool kTrmnlThoroughRefresh = true;

    // Slow refreshes (>= 30 min) always use GL16; fast refreshes cycle
    // DU → GL16 every 10 → GC16 every 3rd GL16 to stay ghost-free.
    constexpr uint32_t kSlowRefreshThresholdSec = 1800;
    constexpr uint16_t kFullRefreshEvery = 10;
    constexpr uint16_t kGhostClearEvery  = 3;

    EpdDrawMode select_trmnl_mode(uint32_t refresh_seconds, bool force_full)
    {
        if (force_full || refresh_seconds >= kSlowRefreshThresholdSec || refresh_seconds == 0) {
            return MODE_GL16;
        }
        s_trmnl_du_counter++;
        if (s_trmnl_du_counter % kFullRefreshEvery != 0) {
            return MODE_DU;
        }
        s_trmnl_gl_counter++;
        if (s_trmnl_gl_counter % kGhostClearEvery == 0) {
            return MODE_GC16;
        }
        return MODE_GL16;
    }

    static inline void log_draw_error(enum EpdDrawError err) {
        if (err != EPD_DRAW_SUCCESS) {
            Serial.printf("[display] draw error: 0x%02x\n", err);
        }
    }

    void thorough_white_clear(EpdiyHighlevelState* state, const char* prefix)
    {
        Serial.printf("%s thorough white clear begin\n", prefix);
        epd_hl_set_all_white(state);
        epd_poweron();
        EpdDrawError err = epd_hl_update_screen(state, MODE_GC16, epd_ambient_temperature());
        log_draw_error(err);
        epd_clear();
        epd_poweroff();
        Serial.printf("%s thorough white clear done\n", prefix);
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

    int parse_percent(const char* text)
    {
        if (text == nullptr || text[0] < '0' || text[0] > '9') {
            return -1;
        }

        const int percent = std::atoi(text);
        if (percent < 0) {
            return 0;
        }
        if (percent > 100) {
            return 100;
        }
        return percent;
    }

    void draw_trmnl_refresh_icon(paper_screen::DisplayRenderContext ctx, int x, int y)
    {
        const uint8_t black = paper_screen::epd_gray(paper_screen::kBlack);
        epd_draw_rect({x + 1, y + 2, 14, 14}, black, ctx.framebuffer);
        epd_fill_rect({x + 8, y, 8, 8}, paper_screen::epd_gray(paper_screen::kWhite), ctx.framebuffer);
        epd_draw_line(x + 13, y + 3, x + 17, y + 3, black, ctx.framebuffer);
        epd_draw_line(x + 17, y + 3, x + 17, y + 7, black, ctx.framebuffer);
        epd_draw_line(x + 17, y + 7, x + 13, y + 3, black, ctx.framebuffer);
    }

    void draw_trmnl_download_icon(paper_screen::DisplayRenderContext ctx, int x, int y)
    {
        const uint8_t black = paper_screen::epd_gray(paper_screen::kBlack);
        epd_draw_vline(x + 9, y + 1, 10, black, ctx.framebuffer);
        epd_draw_line(x + 5, y + 7, x + 9, y + 12, black, ctx.framebuffer);
        epd_draw_line(x + 13, y + 7, x + 9, y + 12, black, ctx.framebuffer);
        epd_draw_hline(x + 4, y + 16, 12, black, ctx.framebuffer);
    }

    void draw_trmnl_battery_icon(paper_screen::DisplayRenderContext ctx, int x, int y, const char* percent_text)
    {
        constexpr int body_w = 24;
        constexpr int body_h = 12;
        const uint8_t black = paper_screen::epd_gray(paper_screen::kBlack);
        const uint8_t white = paper_screen::epd_gray(paper_screen::kWhite);
        const int percent = parse_percent(percent_text);

        epd_draw_rect({x, y, body_w, body_h}, black, ctx.framebuffer);
        epd_fill_rect({x + body_w, y + 4, 3, 4}, black, ctx.framebuffer);
        epd_fill_rect({x + 2, y + 2, body_w - 4, body_h - 4}, white, ctx.framebuffer);

        if (percent >= 0) {
            const int fill_w = ((body_w - 4) * percent) / 100;
            if (fill_w > 0) {
                epd_fill_rect({x + 2, y + 2, fill_w, body_h - 4}, black, ctx.framebuffer);
            }
        }
    }

    void render_trmnl_indicator_icons(paper_screen::DisplayRenderContext ctx,
                                      paper_screen::TrmnlFetchStatus status,
                                      const char* battery)
    {
        constexpr int margin = 10;
        constexpr int indicator_size = 24;
        const uint8_t white = paper_screen::epd_gray(paper_screen::kWhite);

        if (status == paper_screen::TrmnlFetchStatus::RequestingMetadata
            || status == paper_screen::TrmnlFetchStatus::DownloadingImage) {
            epd_fill_rect({margin - 4, margin - 4, indicator_size + 8, indicator_size + 8}, white, ctx.framebuffer);
            if (status == paper_screen::TrmnlFetchStatus::RequestingMetadata) {
                draw_trmnl_refresh_icon(ctx, margin, margin);
            } else {
                draw_trmnl_download_icon(ctx, margin, margin);
            }
        }

        epd_fill_rect({ctx.width - 43, margin - 4, 33, 20}, white, ctx.framebuffer);
        draw_trmnl_battery_icon(ctx, ctx.width - 37, margin, battery);
    }

    void render_trmnl_indicators(paper_screen::DisplayRenderContext ctx, const paper_screen::TrmnlViewModel &view_model)
    {
        render_trmnl_indicator_icons(ctx, view_model.status, view_model.status_bar.battery);
    }

    void render_trmnl_content(paper_screen::DisplayRenderContext ctx, const paper_screen::TrmnlViewModel &view_model) {
        bool rendered_image = false;
        Serial.printf("[display] trmnl content status=%d image=%s bytes=%u\n",
                      static_cast<int>(view_model.status),
                      view_model.image_data != nullptr ? "yes" : "no",
                      static_cast<unsigned>(view_model.image_size));
        if (view_model.status == paper_screen::TrmnlFetchStatus::Ready
            && view_model.image_data != nullptr
            && view_model.image_size > 0) {
            Serial.println("[display] trmnl png render begin");
            if (paper_screen::render_image(ctx, view_model.image_data, view_model.image_size)) {
                rendered_image = true;
            }
            Serial.printf("[display] trmnl png render done rendered=%s\n", rendered_image ? "yes" : "no");
        }

        if (!rendered_image) {
            Serial.println("[display] trmnl status render begin");
            render_trmnl_status(ctx, view_model);
            Serial.println("[display] trmnl status render done");
        }

        Serial.println("[display] trmnl indicators render begin");
        render_trmnl_indicators(ctx, view_model);
        Serial.println("[display] trmnl indicators render done");

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

    void render_app_placeholder(paper_screen::DisplayRenderContext ctx, const paper_screen::AppScreenViewModel &view_model)
    {
        const int content_y = paper_screen::kStatusBarHeight;
        const int content_h = ctx.height - content_y;
        const int center_x = ctx.width / 2;
        const int center_y = content_y + (content_h / 2);

        paper_screen::draw_text_20(
            ctx,
            view_model.headline[0] != '\0' ? view_model.headline : view_model.app_name,
            center_x,
            center_y - 34,
            EPD_DRAW_ALIGN_CENTER
        );

        if (view_model.detail[0] != '\0') {
            paper_screen::draw_text_12(
                ctx,
                view_model.detail,
                center_x,
                center_y + 8,
                EPD_DRAW_ALIGN_CENTER,
                paper_screen::kDarkGray,
                paper_screen::kWhite
            );
        }

        if (view_model.footer[0] != '\0') {
            paper_screen::draw_text_12(
                ctx,
                view_model.footer,
                center_x,
                ctx.height - 42,
                EPD_DRAW_ALIGN_CENTER,
                paper_screen::kMidGray,
                paper_screen::kWhite
            );
        }
    }

    int smooth_noise_at(int x, int y, int cell_size, uint32_t seed)
    {
        const int gx = x / cell_size;
        const int gy = y / cell_size;
        const int fx = x % cell_size;
        const int fy = y % cell_size;
        auto sample = [seed](int sx, int sy) {
            const uint32_t hash = static_cast<uint32_t>(sx * 73856093U)
                                ^ static_cast<uint32_t>(sy * 19349663U)
                                ^ seed;
            return static_cast<int>((hash >> 24) & 0xff);
        };

        const int n00 = sample(gx, gy);
        const int n10 = sample(gx + 1, gy);
        const int n01 = sample(gx, gy + 1);
        const int n11 = sample(gx + 1, gy + 1);
        const int ix0 = n00 + ((n10 - n00) * fx) / cell_size;
        const int ix1 = n01 + ((n11 - n01) * fx) / cell_size;
        return ix0 + ((ix1 - ix0) * fy) / cell_size;
    }

    int terrain_height_at(int x, int y)
    {
        int value = 18;

        const int peaks[][5] = {
            {76, 44, 168, 24, 16},
            {50, 31, 74, 22, 15},
            {105, 30, 66, 28, 18},
            {101, 63, 72, 32, 20},
            {68, 66, 48, 22, 15},
            {124, 51, 38, 24, 16},
        };
        for (const auto& peak : peaks) {
            const int peak_dx = x - peak[0];
            const int peak_dy = y - peak[1];
            const int contribution = peak[2] - ((peak_dx * peak_dx) / peak[3]) - ((peak_dy * peak_dy) / peak[4]);
            if (contribution > 0) {
                value += contribution;
            }
        }

        value += (smooth_noise_at(x, y, 31, 0x9e3779b9U) - 128) / 7;
        value += (smooth_noise_at(x, y, 17, 0x85ebca6bU) - 128) / 16;
        if (value < 0) {
            return 0;
        }
        if (value > 255) {
            return 255;
        }
        return value;
    }

    int map_grid_x(EpdRect rect, int x)
    {
        constexpr int grid_w = 160;
        return rect.x + (x * rect.width) / (grid_w - 1);
    }

    int map_grid_y(EpdRect rect, int y)
    {
        constexpr int grid_h = 90;
        return rect.y + (y * rect.height) / (grid_h - 1);
    }

    int interpolate_axis(int level, int a_value, int b_value, int a_coord, int b_coord)
    {
        const int delta = b_value - a_value;
        if (delta == 0) {
            return (a_coord + b_coord) / 2;
        }

        return a_coord + ((level - a_value) * (b_coord - a_coord)) / delta;
    }

    void draw_contour_segment(paper_screen::DisplayRenderContext ctx, int x0, int y0, int x1, int y1, bool major)
    {
        epd_draw_line(x0, y0, x1, y1, paper_screen::epd_gray(paper_screen::kBlack), ctx.framebuffer);
        if (major) {
            epd_draw_line(x0, y0 + 1, x1, y1 + 1, paper_screen::epd_gray(paper_screen::kBlack), ctx.framebuffer);
        }
    }

    void render_terrain_contours(paper_screen::DisplayRenderContext ctx)
    {
        constexpr int grid_w = 160;
        constexpr int grid_h = 90;
        constexpr int contour_step = 16;
        const EpdRect map = {
            .x = 28,
            .y = paper_screen::kStatusBarHeight + 22,
            .width = ctx.width - 56,
            .height = ctx.height - paper_screen::kStatusBarHeight - 44,
        };

        for (int gy = 0; gy < grid_h - 1; ++gy) {
            for (int gx = 0; gx < grid_w - 1; ++gx) {
                const int v0 = terrain_height_at(gx, gy);
                const int v1 = terrain_height_at(gx + 1, gy);
                const int v2 = terrain_height_at(gx + 1, gy + 1);
                const int v3 = terrain_height_at(gx, gy + 1);
                const int sx0 = map_grid_x(map, gx);
                const int sx1 = map_grid_x(map, gx + 1);
                const int sy0 = map_grid_y(map, gy);
                const int sy1 = map_grid_y(map, gy + 1);

                for (int level = contour_step; level < 256; level += contour_step) {
                    int px[4];
                    int py[4];
                    int count = 0;

                    if ((v0 < level) != (v1 < level)) {
                        px[count] = interpolate_axis(level, v0, v1, sx0, sx1);
                        py[count] = sy0;
                        ++count;
                    }
                    if ((v1 < level) != (v2 < level)) {
                        px[count] = sx1;
                        py[count] = interpolate_axis(level, v1, v2, sy0, sy1);
                        ++count;
                    }
                    if ((v2 < level) != (v3 < level)) {
                        px[count] = interpolate_axis(level, v2, v3, sx1, sx0);
                        py[count] = sy1;
                        ++count;
                    }
                    if ((v3 < level) != (v0 < level)) {
                        px[count] = sx0;
                        py[count] = interpolate_axis(level, v3, v0, sy1, sy0);
                        ++count;
                    }

                    const bool major = level % (contour_step * 4) == 0;
                    if (count == 2) {
                        draw_contour_segment(ctx, px[0], py[0], px[1], py[1], major);
                    } else if (count == 4) {
                        draw_contour_segment(ctx, px[0], py[0], px[1], py[1], major);
                        draw_contour_segment(ctx, px[2], py[2], px[3], py[3], major);
                    }
                }
            }
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

    constexpr int kTrmnlMenuRows = 6;
    constexpr int kTrmnlMenuPaddingX = 34;
    constexpr int kTrmnlMenuPaddingY = 20;
    constexpr int kTrmnlMenuItemGap = 16;

    struct TrmnlMenuItem {
        const char* label;
        paper_screen::TrmnlMenuAction action;
    };

    constexpr TrmnlMenuItem kTrmnlMenuItems[kTrmnlMenuRows] = {
        {"Next screen", paper_screen::TrmnlMenuAction::NextScreen},
        {"Refetch current", paper_screen::TrmnlMenuAction::RefetchCurrent},
        {"Special button", paper_screen::TrmnlMenuAction::SpecialFunction},
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
        render_app_placeholder(ctx, view_model);

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

        if (kTrmnlThoroughRefresh) {
            thorough_white_clear(&g_epd, "[display] trmnl");
        } else {
            Serial.println("[display] trmnl clean clear");
            epd_poweron();
            epd_clear();
            epd_poweroff();
        }
        Serial.println("[display] trmnl framebuffer white begin");
        epd_hl_set_all_white(&g_epd);
        Serial.println("[display] trmnl framebuffer white done");

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;
        Serial.printf("[display] trmnl ctx framebuffer=%p size=%d x %d image=%p bytes=%u status=%d\n",
                      ctx.framebuffer,
                      ctx.width,
                      ctx.height,
                      view_model.image_data,
                      static_cast<unsigned>(view_model.image_size),
                      static_cast<int>(view_model.status));

        Serial.println("[display] trmnl content draw begin");
        render_trmnl_content(ctx, view_model);
        Serial.println("[display] trmnl content draw done");

        const EpdDrawMode mode = select_trmnl_mode(view_model.refresh_seconds, view_model.maximum_compatibility);
        Serial.printf("[display] trmnl update_screen mode=%d temp=%d begin\n",
                      static_cast<int>(mode), epd_ambient_temperature());
        update_screen(mode);
        Serial.printf("[display] trmnl update_screen mode=%d done\n", static_cast<int>(mode));
        if (kTrmnlThoroughRefresh && mode == MODE_GC16
            && view_model.image_data != nullptr && view_model.image_size > 0) {
            Serial.println("[display] trmnl contrast settle update begin");
            update_screen(mode);
            Serial.println("[display] trmnl contrast settle update done");
        }
        Serial.println("[display] trmnl render done");
    }

    void Display::render_status_bar_only(const StatusBarViewModel& view_model)
    {
        Serial.println("[display] status bar render begin");
        if (!initialized_) {
            Serial.println("[display] status bar render skipped: display not initialized");
            return;
        }
        apply_rotation(kNormalRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        render_status_bar(ctx, view_model);
        update_area({0, 0, ctx.width, kStatusBarHeight}, MODE_GC16);
        Serial.println("[display] status bar render done");
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
        if (view_model.icon == paper_screen::AppIcon::Terrain) {
            render_terrain_contours(ctx);
        } else {
            render_app_placeholder(ctx, view_model);
        }
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
        update_area(content_area(width_, height_), settings_content_mode());
        Serial.println("[display] settings content render done");
    }

    void Display::render_content(const TrmnlViewModel &view_model) {
        Serial.println("[display] trmnl content render begin");
        if (!initialized_) {
            Serial.println("[display] trmnl content render skipped: display not initialized");
            return;
        }
        apply_rotation(kTrmnlRotation);
        Serial.printf("[display] trmnl content rotation ready dimensions %d x %d\n", width_, height_);

        if (kTrmnlThoroughRefresh) {
            thorough_white_clear(&g_epd, "[display] trmnl content");
        }

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;
        Serial.printf("[display] trmnl content ctx framebuffer=%p size=%d x %d image=%p bytes=%u status=%d\n",
                      ctx.framebuffer,
                      ctx.width,
                      ctx.height,
                      view_model.image_data,
                      static_cast<unsigned>(view_model.image_size),
                      static_cast<int>(view_model.status));

        epd_fill_rect({0, 0, ctx.width, ctx.height}, epd_gray(kWhite), ctx.framebuffer);
        Serial.println("[display] trmnl framebuffer clear done");

        Serial.println("[display] trmnl content draw begin");
        render_trmnl_content(ctx, view_model);
        Serial.println("[display] trmnl content draw done");
        Serial.println("[display] trmnl e-paper update begin");
        const EpdDrawMode mode = select_trmnl_mode(view_model.refresh_seconds, view_model.maximum_compatibility);
        Serial.printf("[display] trmnl update_screen mode=%d temp=%d begin\n",
                      static_cast<int>(mode), epd_ambient_temperature());
        update_screen(mode);
        Serial.printf("[display] trmnl update_screen mode=%d done\n", static_cast<int>(mode));
        if (kTrmnlThoroughRefresh && mode == MODE_GC16
            && view_model.image_data != nullptr && view_model.image_size > 0) {
            Serial.println("[display] trmnl contrast settle update begin");
            update_screen(mode);
            Serial.println("[display] trmnl contrast settle update done");
        }
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
        update_area(trmnl_overlay_rect(), MODE_GL16);
        Serial.println("[display] trmnl overlay render done");
    }

    void Display::render_trmnl_activity_overlay(TrmnlFetchStatus status, const char* battery) {
        Serial.println("[display] trmnl activity overlay render begin");
        if (!initialized_) {
            Serial.println("[display] trmnl activity overlay skipped: display not initialized");
            return;
        }
        apply_rotation(kTrmnlRotation);

        DisplayRenderContext ctx;
        ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
        ctx.width = width_;
        ctx.height = height_;

        render_trmnl_indicator_icons(ctx, status, battery);
        update_area({0, 0, ctx.width, 34}, MODE_GL16);
        Serial.println("[display] trmnl activity overlay render done");
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
        update_area(trmnl_menu_rect(ctx.width, ctx.height), MODE_GL16);
        Serial.println("[display] trmnl menu render done");
    }

    void Display::identify_flash()
    {
        if (!initialized_) return;
        Serial.println("[display] identify flash begin");
        apply_rotation(kTrmnlRotation);
        epd_hl_set_all_white(&g_epd);
        epd_poweron();
        epd_hl_update_screen(&g_epd, MODE_DU, epd_ambient_temperature());
        epd_poweroff();
        delay(300);
        epd_poweron();
        epd_hl_update_screen(&g_epd, MODE_DU, epd_ambient_temperature());
        epd_poweroff();
        Serial.println("[display] identify flash done");
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

    enum EpdDrawMode Display::settings_content_mode() const
    {
        return refresh_policy_ == RefreshPolicy::PartialWhenSafe ? MODE_GL16 : MODE_GC16;
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
