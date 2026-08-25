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

    const int w = image.width;
    const int h = image.height;
    const int d = image.depth;
    byte* const px = image.pixels;

    const int blocksX = (w + blockW - 1) / blockW;
    const int blocksY = (h + blockH - 1) / blockH;

    // Blocks outer, pixels inner -- bias is indexed by BLOCK coordinates, not
    // pixel ones, so every pixel in a block already got the identical value
    // the old per-pixel loop just kept recomputing. Gated blocks (amplitude
    // <= 0, common on flat/saturated regions -- see BlockContrast.cpp) now
    // skip their whole run of pixels in one branch instead of one `continue`
    // per pixel. Pixel order within and across blocks doesn't matter: every
    // pixel is written independently, nothing here accumulates across pixels.
    for (int blockY = 0; blockY < blocksY; blockY++) {
        const int y0 = blockY * blockH;
        const int y1 = std::min(h, y0 + blockH);

        for (int blockX = 0; blockX < blocksX; blockX++) {
            const float amplitude = gate.at(blockX, blockY);
            if (amplitude <= 0.f) continue;

            const float bias =
                (kBayer[blockY & 3][blockX & 3] / 16.f - 0.5f) * step * amplitude;

            const int x0 = blockX * blockW;
            const int x1 = std::min(w, x0 + blockW);

            for (int y = y0; y < y1; y++) {
                byte* p = px + ((size_t)y * w + x0) * d;

                if (d >= 3) {
                    for (int x = x0; x < x1; x++, p += d) {
                        p[0] = (byte)std::clamp(std::round(p[0] + bias), 0.f, 255.f);
                        p[1] = (byte)std::clamp(std::round(p[1] + bias), 0.f, 255.f);
                        p[2] = (byte)std::clamp(std::round(p[2] + bias), 0.f, 255.f);
                    }
                }
                else {
                    // Grey buffers keep setAt's collapse -- a 1 or 2 channel
                    // image's r, g and b were always equal, so the old
                    // luma-reweight of three equal values just reconstructed
                    // the same byte. See UnsharpMask.cpp's identical case.
                    for (int x = x0; x < x1; x++, p += d)
                        p[0] = (byte)std::clamp(std::round(p[0] + bias), 0.f, 255.f);
                }
            }
        }
    }
}

}   // namespace Dithering
