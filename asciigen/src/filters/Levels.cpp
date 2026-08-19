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

    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            PixelColor c = image.getAt(x, y);
            c.r = lut[c.r], c.g = lut[c.g], c.b = lut[c.b];
            image.setAt(x, y, c);
        }
    }
}

}   // namespace ImageFilters
