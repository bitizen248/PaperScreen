#pragma once

#include <cstddef>
#include <cstdint>

#include "display/render_context.h"

namespace paper_screen {

enum class ImageFit {
    Contain,
};

bool render_png_image(DisplayRenderContext ctx, const uint8_t* data, size_t size, ImageFit fit = ImageFit::Contain);

}  // namespace paper_screen
