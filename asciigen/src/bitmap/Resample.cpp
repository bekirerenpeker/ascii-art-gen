#include "bitmap/Resample.hpp"
#include <algorithm>
#include <cmath>

namespace Resample {

void toGrid(const Image& src, Image& outPlane, int outW, int outH)
{
    if (!src.pixels || outW <= 0 || outH <= 0) return;

    if (outPlane.width != outW || outPlane.height != outH || outPlane.depth != 3)
        outPlane = Image(outW, outH, 3);

    const float scaleX = src.width / (float)outW;
    const float scaleY = src.height / (float)outH;

    for (int y = 0; y < outH; y++) {
        const int y0 = (int)(y * scaleY);
        const int y1 = std::max(y0 + 1, (int)((y + 1) * scaleY));

        for (int x = 0; x < outW; x++) {
            const int x0 = (int)(x * scaleX);
            const int x1 = std::max(x0 + 1, (int)((x + 1) * scaleX));

            float tr = 0, tg = 0, tb = 0;
            int count = 0;

            for (int sy = y0; sy < y1; sy++) {
                for (int sx = x0; sx < x1; sx++) {
                    const PixelColor c = src.getAt(sx, sy);
                    const float alpha = c.a / 255.f;
                    tr += c.r * alpha;
                    tg += c.g * alpha;
                    tb += c.b * alpha;
                    count++;
                }
            }

            if (count == 0) {
                outPlane.setAt(x, y, {0, 0, 0, 255});
                continue;
            }

            outPlane.setAt(
                x, y,
                {(byte)std::round(tr / count), (byte)std::round(tg / count),
                 (byte)std::round(tb / count), 255}
            );
        }
    }
}

}   // namespace Resample
