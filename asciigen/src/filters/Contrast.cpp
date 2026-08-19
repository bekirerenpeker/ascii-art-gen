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

    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            PixelColor c = image.getAt(x, y);
            c.r = lut[c.r], c.g = lut[c.g], c.b = lut[c.b];
            image.setAt(x, y, c);
        }
    }
}

}   // namespace ImageFilters
