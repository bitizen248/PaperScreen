#include "lock_screen.h"

#include <epdiy.h>

#include "display/drawing.h"

namespace paper_screen {

void render_lock_screen(DisplayRenderContext ctx, const LockScreenViewModel& view_model)
{
    const int center_x = ctx.width / 2;
    const int center_y = ctx.height / 2;
    const EpdRect wake_label = {72, ctx.height - 112, 190, 58};
    const int triangle_center_y = center_y + 40;

    draw_text_20(ctx, view_model.time, center_x, center_y - 80, EPD_DRAW_ALIGN_CENTER);
    draw_text_12(ctx, view_model.battery, center_x, center_y - 38, EPD_DRAW_ALIGN_CENTER);
    draw_text_20(ctx, view_model.message, center_x, center_y + 10, EPD_DRAW_ALIGN_CENTER);

    epd_fill_triangle(0, triangle_center_y,
                      48, triangle_center_y - 30,
                      48, triangle_center_y + 30,
                      epd_gray(kBlack), ctx.framebuffer);
    draw_text_12(ctx, view_model.wake_hint, wake_label.x + wake_label.width / 2, wake_label.y + 38, EPD_DRAW_ALIGN_CENTER);
}

}  // namespace paper_screen
