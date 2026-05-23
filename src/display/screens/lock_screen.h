#pragma once

#include "display/render_context.h"
#include "ui/lock_screen.h"

namespace paper_screen {

void render_lock_screen(DisplayRenderContext ctx, const LockScreenViewModel& view_model);

}  // namespace paper_screen
