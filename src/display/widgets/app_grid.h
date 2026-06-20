#pragma once

#include "display/render_context.h"
#include "ui/home_screen.h"

namespace paper_screen {

void render_app_grid(DisplayRenderContext ctx, const HomeViewModel& model, int top);
int app_grid_hit_test(const HomeViewModel& model, int width, int height, int top, int x, int y);

}  // namespace paper_screen
