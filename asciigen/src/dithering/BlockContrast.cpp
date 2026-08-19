#include "dithering/BlockContrast.hpp"
#include <algorithm>
#include <cmath>

namespace Dithering {

static float smoothstep(float lo, float hi, float v)
{
    if (hi <= lo) return v < lo ? 0.f : 1.f;
    const float t = std::clamp((v - lo) / (hi - lo), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

void computeBlockContrast(
    const Image& plane, int blockW, int blockH, float flat, float edge, float headroom,
    ContrastField& out
)
{
    out.cols = out.rows = 0;
    out.amplitude.clear();

    if (!plane.pixels || blockW < 1 || blockH < 1) return;

    out.cols = (plane.width + blockW - 1) / blockW;
    out.rows = (plane.height + blockH - 1) / blockH;
    out.amplitude.assign((size_t)out.cols * (size_t)out.rows, 1.f);

    // A 1x1 block has no spread of its own -- that is the ramp selector's
    // granularity -- so the window is widened to the smallest size that can
    // actually measure one. Larger blocks already are their own window.
    const int winW = std::max(blockW, 3);
    const int winH = std::max(blockH, 3);
    const float n = (float)(winW * winH);

    for (int by = 0; by < out.rows; by++) {
        for (int bx = 0; bx < out.cols; bx++) {
            const int x0 = bx * blockW + blockW / 2 - winW / 2;
            const int y0 = by * blockH + blockH / 2 - winH / 2;

            float sum = 0.f, sumSq = 0.f;
            for (int wy = 0; wy < winH; wy++) {
                for (int wx = 0; wx < winW; wx++) {
                    const int x = std::clamp(x0 + wx, 0, plane.width - 1);
                    const int y = std::clamp(y0 + wy, 0, plane.height - 1);
                    const PixelColor c = plane.getAt(x, y);
                    const float l = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
                    sum += l;
                    sumSq += l * l;
                }
            }

            const float mean = sum / n;
            const float sd = std::sqrt(std::max(0.f, sumSq / n - mean * mean));

            // Dither only buys anything where quantising would band, and banding
            // needs a flat neighbourhood. Where the signal already swings wider
            // than a quantisation step there is nothing to break up, so the bias
            // is pure jitter on top of detail the selector had right.
            const float flatness = 1.f - smoothstep(flat, edge, sd);

            // A tone pinned against black or white has nowhere to be dithered
            // *towards*: half the bias would invent brightness the source never
            // had, which is what speckles a flat background. Taper by whichever
            // end is nearer, over the distance the bias can actually push.
            const float room = smoothstep(0.f, headroom, std::min(mean, 255.f - mean));

            out.amplitude[(size_t)bx + (size_t)by * (size_t)out.cols] = flatness * room;
        }
    }
}

}   // namespace Dithering
