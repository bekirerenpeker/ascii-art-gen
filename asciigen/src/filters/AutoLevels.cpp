#include "core/Profiler.hpp"
#include "filters/ImageFilters.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ImageFilters {

void autoLevels(Image& image, float lowPercent, float highPercent)
{
    if (!image.pixels || image.width <= 0 || image.height <= 0) return;
    if (highPercent <= lowPercent) return;
    ASCIIGEN_PROFILE("autoLevels", "filter");


    long long hist[256] = {};
    const long long total = (long long)image.width * (long long)image.height;

    // Raw pointer rather than getAt: the accessor's per-pixel bounds check and
    // depth branch both have to be redone from memory every iteration, because a
    // byte pointer may alias the very fields it is testing.
    const int d = image.depth;
    const size_t n = (size_t)image.width * (size_t)image.height;
    const byte* p = image.pixels;

    if (d >= 3) {
        for (size_t i = 0; i < n; i++, p += d) {
            const long l = std::lround(0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]);
            hist[std::clamp(l, 0L, 255L)]++;
        }
    }
    else {
        for (size_t i = 0; i < n; i++, p += d) hist[p[0]]++;
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

    byte* q = image.pixels;

    if (d >= 3) {
        for (size_t i = 0; i < n; i++, q += d) {
            q[0] = lut[q[0]], q[1] = lut[q[1]], q[2] = lut[q[2]];
        }
    }
    else {
        for (size_t i = 0; i < n; i++, q += d) q[0] = lut[q[0]];
    }
}

}   // namespace ImageFilters
