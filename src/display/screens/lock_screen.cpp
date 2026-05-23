#include "lock_screen.h"

#include <epdiy.h>

#include "display/drawing.h"

namespace paper_screen {

void render_lock_screen(DisplayRenderContext ctx, const LockScreenViewModel& view_model)
{
    const int center_x = ctx.width / 2;
    const int center_y = ctx.height / 2;

    draw_text_20(ctx, view_model.message, center_x, center_y - 8, EPD_DRAW_ALIGN_CENTER);
}

}  // namespace paper_screen
