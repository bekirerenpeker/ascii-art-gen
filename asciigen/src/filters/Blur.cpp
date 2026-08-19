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

    const size_t n = (size_t)image.width * (size_t)image.height;
    std::vector<float> ch(n), soft(n);

    // One channel at a time: the kernel is separable and scalar, so there is
    // nothing to gain from interleaving, and this keeps the temporaries small.
    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < image.height; y++) {
            for (int x = 0; x < image.width; x++) {
                const PixelColor p = image.getAt(x, y);
                ch[(size_t)x + (size_t)y * image.width] = c == 0 ? p.r : (c == 1 ? p.g : p.b);
            }
        }

        blur(ch.data(), soft.data(), image.width, image.height, radius);

        for (int y = 0; y < image.height; y++) {
            for (int x = 0; x < image.width; x++) {
                PixelColor p = image.getAt(x, y);
                const byte v = (byte)std::clamp(std::lround(soft[(size_t)x + (size_t)y * image.width]), 0L, 255L);

                if (c == 0) p.r = v;
                else if (c == 1) p.g = v;
                else p.b = v;

                image.setAt(x, y, p);
            }
        }
    }
}

}   // namespace ImageFilters
