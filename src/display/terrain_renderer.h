#pragma once

#include <cstdint>

#include "display/render_context.h"

namespace paper_screen {

// Blits a grayscale heightmap (row-major, map_width x map_height bytes) into
// the framebuffer region [0, top) .. (ctx.width, ctx.height), scaling with
// nearest-neighbor sampling if the buffer doesn't already match the target.
void render_heightmap_region(DisplayRenderContext ctx, const uint8_t* image, int map_width, int map_height, int top);

}  // namespace paper_screen
