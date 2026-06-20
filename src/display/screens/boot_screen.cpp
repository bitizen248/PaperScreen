#include "boot_screen.h"

#include <epdiy.h>

#include "display/drawing.h"

namespace paper_screen {

void render_boot_screen(DisplayRenderContext ctx, const char* message)
{
    const int center_x = ctx.width / 2;
    const int center_y = ctx.height / 2;

    EpdRect mark = {
        .x = center_x - 46,
        .y = center_y - 88,
        .width = 92,
        .height = 92,
    };
    epd_draw_rect(mark, kBlack, ctx.framebuffer);
    epd_fill_rect({mark.x + 18, mark.y + 18, 56, 12}, kBlack, ctx.framebuffer);
    epd_fill_rect({mark.x + 18, mark.y + 40, 56, 12}, kBlack, ctx.framebuffer);
    epd_fill_rect({mark.x + 18, mark.y + 62, 36, 12}, kBlack, ctx.framebuffer);

    draw_text_20(ctx, message, center_x, center_y + 48, EPD_DRAW_ALIGN_CENTER);
}

}  // namespace paper_screen
