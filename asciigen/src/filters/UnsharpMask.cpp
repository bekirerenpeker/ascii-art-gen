#include "core/Profiler.hpp"
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
    ASCIIGEN_PROFILE("unsharpMask", "filter");


    const size_t n = (size_t)image.width * (size_t)image.height;
    std::vector<float> ch[3] = {std::vector<float>(n), std::vector<float>(n),
                                std::vector<float>(n)};
    std::vector<float> soft[3] = {std::vector<float>(n), std::vector<float>(n),
                                  std::vector<float>(n)};

    // Walked as a flat pointer rather than through getAt/setAt. A byte write may
    // alias anything, so the accessors force width/height/depth to be reloaded
    // after every pixel and the loop cannot vectorise; hoisting them here is what
    // makes the gather and scatter cheap next to the blur itself.
    const int d = image.depth;
    byte* const px = image.pixels;

    if (d >= 3) {
        const byte* p = px;
        for (size_t i = 0; i < n; i++, p += d) {
            ch[0][i] = p[0], ch[1][i] = p[1], ch[2][i] = p[2];
        }
    }
    else {
        const byte* p = px;
        for (size_t i = 0; i < n; i++, p += d) ch[0][i] = ch[1][i] = ch[2][i] = p[0];
    }

    for (int c = 0; c < 3; c++)
        blur(ch[c].data(), soft[c].data(), image.width, image.height, radius);

    // The blurred copy holds everything the blur did NOT remove, so the
    // difference is exactly the detail it destroyed. Adding that back on top is
    // what sharpens; the original is untouched where there was no detail to lose.
    if (d >= 3) {
        byte* p = px;
        for (size_t i = 0; i < n; i++, p += d) {
            p[0] = toByte(ch[0][i] + amount * (ch[0][i] - soft[0][i]));
            p[1] = toByte(ch[1][i] + amount * (ch[1][i] - soft[1][i]));
            p[2] = toByte(ch[2][i] + amount * (ch[2][i] - soft[2][i]));
        }
    }
    else {
        // Grey buffers keep setAt's collapse, so a 1 or 2 channel image sharpens
        // the same way it did before.
        byte* p = px;
        for (size_t i = 0; i < n; i++, p += d) {
            const int r = toByte(ch[0][i] + amount * (ch[0][i] - soft[0][i]));
            const int g = toByte(ch[1][i] + amount * (ch[1][i] - soft[1][i]));
            const int b = toByte(ch[2][i] + amount * (ch[2][i] - soft[2][i]));
            p[0] = byte((r * 299 + g * 587 + b * 114) / 1000);
        }
    }
}

}   // namespace ImageFilters
