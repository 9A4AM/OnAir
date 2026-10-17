// The waterfall colour maps as pure functions: v in 0..1 to 0xAABBGGRR (opaque). Map numbers match Waterfall::Map. Tested in tests/app/test_plot_util.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cmap {
enum Map { Classic, Viridis, Inferno, Grey, Turbo, Cividis, NumMaps };

inline uint32_t stopsColor(const float (*s)[3], int n, float v) {   // n colour stops (0..255 per channel), blended linearly
    v *= (float)(n - 1);
    const int i = std::min(n - 2, (int)v);
    const float f = v - i;
    auto ch = [&](int k) { return (uint32_t)(s[i][k] + (s[i + 1][k] - s[i][k]) * f + 0.5f); };
    return 0xFF000000u | (ch(2) << 16) | (ch(1) << 8) | ch(0);
}
inline uint32_t color(int map, float v) {
    v = std::min(1.f, std::max(0.f, v));
    if (map == Turbo) {   // Turbo (Google's colour map), with the noise floor fading to black
        const float t = 0.12f + 0.84f * v;
        auto poly = [&](float a0, float a1, float a2, float a3, float a4, float a5) { return std::min(1.f, std::max(0.f, a0 + t * (a1 + t * (a2 + t * (a3 + t * (a4 + t * a5)))))); };
        const float r = poly(0.13572138f, 4.61539260f, -42.66032258f, 132.13108234f, -152.94239396f, 59.28637943f);
        const float g = poly(0.09140261f, 2.19418839f, 4.84296658f, -14.18503333f, 4.27729857f, 2.82956604f);
        const float b = poly(0.10667330f, 12.64194608f, -60.58204836f, 110.36276771f, -89.90310912f, 27.34824973f);
        const float f = std::min(1.f, v / 0.22f), k = f * f * (3 - 2 * f);   // smoothstep: black at the bottom
        return 0xFF000000u | ((uint32_t)(b * k * 255 + 0.5f) << 16) | ((uint32_t)(g * k * 255 + 0.5f) << 8) | (uint32_t)(r * k * 255 + 0.5f);
    }
    static const float cool[5][3] = {{6, 10, 20}, {16, 38, 70}, {30, 100, 140}, {110, 190, 205}, {240, 250, 250}};   // Classic: dark blue, teal, near white
    static const float grey[5][3] = {{0, 0, 0}, {34, 34, 33}, {96, 95, 92}, {170, 168, 162}, {244, 242, 236}};
    static const float viridis[9][3] = {{68, 1, 84}, {71, 44, 122}, {59, 81, 139}, {44, 113, 142}, {33, 144, 141}, {39, 173, 129}, {92, 200, 99}, {170, 220, 50}, {253, 231, 37}};
    static const float inferno[9][3] = {{0, 0, 4}, {31, 12, 72}, {85, 15, 109}, {136, 34, 106}, {186, 54, 85}, {227, 89, 51}, {249, 140, 10}, {249, 201, 50}, {252, 255, 164}};
    static const float cividis[9][3] = {{0, 32, 77}, {26, 52, 107}, {66, 78, 108}, {97, 104, 111}, {124, 123, 120}, {154, 145, 119}, {187, 167, 108}, {221, 193, 88}, {255, 233, 69}};
    switch (map) {
    case Cividis: return stopsColor(cividis, 9, v);
    case Viridis: return stopsColor(viridis, 9, v);
    case Inferno: return stopsColor(inferno, 9, v);
    case Grey: return stopsColor(grey, 5, v);
    default: return stopsColor(cool, 5, v);
    }
}


// Contrast: v ^ gamma (gamma > 1 darkens the weak signals, < 1 lifts them).
inline float applyGamma(float v, float gamma) { return gamma == 1.f ? v : std::pow(std::min(1.f, std::max(0.f, v)), gamma); }
} // namespace cmap
