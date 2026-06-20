#pragma once

#include "display/render_context.h"
#include "ui/home_screen.h"

namespace paper_screen {

constexpr int kStatusBarHeight = 64;

void render_status_bar(DisplayRenderContext ctx, const StatusBarViewModel& model);

}  // namespace paper_screen
