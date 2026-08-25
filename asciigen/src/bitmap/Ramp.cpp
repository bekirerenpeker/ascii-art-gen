#include "bitmap/Ramp.hpp"
#include "bitmap/Resample.hpp"
#include "dithering/Dithering.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Ramp {

void generate(
    const Image& image, CellBuffer& outBuffer, Charset& outCharset, const std::string& ramp,
    Resample::Filter resampleFilter
)
{
    outCharset = Charset(ramp);
    if (outCharset.size() == 0 || outBuffer.width() <= 0 || outBuffer.height() <= 0) return;

    // One sample per cell, so the plane is already at the granularity the glyph
    // choice quantises at -- the default 1x1 dither block is the right one here.
    Image plane;
    Resample::toGrid(image, plane, outBuffer.width(), outBuffer.height(), resampleFilter);
    Dithering::apply(plane);

    for (int y = 0; y < outBuffer.height(); y++) {
        for (int x = 0; x < outBuffer.width(); x++) {
            const PixelColor p = plane.getAt(x, y);

            float luma = (0.299f * p.r + 0.587f * p.g + 0.114f * p.b) / 255.f;
            luma = std::clamp(luma, 0.f, 1.f);

            Cell& cell = outBuffer.getAt(x, y);
            cell.glyphIndex = (uint16_t)std::round(luma * (outCharset.size() - 1));
            cell.fg = {p.r, p.g, p.b};
            cell.bg = {0, 0, 0};
        }
    }
}

}   // namespace Ramp
