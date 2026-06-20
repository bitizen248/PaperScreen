#include "heightmap.h"

#include <cmath>

namespace paper_screen {

namespace {

uint32_t splitmix32(uint32_t& state)
{
    uint32_t z = (state += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x21F0AAADu;
    z = (z ^ (z >> 15)) * 0x735A2D97u;
    return z ^ (z >> 15);
}

// Classic improved-Perlin-style 2D gradient noise with a seeded permutation
// table, so each seed produces a distinct but reproducible terrain.
class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed)
    {
        for (int i = 0; i < 256; ++i) {
            perm_[i] = static_cast<uint8_t>(i);
        }

        uint32_t state = seed != 0 ? seed : 0x9E3779B9u;
        for (int i = 255; i > 0; --i) {
            const int j = static_cast<int>(splitmix32(state) % static_cast<uint32_t>(i + 1));
            const uint8_t tmp = perm_[i];
            perm_[i] = perm_[j];
            perm_[j] = tmp;
        }
        for (int i = 0; i < 256; ++i) {
            perm_[256 + i] = perm_[i];
        }
    }

    // Returns gradient noise in roughly [-1, 1].
    float noise(float x, float y) const
    {
        const int xi = static_cast<int>(std::floor(x)) & 255;
        const int yi = static_cast<int>(std::floor(y)) & 255;
        const float xf = x - std::floor(x);
        const float yf = y - std::floor(y);

        const float u = fade(xf);
        const float v = fade(yf);

        const int aa = perm_[perm_[xi] + yi];
        const int ab = perm_[perm_[xi] + yi + 1];
        const int ba = perm_[perm_[xi + 1] + yi];
        const int bb = perm_[perm_[xi + 1] + yi + 1];

        const float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u);
        const float x2 = lerp(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u);
        return lerp(x1, x2, v);
    }

private:
    static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    static float lerp(float a, float b, float t) { return a + t * (b - a); }
    static float grad(int hash, float x, float y)
    {
        switch (hash & 7) {
        case 0: return x + y;
        case 1: return x - y;
        case 2: return -x + y;
        case 3: return -x - y;
        case 4: return x;
        case 5: return -x;
        case 6: return y;
        default: return -y;
        }
    }

    uint8_t perm_[512];
};

float fbm(const PerlinNoise& noise, float x, float y, int octaves, float persistence, float lacunarity)
{
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_amplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += noise.noise(x * frequency, y * frequency) * amplitude;
        max_amplitude += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return max_amplitude > 0.0f ? sum / max_amplitude : 0.0f;
}

// Ridged multifractal: folds noise around zero so ridgelines stay sharp
// instead of smoothing out the way plain fBm does.
float ridged_fbm(const PerlinNoise& noise, float x, float y, int octaves, float persistence, float lacunarity)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float max_amplitude = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const float n = 1.0f - std::fabs(noise.noise(x * frequency, y * frequency));
        sum += (n * n) * amplitude;
        max_amplitude += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return max_amplitude > 0.0f ? sum / max_amplitude : 0.0f;
}

uint8_t clamp_u8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return static_cast<uint8_t>(v);
}

float clamp01(float v)
{
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return v;
}

}  // namespace

void generate_heightmap(uint8_t* out, int width, int height, uint32_t seed, const HeightmapParams& params)
{
    if (out == nullptr || width <= 0 || height <= 0) {
        return;
    }

    const PerlinNoise shape_noise(seed);
    const PerlinNoise warp_noise_x(seed ^ 0xA53Fu);
    const PerlinNoise warp_noise_y(seed ^ 0x5C21u);

    const float aspect = static_cast<float>(height) / static_cast<float>(width);

    auto elevation_at = [&](int x, int y) {
        const float ny = (static_cast<float>(y) / static_cast<float>(height) - 0.5f) * params.base_frequency * aspect;
        const float nx = (static_cast<float>(x) / static_cast<float>(width) - 0.5f) * params.base_frequency;

        // Domain-warp the sampling coordinates with their own low-octave
        // noise field so coastlines/ridges bend instead of looking like a
        // raw noise grid (Inigo Quilez's warped-fbm technique).
        const float warp_x = fbm(warp_noise_x, nx, ny, 4, 0.5f, 2.0f);
        const float warp_y = fbm(warp_noise_y, nx, ny, 4, 0.5f, 2.0f);
        const float wx = nx + warp_x * params.warp_strength;
        const float wy = ny + warp_y * params.warp_strength;

        const float continent = fbm(shape_noise, wx, wy, params.octaves, params.persistence, params.lacunarity);
        const float ridges = ridged_fbm(shape_noise, wx * 1.7f, wy * 1.7f, params.octaves, params.persistence, params.lacunarity);
        return continent * (1.0f - params.ridge_mix) + ridges * params.ridge_mix;
    };

    // The warped/ridged blend has no fixed analytic range, so first sweep a
    // sparse grid to find the actual min/max for this seed before stretching
    // contrast over the full 0..255 output range.
    float lowest = 1e9f;
    float highest = -1e9f;
    constexpr int kSampleStride = 4;
    for (int y = 0; y < height; y += kSampleStride) {
        for (int x = 0; x < width; x += kSampleStride) {
            const float elevation = elevation_at(x, y);
            if (elevation < lowest) lowest = elevation;
            if (elevation > highest) highest = elevation;
        }
    }
    const float range = (highest - lowest) > 1e-4f ? (highest - lowest) : 1.0f;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float elevation = elevation_at(x, y);
            float normalized = clamp01((elevation - lowest) / range);
            normalized = std::pow(normalized, params.contrast);
            out[static_cast<size_t>(y) * width + x] = clamp_u8(normalized * 255.0f);
        }
    }
}

void shade_heightmap(const uint8_t* heights, uint8_t* shaded, int width, int height)
{
    if (heights == nullptr || shaded == nullptr || width <= 1 || height <= 1) {
        return;
    }

    constexpr float kLightX = -0.55f;
    constexpr float kLightY = -0.45f;
    constexpr float kLightZ = 0.7f;
    const float light_len = std::sqrt(kLightX * kLightX + kLightY * kLightY + kLightZ * kLightZ);
    const float lx = kLightX / light_len;
    const float ly = kLightY / light_len;
    const float lz = kLightZ / light_len;

    for (int y = 0; y < height; ++y) {
        const int y0 = y > 0 ? y - 1 : y;
        const int y1 = y < height - 1 ? y + 1 : y;
        for (int x = 0; x < width; ++x) {
            const int x0 = x > 0 ? x - 1 : x;
            const int x1 = x < width - 1 ? x + 1 : x;

            const float left = heights[static_cast<size_t>(y) * width + x0];
            const float right = heights[static_cast<size_t>(y) * width + x1];
            const float up = heights[static_cast<size_t>(y0) * width + x];
            const float down = heights[static_cast<size_t>(y1) * width + x];

            float nx = -(right - left) * 0.5f;
            float ny = -(down - up) * 0.5f;
            float nz = 64.0f;
            const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;

            float lambert = nx * lx + ny * ly + nz * lz;
            if (lambert < 0.0f) {
                lambert = 0.0f;
            }

            const float elevation = heights[static_cast<size_t>(y) * width + x] / 255.0f;
            const float ambient = 0.35f + 0.45f * elevation;
            float brightness = ambient + lambert * 0.55f;
            if (brightness > 1.0f) {
                brightness = 1.0f;
            }

            shaded[static_cast<size_t>(y) * width + x] = clamp_u8(brightness * 255.0f);
        }
    }
}

}  // namespace paper_screen
