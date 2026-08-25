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
    std::vector<float> ch(n), soft(n);

    // The blurred copy holds everything the blur did NOT remove, so the
    // difference is exactly the detail it destroyed. Adding that back on top is
    // what sharpens; the original is untouched where there was no detail to lose.
    if (d >= 3) {
        for (int c = 0; c < 3; c++) {
            const byte* p = px + c;
            for (size_t i = 0; i < n; i++, p += d) ch[i] = *p;

            blur(ch.data(), soft.data(), image.width, image.height, radius);

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

        blur(ch.data(), soft.data(), image.width, image.height, radius);

        byte* dst = px;
        for (size_t i = 0; i < n; i++, dst += d) dst[0] = toByte(ch[i] + amount * (ch[i] - soft[i]));
    }
}

}   // namespace ImageFilters
