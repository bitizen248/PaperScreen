#include "drawing.h"

#include "fonts/firasans_12.h"
#include "fonts/firasans_20.h"

namespace paper_screen {

namespace {

void draw_text(
    DisplayRenderContext ctx,
    const EpdFont* font,
    const char* text,
    int x,
    int y,
    EpdFontFlags flags,
    Gray4 fg,
    Gray4 bg
)
{
    if (ctx.framebuffer == nullptr || text == nullptr) {
        return;
    }

    EpdFontProperties props = epd_font_properties_default();
    props.flags = flags;
    props.fg_color = fg;
    props.bg_color = bg;

    epd_write_string(font, text, &x, &y, ctx.framebuffer, &props);
}

}  // namespace

uint8_t quantize_gray4(uint8_t luma)
{
    if (luma >= 248U) {
        return 0xFF;
    }

    const uint8_t darkened = static_cast<uint8_t>((static_cast<uint16_t>(luma) * luma + 127U) / 255U);
    const uint8_t level = static_cast<uint8_t>((static_cast<uint16_t>(darkened) * 15U + 127U) / 255U);
    return static_cast<uint8_t>(level * 17U);
}

void draw_text_12(DisplayRenderContext ctx, const char* text, int x, int y, EpdFontFlags flags)
{
    draw_text(ctx, &FiraSans_12, text, x, y, flags, kBlack, kWhite);
}

void draw_text_20(DisplayRenderContext ctx, const char* text, int x, int y, EpdFontFlags flags)
{
    draw_text(ctx, &FiraSans_20, text, x, y, flags, kBlack, kWhite);
}

void draw_text_12(DisplayRenderContext ctx, const char* text, int x, int y, EpdFontFlags flags, Gray4 fg, Gray4 bg)
{
    draw_text(ctx, &FiraSans_12, text, x, y, flags, fg, bg);
}

void draw_text_20(DisplayRenderContext ctx, const char* text, int x, int y, EpdFontFlags flags, Gray4 fg, Gray4 bg)
{
    draw_text(ctx, &FiraSans_20, text, x, y, flags, fg, bg);
}

void draw_label_centered(DisplayRenderContext ctx, const char* text, EpdRect rect)
{
    const int x = rect.x + rect.width / 2;
    const int y = rect.y + rect.height / 2 + 7;
    draw_text_12(ctx, text, x, y, EPD_DRAW_ALIGN_CENTER);
}

}  // namespace paper_screen
