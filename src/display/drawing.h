#pragma once

#include <epdiy.h>

#include "display/render_context.h"

namespace paper_screen {

enum Gray4 : uint8_t {
    kBlack = 0,
    kDarkGray = 4,
    kMidGray = 8,
    kLightGray = 12,
    kWhite = 15,
};

constexpr uint8_t epd_gray(Gray4 color)
{
    return static_cast<uint8_t>(color) * 17;
}

void draw_text_12(DisplayRenderContext ctx, const char* text, int x, int y, EpdFontFlags flags);
void draw_text_20(DisplayRenderContext ctx, const char* text, int x, int y, EpdFontFlags flags);
void draw_text_12(DisplayRenderContext ctx, const char* text, int x, int y, EpdFontFlags flags, Gray4 fg, Gray4 bg);
void draw_text_20(DisplayRenderContext ctx, const char* text, int x, int y, EpdFontFlags flags, Gray4 fg, Gray4 bg);
void draw_label_centered(DisplayRenderContext ctx, const char* text, EpdRect rect);

}  // namespace paper_screen
