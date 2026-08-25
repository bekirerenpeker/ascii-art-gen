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

    // Walked as a flat pointer rather than through getAt/setAt. A byte write may
    // alias anything, so the accessors force width/height/depth to be reloaded
    // after every pixel and the loop cannot vectorise; hoisting them here is what
    // makes the gather and scatter cheap next to the blur itself.
    const int d = image.depth;
    byte* const px = image.pixels;

    // S4: two buffers reused across channels instead of six held at once.
    // Channels are independent -- each only ever reads or writes its own byte
    // lane -- so nothing was ever gained from gathering all of them up front.
    // F2: blur's own internal scratch is caller-owned here too, for the same
    // reason -- reused across all 3 channel calls instead of allocated fresh
    // each time.
    std::vector<float> ch(n), soft(n), scratch;

    const int colorChannels = d >= 3 ? 3 : 1;

    if (radius == 1) {
        // F4: `original + amount*(original - blur(radius=1))` is, algebraically,
        // exactly one fixed 3x3 kernel -- a separable radius-1 box blur composes
        // into a true uniform 3x3 average (weight 1/9 each, including the centre
        // pixel), so expanding the subtraction gives:
        //   centre   = 1 + amount*(8/9)
        //   each of the 8 neighbours = -amount/9
        // (sum of all 9 weights is exactly 1, so a flat region is untouched, as
        // it should be). This is the same operation as the general path below,
        // not an approximation of it -- just one pass over the pixels instead of
        // four (gather, blur-horizontal, blur-vertical, combine), and no
        // intermediate buffer between them. Floating-point rounding lands in a
        // different order than the two-pass version, the same caveat as any
        // other fused-vs-separate accumulation on this page.
        const float centre = 1.f + amount * (8.f / 9.f);
        const float nb = -amount / 9.f;
        const int w = image.width, h = image.height;

        for (int c = 0; c < colorChannels; c++) {
            // Reads the still-untouched original directly -- `soft` is a
            // separate buffer, so nothing here can read a value this same pass
            // already overwrote. T1 already showed clamp removal on a 3x3
            // neighbourhood isn't worth chasing, so every pixel just clamps.
            auto at = [&](int x, int y) {
                x = std::clamp(x, 0, w - 1);
                y = std::clamp(y, 0, h - 1);
                return (float)px[((size_t)y * w + x) * d + c];
            };

            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const float sum = at(x - 1, y - 1) + at(x, y - 1) + at(x + 1, y - 1)
                                     + at(x - 1, y) + at(x + 1, y)
                                     + at(x - 1, y + 1) + at(x, y + 1) + at(x + 1, y + 1);
                    soft[(size_t)y * w + x] = centre * at(x, y) + nb * sum;
                }
            }

            byte* dst = px + c;
            for (size_t i = 0; i < n; i++, dst += d) *dst = toByte(soft[i]);
        }
        return;
    }

    // The blurred copy holds everything the blur did NOT remove, so the
    // difference is exactly the detail it destroyed. Adding that back on top is
    // what sharpens; the original is untouched where there was no detail to lose.
    if (d >= 3) {
        for (int c = 0; c < 3; c++) {
            const byte* p = px + c;
            for (size_t i = 0; i < n; i++, p += d) ch[i] = *p;

            blur(ch.data(), soft.data(), image.width, image.height, radius, scratch);

            byte* dst = px + c;
            for (size_t i = 0; i < n; i++, dst += d) *dst = toByte(ch[i] + amount * (ch[i] - soft[i]));
        }
    }
    else {
        // A grey pixel's r, g and b were always identical, so the old
        // luma-reweight after sharpening each one separately just recombined
        // three equal values back into itself -- one pass lands on the same byte.
        const byte* p = px;
        for (size_t i = 0; i < n; i++, p += d) ch[i] = p[0];

        blur(ch.data(), soft.data(), image.width, image.height, radius, scratch);

        byte* dst = px;
        for (size_t i = 0; i < n; i++, dst += d) dst[0] = toByte(ch[i] + amount * (ch[i] - soft[i]));
    }
}

}   // namespace ImageFilters
