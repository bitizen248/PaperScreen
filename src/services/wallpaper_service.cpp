#include "wallpaper_service.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "util/heightmap.h"

namespace paper_screen {

namespace {

uint8_t* alloc_buffer(size_t size)
{
    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    }
    return buffer;
}

}  // namespace

WallpaperService::~WallpaperService()
{
    release();
}

bool WallpaperService::generate(int width, int height, uint32_t seed)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    const size_t needed = static_cast<size_t>(width) * static_cast<size_t>(height);

    uint8_t* heights = alloc_buffer(needed);
    if (heights == nullptr) {
        Serial.println("[wallpaper] heightmap scratch allocation failed");
        return false;
    }

    if (shaded_ == nullptr || capacity_ != needed) {
        release();
        shaded_ = alloc_buffer(needed);
        if (shaded_ == nullptr) {
            Serial.println("[wallpaper] shaded buffer allocation failed");
            heap_caps_free(heights);
            return false;
        }
        capacity_ = needed;
    }

    Serial.printf("[wallpaper] generating %dx%d seed=%lu\n", width, height, static_cast<unsigned long>(seed));
    const HeightmapParams params;
    generate_heightmap(heights, width, height, seed, params);
    shade_heightmap(heights, shaded_, width, height);
    heap_caps_free(heights);

    width_ = width;
    height_ = height;
    Serial.println("[wallpaper] generation done");
    return true;
}

void WallpaperService::release()
{
    if (shaded_ != nullptr) {
        heap_caps_free(shaded_);
        shaded_ = nullptr;
    }
    capacity_ = 0;
}

}  // namespace paper_screen
