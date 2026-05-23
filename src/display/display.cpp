#include "display.h"

#include <Arduino.h>
#include <epdiy.h>

#include "display/drawing.h"
#include "display/screens/boot_screen.h"
#include "display/screens/lock_screen.h"
#include "display/widgets/app_grid.h"
#include "display/widgets/status_bar.h"

namespace {

#define PAPER_SCREEN_WAVEFORM EPD_BUILTIN_WAVEFORM
#define PAPER_SCREEN_BOARD epd_board_v7

EpdiyHighlevelState g_epd;

static inline void log_draw_error(enum EpdDrawError err)
{
    if (err != EPD_DRAW_SUCCESS) {
        Serial.printf("[display] draw error: 0x%02x\n", err);
    }
}

EpdRect content_area(int width, int height)
{
    return {
        .x = 0,
        .y = paper_screen::kStatusBarHeight,
        .width = width,
        .height = height - paper_screen::kStatusBarHeight,
    };
}

}  // namespace

namespace paper_screen {

void Display::begin(RefreshPolicy refresh_policy)
{
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

    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    epd_set_lcd_pixel_clock_MHz(17);

    width_ = epd_rotated_display_width();
    height_ = epd_rotated_display_height();
    Serial.printf("[display] dimensions %d x %d\n", width_, height_);

    initialized_ = true;
    Serial.println("[display] ready");
}

void Display::full_refresh()
{
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

void Display::render_boot_screen(const char* message)
{
    Serial.println("[display] boot render begin");
    if (!initialized_) {
        Serial.println("[display] boot render skipped: display not initialized");
        return;
    }

    epd_hl_set_all_white(&g_epd);
    DisplayRenderContext ctx;
    ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
    ctx.width = width_;
    ctx.height = height_;

    ::paper_screen::render_boot_screen(ctx, message);

    update_screen(MODE_GC16);
    Serial.println("[display] boot render done");
}

void Display::render_lock_screen(const LockScreenViewModel& view_model)
{
    Serial.println("[display] lock render begin");
    if (!initialized_) {
        Serial.println("[display] lock render skipped: display not initialized");
        return;
    }

    epd_hl_set_all_white(&g_epd);
    DisplayRenderContext ctx;
    ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
    ctx.width = width_;
    ctx.height = height_;

    ::paper_screen::render_lock_screen(ctx, view_model);

    update_screen(MODE_GC16);
    Serial.println("[display] lock render done");
}

void Display::render(const HomeViewModel& view_model)
{
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

    Serial.println("[display] e-paper draw begin");
    epd_hl_set_all_white(&g_epd);

    DisplayRenderContext ctx;
    ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
    ctx.width = width_;
    ctx.height = height_;

    render_status_bar(ctx, view_model.status_bar);
    render_app_grid(ctx, view_model, kStatusBarHeight + 28);

    Serial.println("[display] e-paper update begin");
    update_screen(MODE_GC16);
    Serial.println("[display] e-paper update done");
    Serial.println("[display] render done");
}

void Display::render(const AppScreenViewModel& view_model)
{
    Serial.println("[display] app render begin");
    if (!initialized_) {
        Serial.println("[display] app render skipped: display not initialized");
        return;
    }

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

    update_screen(MODE_GC16);
    Serial.println("[display] app render done");
}

void Display::render_content(const HomeViewModel& view_model)
{
    Serial.println("[display] home content render begin");
    if (!initialized_) {
        Serial.println("[display] home content render skipped: display not initialized");
        return;
    }

    DisplayRenderContext ctx;
    ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
    ctx.width = width_;
    ctx.height = height_;

    clear_content_area(ctx);
    render_app_grid(ctx, view_model, kStatusBarHeight + 28);
    update_area(content_area(width_, height_), MODE_GC16);
    Serial.println("[display] home content render done");
}

void Display::render_content(const AppScreenViewModel& view_model)
{
    Serial.println("[display] app content render begin");
    if (!initialized_) {
        Serial.println("[display] app content render skipped: display not initialized");
        return;
    }

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

void Display::render_dropdown()
{
    Serial.println("[display] dropdown render begin");
    if (!initialized_) {
        Serial.println("[display] dropdown render skipped: display not initialized");
        return;
    }

    DisplayRenderContext ctx;
    ctx.framebuffer = epd_hl_get_framebuffer(&g_epd);
    ctx.width = width_;
    ctx.height = height_;

    const EpdRect panel = {
        .x = 0,
        .y = kStatusBarHeight,
        .width = ctx.width,
        .height = (ctx.height / 2) - kStatusBarHeight,
    };
    epd_fill_rect(panel, epd_gray(kWhite), ctx.framebuffer);
    epd_draw_rect(panel, epd_gray(kBlack), ctx.framebuffer);

    const int col_w = panel.width / 2;
    const int row_y = panel.y + 72;
    draw_text_20(ctx, "Quick", panel.x + 34, panel.y + 46, EPD_DRAW_ALIGN_LEFT);
    draw_text_12(ctx, "Wi-Fi", panel.x + 42, row_y + 44, EPD_DRAW_ALIGN_LEFT);
    draw_text_12(ctx, "Battery --%", panel.x + col_w + 42, row_y + 44, EPD_DRAW_ALIGN_LEFT);
    draw_text_12(ctx, "Refresh", panel.x + 42, row_y + 116, EPD_DRAW_ALIGN_LEFT);
    draw_text_12(ctx, "Sleep", panel.x + col_w + 42, row_y + 116, EPD_DRAW_ALIGN_LEFT);

    update_area(panel, MODE_GC16);
    Serial.println("[display] dropdown render done");
}

int Display::hit_test_home_app(const HomeViewModel& view_model, int x, int y) const
{
    return app_grid_hit_test(view_model, width_, height_, kStatusBarHeight + 28, x, y);
}

int Display::width() const
{
    return width_;
}

int Display::height() const
{
    return height_;
}

int Display::status_bar_height() const
{
    return kStatusBarHeight;
}

void Display::update_screen(enum EpdDrawMode mode)
{
    epd_poweron();
    log_draw_error(epd_hl_update_screen(&g_epd, mode, epd_ambient_temperature()));
    epd_poweroff();
}

void Display::update_area(EpdRect area, enum EpdDrawMode mode)
{
    Serial.printf("[display] dirty update requested x=%d y=%d w=%d h=%d\n", area.x, area.y, area.width, area.height);
    update_screen(mode);
}

void Display::clear_content_area(DisplayRenderContext ctx)
{
    epd_fill_rect(content_area(ctx.width, ctx.height), epd_gray(kWhite), ctx.framebuffer);
}

}  // namespace paper_screen
