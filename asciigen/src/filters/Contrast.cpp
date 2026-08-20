#include "filters/ImageFilters.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ImageFilters {

void contrast(Image& image, float amount)
{
    if (!image.pixels || amount < 0.f) return;

    uint8_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = (uint8_t)std::clamp(std::lround(((float)i - 128.f) * amount + 128.f), 0L, 255L);

    // See Levels.cpp: the accessors reload the struct after every byte written,
    // so the loop walks a raw pointer instead.
    const int d = image.depth;
    const size_t n = (size_t)image.width * (size_t)image.height;
    byte* p = image.pixels;

    if (d >= 3) {
        for (size_t i = 0; i < n; i++, p += d) {
            p[0] = lut[p[0]], p[1] = lut[p[1]], p[2] = lut[p[2]];
        }
    }
    else {
        for (size_t i = 0; i < n; i++, p += d) p[0] = lut[p[0]];
    }
}

}   // namespace ImageFilters
