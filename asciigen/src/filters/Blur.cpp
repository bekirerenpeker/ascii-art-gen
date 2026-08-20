#include "core/Profiler.hpp"
#include "filters/ImageFilters.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace ImageFilters {

void blur(const float* src, float* dst, int w, int h, int radius)
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

void blur(Image& image, int radius)
{
    if (!image.pixels || radius < 1 || image.width <= 0 || image.height <= 0) return;
    ASCIIGEN_PROFILE("blur(Image)", "filter");


    const size_t n = (size_t)image.width * (size_t)image.height;
    std::vector<float> ch(n), soft(n);

    // Hoisted out of the loops on purpose: byte writes may alias anything, so
    // going through getAt/setAt would reload width/height/depth after every
    // pixel and leave the gather and scatter costing more than the blur.
    const int d = image.depth;
    byte* const px = image.pixels;

    // Grey buffers hold one value that setAt would recompute from all three
    // channels, so blurring channel 0 alone is the whole job.
    const int channels = d >= 3 ? 3 : 1;

    // One channel at a time: the kernel is separable and scalar, so there is
    // nothing to gain from interleaving, and this keeps the temporaries small.
    for (int c = 0; c < channels; c++) {
        const byte* src = px + c;
        for (size_t i = 0; i < n; i++, src += d) ch[i] = *src;

        blur(ch.data(), soft.data(), image.width, image.height, radius);

        byte* dst = px + c;
        for (size_t i = 0; i < n; i++, dst += d)
            *dst = (byte)std::clamp(std::lround(soft[i]), 0L, 255L);
    }
}

}   // namespace ImageFilters
