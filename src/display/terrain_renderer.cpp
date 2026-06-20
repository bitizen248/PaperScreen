#include "terrain_renderer.h"

#include <epdiy.h>

#include "display/drawing.h"

namespace paper_screen {

void render_heightmap_region(DisplayRenderContext ctx, const uint8_t* image, int map_width, int map_height, int top)
{
    if (ctx.framebuffer == nullptr || image == nullptr || map_width <= 0 || map_height <= 0) {
        return;
    }

    const int target_w = ctx.width;
    const int target_h = ctx.height - top;
    if (target_w <= 0 || target_h <= 0) {
        return;
    }

    for (int y = 0; y < target_h; ++y) {
        const int src_y = (y * map_height) / target_h;
        const uint8_t* row = image + static_cast<size_t>(src_y) * map_width;
        for (int x = 0; x < target_w; ++x) {
            const int src_x = (x * map_width) / target_w;
            epd_draw_pixel(x, top + y, quantize_gray4(row[src_x]), ctx.framebuffer);
        }
    }
}

}  // namespace paper_screen
