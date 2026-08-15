#include "bitmap/Ramp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Ramp {

RGB getCellValue(const Image& image, int startX, int startY, int width, int height)
{
    float tr = 0, tg = 0, tb = 0;
    int count = width * height;

    for (int y = startY; y < startY + height; y++) {
        for (int x = startX; x < startX + width; x++) {
            PixelColor c = image.getAt(x, y);
            float alpha = c.a / 255.f;
            tr += c.r * alpha;
            tg += c.g * alpha;
            tb += c.b * alpha;
        }
    }

    return {
        (uint8_t)std::round(tr / count),
        (uint8_t)std::round(tg / count),
        (uint8_t)std::round(tb / count),
    };
}

void generate(
    const Image& image, CellBuffer& outBuffer, Charset& outCharset, const std::string& ramp
)
{
    outCharset = Charset(ramp);

    float cellW = image.width / (float)outBuffer.width();
    float cellH = image.height / (float)outBuffer.height();
    int cellWI = std::max(1, (int)std::round(cellW));
    int cellHI = std::max(1, (int)std::round(cellH));

    for (int y = 0; y < outBuffer.height(); y++) {
        for (int x = 0; x < outBuffer.width(); x++) {
            int cellX = std::min((int)std::round(x * cellW), image.width - cellWI);
            int cellY = std::min((int)std::round(y * cellH), image.height - cellHI);
            RGB avg = getCellValue(image, cellX, cellY, cellWI, cellHI);

            float luma = (0.299 * avg.r + 0.587 * avg.g + 0.114 * avg.b) / 255;
            luma = std::clamp(luma, 0.f, 1.f);

            Cell& cell = outBuffer.getAt(x, y);
            cell.glyphIndex = (uint16_t)std::round(luma * (outCharset.size() - 1));
            cell.fg = avg;
            cell.bg = {0, 0, 0};
        }
    }
}

}   // namespace Ramp
