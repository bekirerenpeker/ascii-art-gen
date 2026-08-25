#include "filters/ImageFilters.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ImageFilters {

void levels(Image& image, float blackPoint, float whitePoint, float gamma)
{
    if (!image.pixels) return;
    if (whitePoint <= blackPoint || gamma <= 0.f) return;

    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        const float t = std::clamp(((float)i - blackPoint) / (whitePoint - blackPoint), 0.f, 1.f);
        lut[i] = (uint8_t)std::clamp(std::lround(255.f * std::pow(t, 1.f / gamma)), 0L, 255L);
    }

    // Flat pointer walk instead of getAt/setAt: a byte store may alias the
    // struct's own fields, so the accessors reload width/height/depth every
    // pixel. On a grey buffer the lut lands on channel 0 only -- setAt would have
    // recombined the three identical results back into the same value anyway.
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
