#include "filters/Blur.hpp"
#include <algorithm>
#include <vector>

namespace Blur {

void box(const float* src, float* dst, int w, int h, int radius)
{
    if (!src || !dst || w <= 0 || h <= 0) return;

    if (radius < 1) {
        std::copy(src, src + (size_t)w * (size_t)h, dst);
        return;
    }

    const float norm = 1.f / (2 * radius + 1);
    std::vector<float> tmp((size_t)w * (size_t)h);

    // Separable, horizontal then vertical. Borders clamp rather than wrap, so a
    // stroke touching one edge of a glyph cannot bleed round to the opposite one.
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float sum = 0.f;
            for (int k = -radius; k <= radius; k++) sum += src[std::clamp(x + k, 0, w - 1) + y * w];
            tmp[(size_t)(x + y * w)] = sum * norm;
        }
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float sum = 0.f;
            for (int k = -radius; k <= radius; k++)
                sum += tmp[(size_t)(x + std::clamp(y + k, 0, h - 1) * w)];
            dst[(size_t)(x + y * w)] = sum * norm;
        }
    }
}

}   // namespace Blur
