#pragma once

#include <stdint.h>

namespace paper_screen {

struct DisplayRenderContext {
    uint8_t* framebuffer = nullptr;
    int width = 0;
    int height = 0;
};

}  // namespace paper_screen
