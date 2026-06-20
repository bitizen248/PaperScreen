#pragma once

#include <cstddef>
#include <cstdint>

namespace paper_screen {

// Owns the generated home-screen wallpaper buffer (a relief-shaded
// heightmap). Generation is synchronous and allocates from PSRAM when
// available; callers should trigger it during a moment the UI already
// expects a delay (boot, or an explicit "regenerate" action).
class WallpaperService {
public:
    ~WallpaperService();

    bool generate(int width, int height, uint32_t seed);

    const uint8_t* data() const { return shaded_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool ready() const { return shaded_ != nullptr; }

private:
    void release();

    uint8_t* shaded_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    size_t capacity_ = 0;
};

}  // namespace paper_screen
