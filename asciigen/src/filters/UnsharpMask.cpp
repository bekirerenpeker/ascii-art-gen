#include "filters/ImageFilters.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace ImageFilters {

static byte toByte(float v) { return (byte)std::clamp(std::lround(v), 0L, 255L); }

void unsharpMask(Image& image, float amount, int radius)
{
    if (!image.pixels || amount <= 0.f || radius < 1) return;
    if (image.width <= 0 || image.height <= 0) return;

    const size_t n = (size_t)image.width * (size_t)image.height;
    std::vector<float> ch[3] = {std::vector<float>(n), std::vector<float>(n),
                                std::vector<float>(n)};
    std::vector<float> soft[3] = {std::vector<float>(n), std::vector<float>(n),
                                  std::vector<float>(n)};

    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            const PixelColor c = image.getAt(x, y);
            const size_t i = (size_t)x + (size_t)y * image.width;
            ch[0][i] = c.r, ch[1][i] = c.g, ch[2][i] = c.b;
        }
    }

    for (int c = 0; c < 3; c++)
        blur(ch[c].data(), soft[c].data(), image.width, image.height, radius);

    // The blurred copy holds everything the blur did NOT remove, so the
    // difference is exactly the detail it destroyed. Adding that back on top is
    // what sharpens; the original is untouched where there was no detail to lose.
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            const size_t i = (size_t)x + (size_t)y * image.width;
            PixelColor c = image.getAt(x, y);

            c.r = toByte(ch[0][i] + amount * (ch[0][i] - soft[0][i]));
            c.g = toByte(ch[1][i] + amount * (ch[1][i] - soft[1][i]));
            c.b = toByte(ch[2][i] + amount * (ch[2][i] - soft[2][i]));

            image.setAt(x, y, c);
        }
    }
}

}   // namespace ImageFilters
