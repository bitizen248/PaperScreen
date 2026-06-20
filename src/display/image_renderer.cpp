#include "image_renderer.h"

#include <Arduino.h>
#include <cstring>
#include <cstdlib>

#include <epdiy.h>
#include <esp_heap_caps.h>
#define LODEPNG_NO_COMPILE_CPP
extern "C" {
#include <extra/libs/png/lodepng.h>
}
#include <JPEGDEC.h>

#include "display/drawing.h"

namespace {

struct JpegGrayContext {
    uint8_t* buf;
    int width;
    int height;
};

static JpegGrayContext* g_jpeg_ctx = nullptr;

static int jpeg_gray_cb(JPEGDRAW* d)
{
    if (!g_jpeg_ctx) return 0;
    const auto* src = reinterpret_cast<const uint8_t*>(d->pPixels);
    for (int row = 0; row < d->iHeight; ++row) {
        int y = d->y + row;
        if (y < 0 || y >= g_jpeg_ctx->height) continue;
        for (int col = 0; col < d->iWidth; ++col) {
            int x = d->x + col;
            if (x < 0 || x >= g_jpeg_ctx->width) continue;
            g_jpeg_ctx->buf[y * g_jpeg_ctx->width + x] = src[row * d->iWidth + col];
        }
    }
    return 1;
}

bool decode_jpeg(const uint8_t* data, size_t size,
                 uint8_t** out_gray, int* out_width, int* out_height)
{
    JPEGDEC jpeg;
    if (!jpeg.openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), jpeg_gray_cb)) {
        Serial.printf("[image] jpeg open failed\n");
        return false;
    }

    const int w = jpeg.getWidth();
    const int h = jpeg.getHeight();
    jpeg.close();

    if (w <= 0 || h <= 0) {
        Serial.printf("[image] jpeg bad dimensions %d x %d\n", w, h);
        return false;
    }

    const size_t buf_bytes = static_cast<size_t>(w) * static_cast<size_t>(h);
    uint8_t* buf = static_cast<uint8_t*>(
        heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf) {
        buf = static_cast<uint8_t*>(malloc(buf_bytes));
    }
    if (!buf) {
        Serial.printf("[image] jpeg alloc failed %u bytes\n", static_cast<unsigned>(buf_bytes));
        return false;
    }

    JpegGrayContext ctx = {buf, w, h};
    g_jpeg_ctx = &ctx;

    JPEGDEC jpeg2;
    if (!jpeg2.openRAM(const_cast<uint8_t*>(data), static_cast<int>(size), jpeg_gray_cb)) {
        g_jpeg_ctx = nullptr;
        heap_caps_free(buf);
        return false;
    }
    jpeg2.setPixelType(EIGHT_BIT_GRAYSCALE);
    const bool ok = (jpeg2.decode(0, 0, 0) == 1);
    jpeg2.close();
    g_jpeg_ctx = nullptr;

    if (!ok) {
        Serial.printf("[image] jpeg decode failed\n");
        heap_caps_free(buf);
        return false;
    }

    *out_gray = buf;
    *out_width = w;
    *out_height = h;
    return true;
}

bool is_jpeg(const uint8_t* data, size_t size)
{
    return data != nullptr && size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

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
                           paper_screen::quantize_gray4(gray),
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

bool render_image(DisplayRenderContext ctx, const uint8_t* data, size_t size, ImageFit fit)
{
    if (ctx.framebuffer == nullptr || data == nullptr || size == 0) {
        Serial.println("[image] render skipped: framebuffer or data missing");
        return false;
    }

    log_image_signature(data, size);

    if (is_jpeg(data, size)) {
        uint8_t* gray = nullptr;
        int w = 0;
        int h = 0;
        if (!decode_jpeg(data, size, &gray, &w, &h)) {
            Serial.println("[image] jpeg decode failed");
            return false;
        }
        const bool rendered = render_gray(ctx, static_cast<unsigned>(w), static_cast<unsigned>(h),
                                          "jpeg-gray8",
                                          [gray, w](unsigned x, unsigned y) -> uint8_t {
                                              return gray[static_cast<size_t>(y) * static_cast<size_t>(w) + x];
                                          });
        heap_caps_free(gray);
        return rendered;
    }

    return render_png_image(ctx, data, size, fit);
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
