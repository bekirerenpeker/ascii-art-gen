#include "core/Profiler.hpp"
#include "bitmap/Resample.hpp"
#include "stb/stb_image_resize2.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Resample {

void toGrid(const Image& src, Image& outPlane, int outW, int outH, Filter filter)
{
    ASCIIGEN_PROFILE("Resample::toGrid", "resample");

    if (!src.pixels || outW <= 0 || outH <= 0) return;

    if (outPlane.width != outW || outPlane.height != outH || outPlane.depth != 3)
        outPlane = Image(outW, outH, 3);

    // Composite alpha against black and expand grey to RGB up front, matching
    // the old box filter's own per-pixel semantics exactly (multiplying by
    // alpha before averaging or after gives the same result, since
    // multiplication distributes through a sum either way) -- so stb only
    // ever sees a plain RGB source, and there is nothing of its own alpha
    // handling to reason about.
    std::vector<byte> rgb;
    const byte* rgbSrc = src.pixels;
    if (src.depth != 3) {
        rgb.resize((size_t)src.width * (size_t)src.height * 3);
        const byte* s = src.pixels;
        byte* d = rgb.data();
        const int sd = src.depth;
        const size_t count = (size_t)src.width * (size_t)src.height;
        for (size_t i = 0; i < count; i++, s += sd, d += 3) {
            if (sd >= 3) {
                const float a = sd > 3 ? s[3] / 255.f : 1.f;
                d[0] = (byte)std::lround(s[0] * a);
                d[1] = (byte)std::lround(s[1] * a);
                d[2] = (byte)std::lround(s[2] * a);
            }
            else {
                const float a = sd == 2 ? s[1] / 255.f : 1.f;
                const byte grey = (byte)std::lround(s[0] * a);
                d[0] = d[1] = d[2] = grey;
            }
        }
        rgbSrc = rgb.data();
    }

    // Already the right size. Matters because the app now resamples once up
    // front so filters run at cell resolution; the selector then asks for the
    // same size again and this turns that second pass into a copy.
    if (src.width == outW && src.height == outH) {
        std::copy(rgbSrc, rgbSrc + (size_t)outW * outH * 3, outPlane.pixels);
        return;
    }

    // _linear, not _srgb: treats bytes as already-linear values with no gamma
    // conversion, the same (naive, but consistent) assumption the old box
    // filter made by averaging raw bytes directly, for all three filters below.
    if (filter == Filter::Auto) {
        // Picks its own filter by direction -- Mitchell shrinking, Catmull-Rom
        // growing -- both real cubic interpolation, unlike Box/Triangle's one
        // filter both ways.
        stbir_resize_uint8_linear(
            rgbSrc, src.width, src.height, 0, outPlane.pixels, outW, outH, 0, STBIR_RGB
        );
        return;
    }

    STBIR_RESIZE resize;
    stbir_resize_init(
        &resize, rgbSrc, src.width, src.height, 0, outPlane.pixels, outW, outH, 0, STBIR_RGB,
        STBIR_TYPE_UINT8
    );
    const stbir_filter f = filter == Filter::Box ? STBIR_FILTER_BOX : STBIR_FILTER_TRIANGLE;
    stbir_set_filters(&resize, f, f);
    stbir_resize_extended(&resize);
}

}   // namespace Resample
