#include "app_grid.h"

#include <epdiy.h>

#include "display/drawing.h"

namespace paper_screen {

namespace {

constexpr int kAppGridColumns = 4;
constexpr int kAppGridRows = 2;
constexpr int kAppGridGapX = 22;
constexpr int kAppGridGapY = 26;
constexpr int kAppGridMarginX = 34;
constexpr int kAppGridMarginBottom = 30;

EpdRect app_tile_rect(int width, int height, int top, int index)
{
    const int tile_w = (width - (kAppGridMarginX * 2) - (kAppGridGapX * (kAppGridColumns - 1))) / kAppGridColumns;
    const int available_h = height - top - kAppGridMarginBottom;
    const int tile_h = (available_h - kAppGridGapY * (kAppGridRows - 1)) / kAppGridRows;
    const int row = index / kAppGridColumns;
    const int col = index % kAppGridColumns;

    return {
        .x = kAppGridMarginX + col * (tile_w + kAppGridGapX),
        .y = top + row * (tile_h + kAppGridGapY),
        .width = tile_w,
        .height = tile_h,
    };
}

bool contains(EpdRect rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

void draw_icon(DisplayRenderContext ctx, AppIcon icon, EpdRect rect)
{
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + (rect.height * 42) / 100;

    switch (icon) {
    case AppIcon::Tasks:
        epd_draw_rect({cx - 27, cy - 24, 54, 54}, kBlack, ctx.framebuffer);
        epd_draw_line(cx - 18, cy - 2, cx - 6, cy + 12, kBlack, ctx.framebuffer);
        epd_draw_line(cx - 6, cy + 12, cx + 20, cy - 16, kBlack, ctx.framebuffer);
        break;
    case AppIcon::Reader:
        epd_draw_rect({cx - 42, cy - 24, 38, 58}, kBlack, ctx.framebuffer);
        epd_draw_rect({cx + 4, cy - 24, 38, 58}, kBlack, ctx.framebuffer);
        epd_draw_line(cx, cy - 20, cx, cy + 36, kBlack, ctx.framebuffer);
        break;
    case AppIcon::Focus:
    case AppIcon::Timer:
        epd_draw_circle(cx, cy, 32, kBlack, ctx.framebuffer);
        epd_draw_line(cx, cy, cx, cy - 22, kBlack, ctx.framebuffer);
        epd_draw_line(cx, cy, cx + 18, cy + 12, kBlack, ctx.framebuffer);
        break;
    case AppIcon::Trmnl:
        epd_draw_rect({cx - 40, cy - 28, 80, 56}, kBlack, ctx.framebuffer);
        epd_draw_line(cx - 14, cy - 28, cx - 14, cy + 28, kBlack, ctx.framebuffer);
        epd_draw_line(cx + 14, cy - 28, cx + 14, cy + 28, kBlack, ctx.framebuffer);
        epd_draw_line(cx - 40, cy, cx + 40, cy, kBlack, ctx.framebuffer);
        break;
    case AppIcon::Settings:
        epd_draw_circle(cx, cy, 30, kBlack, ctx.framebuffer);
        epd_draw_circle(cx, cy, 11, kBlack, ctx.framebuffer);
        epd_draw_line(cx - 42, cy, cx + 42, cy, kBlack, ctx.framebuffer);
        epd_draw_line(cx, cy - 42, cx, cy + 42, kBlack, ctx.framebuffer);
        break;
    case AppIcon::Sync:
        epd_draw_line(cx - 38, cy - 12, cx + 28, cy - 12, kBlack, ctx.framebuffer);
        epd_draw_triangle(cx + 28, cy - 26, cx + 28, cy + 2, cx + 44, cy - 12, kBlack, ctx.framebuffer);
        epd_draw_line(cx + 38, cy + 16, cx - 28, cy + 16, kBlack, ctx.framebuffer);
        epd_draw_triangle(cx - 28, cy + 2, cx - 28, cy + 30, cx - 44, cy + 16, kBlack, ctx.framebuffer);
        break;
    case AppIcon::Notes:
        epd_draw_rect({cx - 30, cy - 34, 60, 68}, kBlack, ctx.framebuffer);
        epd_draw_line(cx - 18, cy - 12, cx + 18, cy - 12, kBlack, ctx.framebuffer);
        epd_draw_line(cx - 18, cy + 8, cx + 18, cy + 8, kBlack, ctx.framebuffer);
        break;
    }
}

void render_tile(DisplayRenderContext ctx, const AppTileViewModel& app, EpdRect rect)
{
    epd_draw_rect(rect, kBlack, ctx.framebuffer);
    draw_icon(ctx, app.icon, rect);
    draw_label_centered(ctx, app.label, {rect.x, rect.y + rect.height - 46, rect.width, 36});
}

}  // namespace

void render_app_grid(DisplayRenderContext ctx, const HomeViewModel& model, int top)
{
    for (int i = 0; i < model.app_count; ++i) {
        if (i / kAppGridColumns >= kAppGridRows) {
            break;
        }
        EpdRect tile = app_tile_rect(ctx.width, ctx.height, top, i);
        render_tile(ctx, model.apps[i], tile);
    }
}

int app_grid_hit_test(const HomeViewModel& model, int width, int height, int top, int x, int y)
{
    for (int i = 0; i < model.app_count; ++i) {
        if (i / kAppGridColumns >= kAppGridRows) {
            break;
        }
        if (contains(app_tile_rect(width, height, top, i), x, y)) {
            return i;
        }
    }

    return -1;
}

}  // namespace paper_screen
