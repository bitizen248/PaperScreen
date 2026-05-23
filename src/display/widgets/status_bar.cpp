#include "status_bar.h"

#include <epdiy.h>

#include "display/drawing.h"

namespace paper_screen {

void render_status_bar(DisplayRenderContext ctx, const StatusBarViewModel& model)
{
    constexpr int title_x = 24;
    constexpr int text_baseline_y = 48;
    constexpr int right_margin = 26;

    epd_fill_rect({0, 0, ctx.width, kStatusBarHeight}, epd_gray(kBlack), ctx.framebuffer);
    epd_draw_line(0, kStatusBarHeight - 1, ctx.width, kStatusBarHeight - 1, epd_gray(kWhite), ctx.framebuffer);

    draw_text_12(ctx, model.title, title_x, text_baseline_y, EPD_DRAW_ALIGN_LEFT, kWhite, kBlack);
    draw_text_12(ctx, model.time, ctx.width / 2, text_baseline_y, EPD_DRAW_ALIGN_CENTER, kWhite, kBlack);
    draw_text_12(ctx, model.battery, ctx.width - right_margin, text_baseline_y, EPD_DRAW_ALIGN_RIGHT, kWhite, kBlack);
}

}  // namespace paper_screen
