#include "dithering/Bayer4.hpp"
#include <algorithm>
#include <cmath>

namespace Dithering {

void applyBayer4(Image& image, int levels, int blockW, int blockH, const ContrastField& gate)
{
    if (!image.pixels || levels < 2 || blockW < 1 || blockH < 1) return;

    // Ordered 4x4 Bayer matrix. Values 0-15 spread as evenly as possible so the
    // added bias averages to zero over any 4x4 block.
    static const int kBayer[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5},
    };

    // Bias only, no snapping: whatever consumes this plane -- glyph selection,
    // palette mapping -- is the quantiser. Rounding here would throw the tone
    // away before it ever reached the step being smoothed over. `levels` is how
    // many levels that consumer can resolve, which sets the bias amplitude.
    const float step = 255.f / (levels - 1);

    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            // Indexed by block, not pixel: the unit being quantised downstream is
            // a cell, so a bias that varied within one would just average back out.
            const int blockX = x / blockW;
            const int blockY = y / blockH;

            const float amplitude = gate.at(blockX, blockY);
            if (amplitude <= 0.f) continue;

            const float bias =
                (kBayer[blockY & 3][blockX & 3] / 16.f - 0.5f) * step * amplitude;

            PixelColor c = image.getAt(x, y);
            c.r = (byte)std::clamp(std::round(c.r + bias), 0.f, 255.f);
            c.g = (byte)std::clamp(std::round(c.g + bias), 0.f, 255.f);
            c.b = (byte)std::clamp(std::round(c.b + bias), 0.f, 255.f);

            image.setAt(x, y, c);
        }
    }
}

}   // namespace Dithering
