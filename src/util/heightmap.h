#pragma once

#include <cstdint>

namespace paper_screen {

struct HeightmapParams {
    int octaves = 6;
    float persistence = 0.5f;
    float lacunarity = 2.05f;
    float base_frequency = 3.2f;
    float warp_strength = 0.6f;
    float ridge_mix = 0.55f;
    float contrast = 1.35f;
};

// Fills `out` (row-major, width*height bytes) with a normalized 0..255
// elevation field built from layered, domain-warped gradient noise.
void generate_heightmap(uint8_t* out, int width, int height, uint32_t seed, const HeightmapParams& params);

// Converts a raw elevation field into a relief-shaded grayscale image
// (slope-based hillshade blended with elevation) ready for e-paper display.
void shade_heightmap(const uint8_t* heights, uint8_t* shaded, int width, int height);

}  // namespace paper_screen
