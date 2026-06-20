#include "image_renderer.h"

#include <Arduino.h>
#include <cstring>
#include <cstdlib>

#include <epdiy.h>
#define LODEPNG_NO_COMPILE_CPP
extern "C" {
#include <extra/libs/png/lodepng.h>
}

#include "display/drawing.h"

namespace {

bool is_png(const uint8_t* data, size_t size)
{
    static const uint8_t kPngSignature[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    return data != nullptr && size >= sizeof(kPngSignature) &&
           std::memcmp(data, kPngSignature, sizeof(kPngSignature)) == 0;
}

bool is_bmp(const uint8_t* data, size_t size)
{
    return data != nullptr && size >= 2 && data[0] == 'B' && data[1] == 'M';
}

void log_image_signature(const uint8_t* data, size_t size)
{
    if (data == nullptr || size == 0) {
        Serial.println("[image] empty image buffer");
        return;
    }

    Serial.printf(
        "[image] signature bytes=%02X %02X %02X %02X %02X %02X %02X %02X size=%u\n",
        size > 0 ? data[0] : 0, size > 1 ? data[1] : 0, size > 2 ? data[2] : 0, size > 3 ? data[3] : 0,
        size > 4 ? data[4] : 0, size > 5 ? data[5] : 0, size > 6 ? data[6] : 0, size > 7 ? data[7] : 0,
        static_cast<unsigned>(size));
}

void log_png_error(unsigned error)
{
    Serial.printf("[image] png decode failed error=%u %s\n", error, lodepng_error_text(error));
}

uint8_t epaper_gray4(uint8_t value)
{
    if (value >= 248U) {
        return 0xFF;
    }

    const uint8_t darkened = static_cast<uint8_t>((static_cast<uint16_t>(value) * value + 127U) / 255U);
    const uint8_t level = static_cast<uint8_t>((static_cast<uint16_t>(darkened) * 15U + 127U) / 255U);
    return static_cast<uint8_t>(level * 17U);
}

struct RenderStats {
    uint32_t black = 0;
    uint32_t gray = 0;
    uint32_t white = 0;
};

void add_sample(RenderStats& stats, uint8_t value)
{
    if (value <= 8) {
        ++stats.black;
    } else if (value >= 247) {
        ++stats.white;
    } else {
        ++stats.gray;
    }
}

template <typename Sample>
bool render_gray(paper_screen::DisplayRenderContext ctx,
                 unsigned width,
                 unsigned height,
                 const char* format,
                 Sample sample)
{
    Serial.printf("[image] png decoded %u x %u %s\n", width, height, format);
    epd_fill_rect({0, 0, ctx.width, ctx.height}, paper_screen::epd_gray(paper_screen::kWhite), ctx.framebuffer);

    const bool fits_native = width <= static_cast<unsigned>(ctx.width) && height <= static_cast<unsigned>(ctx.height);
    int target_w = static_cast<int>(width);
    int target_h = static_cast<int>(height);
    if (!fits_native) {
        const uint32_t scale_x = (static_cast<uint32_t>(ctx.width) << 16) / width;
        const uint32_t scale_y = (static_cast<uint32_t>(ctx.height) << 16) / height;
        const uint32_t scale = scale_x < scale_y ? scale_x : scale_y;
        target_w = static_cast<int>((width * scale) >> 16);
        target_h = static_cast<int>((height * scale) >> 16);
    }
    const int offset_x = (ctx.width - target_w) / 2;
    const int offset_y = (ctx.height - target_h) / 2;

    Serial.printf("[image] target=%d x %d offset=%d,%d panel=%d x %d fit=%s\n",
                  target_w, target_h, offset_x, offset_y, ctx.width, ctx.height,
                  fits_native ? "native" : "downscale");

    RenderStats stats;
    for (int y = 0; y < target_h; ++y) {
        const unsigned src_y = static_cast<unsigned>((static_cast<uint64_t>(y) * height) / target_h);
        for (int x = 0; x < target_w; ++x) {
            const unsigned src_x = static_cast<unsigned>((static_cast<uint64_t>(x) * width) / target_w);
            const uint8_t gray = sample(src_x, src_y);
            add_sample(stats, gray);
            epd_draw_pixel(offset_x + x,
                           offset_y + y,
                           epaper_gray4(gray),
                           ctx.framebuffer);
        }
    }

    epd_draw_rect({offset_x, offset_y, target_w, target_h}, paper_screen::epd_gray(paper_screen::kBlack), ctx.framebuffer);
    Serial.printf("[image] samples black=%lu gray=%lu white=%lu\n",
                  static_cast<unsigned long>(stats.black),
                  static_cast<unsigned long>(stats.gray),
                  static_cast<unsigned long>(stats.white));
    return true;
}

uint8_t rgba_luminance(const unsigned char* rgba, unsigned width, unsigned x, unsigned y)
{
    const size_t offset = (static_cast<size_t>(y) * width + x) * 4U;
    const uint32_t r = rgba[offset + 0];
    const uint32_t g = rgba[offset + 1];
    const uint32_t b = rgba[offset + 2];
    const uint32_t a = rgba[offset + 3];
    const uint32_t luma = (r * 299U + g * 587U + b * 114U + 500U) / 1000U;

    if (a == 255U) {
        return static_cast<uint8_t>(luma);
    }

    return static_cast<uint8_t>((luma * a + 255U * (255U - a) + 127U) / 255U);
}

uint32_t fnv1a_mix(uint32_t hash, uint8_t value)
{
    hash ^= value;
    hash *= 16777619UL;
    return hash;
}

}  // namespace

namespace paper_screen {

bool render_png_image(DisplayRenderContext ctx, const uint8_t* data, size_t size, ImageFit)
{
    if (ctx.framebuffer == nullptr || data == nullptr || size == 0) {
        Serial.println("[image] render skipped: framebuffer or data missing");
        return false;
    }

    log_image_signature(data, size);

    if (is_bmp(data, size)) {
        Serial.println("[image] unsupported format: BMP detected, renderer currently supports PNG only");
        return false;
    }

    if (!is_png(data, size)) {
        Serial.println("[image] unsupported format: payload is neither PNG nor BMP");
        return false;
    }

    unsigned char* rgba = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    const unsigned error = lodepng_decode_memory(&rgba, &width, &height, data, size, LCT_RGBA, 8);
    if (error != 0 || rgba == nullptr || width == 0 || height == 0) {
        log_png_error(error);
        free(rgba);
        return false;
    }

    Serial.printf("[image] rgba head=%u,%u,%u,%u %u,%u,%u,%u\n",
                  rgba[0], rgba[1], rgba[2], rgba[3],
                  rgba[4], rgba[5], rgba[6], rgba[7]);
    const bool rendered = render_gray(ctx, width, height, "rgba8", [rgba, width](unsigned x, unsigned y) {
        return rgba_luminance(rgba, width, x, y);
    });
    free(rgba);
    return rendered;
}

uint32_t png_visual_hash(const uint8_t* data, size_t size)
{
    if (!is_png(data, size)) {
        return 0;
    }

    unsigned char* rgba = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    const unsigned error = lodepng_decode_memory(&rgba, &width, &height, data, size, LCT_RGBA, 8);
    if (error != 0 || rgba == nullptr || width == 0 || height == 0) {
        free(rgba);
        return 0;
    }

    constexpr unsigned kHashWidth = 48;
    constexpr unsigned kHashHeight = 27;
    uint32_t hash = 2166136261UL;
    for (unsigned y = 0; y < kHashHeight; ++y) {
        const unsigned src_y = (static_cast<uint64_t>(y) * height) / kHashHeight;
        for (unsigned x = 0; x < kHashWidth; ++x) {
            const unsigned src_x = (static_cast<uint64_t>(x) * width) / kHashWidth;
            const uint8_t gray = rgba_luminance(rgba, width, src_x, src_y);
            hash = fnv1a_mix(hash, static_cast<uint8_t>(gray / 16U));
        }
    }

    free(rgba);
    return hash == 0 ? 1 : hash;
}

}  // namespace paper_screen
