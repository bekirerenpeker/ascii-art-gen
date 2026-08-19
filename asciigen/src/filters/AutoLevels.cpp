#include "filters/ImageFilters.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ImageFilters {

void autoLevels(Image& image, float lowPercent, float highPercent)
{
    if (!image.pixels || image.width <= 0 || image.height <= 0) return;
    if (highPercent <= lowPercent) return;

    long long hist[256] = {};
    const long long total = (long long)image.width * (long long)image.height;

    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            const PixelColor c = image.getAt(x, y);
            const long l = std::lround(0.299f * c.r + 0.587f * c.g + 0.114f * c.b);
            hist[std::clamp(l, 0L, 255L)]++;
        }
    }

    const long long loTarget = (long long)((double)total * lowPercent / 100.0);
    const long long hiTarget = (long long)((double)total * highPercent / 100.0);

    int lo = 0, hi = 255;
    long long run = 0;
    for (int i = 0; i < 256; i++) {
        run += hist[i];
        if (run >= loTarget) {
            lo = i;
            break;
        }
    }

    run = 0;
    for (int i = 0; i < 256; i++) {
        run += hist[i];
        if (run >= hiTarget) {
            hi = i;
            break;
        }
    }

    if (hi <= lo) return;

    // The same affine map on all three channels, so neutrals stay neutral and
    // the stretch cannot introduce a colour cast of its own.
    const float scale = 255.f / (float)(hi - lo);
    uint8_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = (uint8_t)std::clamp(std::lround((float)(i - lo) * scale), 0L, 255L);

    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            PixelColor c = image.getAt(x, y);
            c.r = lut[c.r], c.g = lut[c.g], c.b = lut[c.b];
            image.setAt(x, y, c);
        }
    }
}

}   // namespace ImageFilters
