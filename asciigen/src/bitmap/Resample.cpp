#include "core/Profiler.hpp"
#include "bitmap/Resample.hpp"
#include <algorithm>
#include <cmath>

namespace Resample {

void toGrid(const Image& src, Image& outPlane, int outW, int outH)
{
    ASCIIGEN_PROFILE("Resample::toGrid", "resample");

    if (!src.pixels || outW <= 0 || outH <= 0) return;

    // Already the right size. Matters because the app now resamples once up
    // front so filters run at cell resolution; the selector then asks for the
    // same size again and this turns that second pass into a copy.
    if (src.width == outW && src.height == outH && src.depth == 3) {
        if (outPlane.width != outW || outPlane.height != outH || outPlane.depth != 3)
            outPlane = Image(outW, outH, 3);

        std::copy(src.pixels, src.pixels + (size_t)outW * outH * 3, outPlane.pixels);
        return;
    }

    if (outPlane.width != outW || outPlane.height != outH || outPlane.depth != 3)
        outPlane = Image(outW, outH, 3);

    const float scaleX = src.width / (float)outW;
    const float scaleY = src.height / (float)outH;

    // Dimensions and both base pointers are pulled into locals before the loops.
    // Not style: `pixels` is a byte*, and a byte write may legally alias any
    // object, so writing through outPlane.pixels forces the compiler to reload
    // src.width/height/depth from memory on every single sample. Hoisting is what
    // breaks that dependency and lets the box filter run at pointer speed.
    const byte* const srcPx = src.pixels;
    const int srcW = src.width, srcH = src.height, srcD = src.depth;
    byte* const outPx = outPlane.pixels;

    for (int y = 0; y < outH; y++) {
        const int y0 = (int)(y * scaleY);
        const int y1 = std::min(srcH, std::max(y0 + 1, (int)((y + 1) * scaleY)));

        byte* outRow = outPx + (size_t)y * outW * 3;

        for (int x = 0; x < outW; x++) {
            const int x0 = (int)(x * scaleX);
            const int x1 = std::min(srcW, std::max(x0 + 1, (int)((x + 1) * scaleX)));

            float tr = 0, tg = 0, tb = 0;
            int count = 0;

            for (int sy = y0; sy < y1; sy++) {
                const byte* p = srcPx + ((size_t)sy * srcW + x0) * srcD;

                for (int sx = x0; sx < x1; sx++, p += srcD) {
                    // Matches getAt's expansion: 1 and 2 channels are grey, and
                    // only 2 and 4 carry alpha.
                    if (srcD >= 3) {
                        const float alpha = srcD > 3 ? p[3] / 255.f : 1.f;
                        tr += p[0] * alpha;
                        tg += p[1] * alpha;
                        tb += p[2] * alpha;
                    }
                    else {
                        const float alpha = srcD == 2 ? p[1] / 255.f : 1.f;
                        const float grey = p[0] * alpha;
                        tr += grey;
                        tg += grey;
                        tb += grey;
                    }
                    count++;
                }
            }

            byte* out = outRow + (size_t)x * 3;

            if (count == 0) {
                out[0] = out[1] = out[2] = 0;
                continue;
            }

            out[0] = (byte)std::lround(tr / count);
            out[1] = (byte)std::lround(tg / count);
            out[2] = (byte)std::lround(tb / count);
        }
    }
}

}   // namespace Resample
