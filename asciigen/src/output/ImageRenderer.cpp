#include "output/ImageRenderer.hpp"
#include "file_management/ImageManager.hpp"
#include <cstdint>

namespace ImageRenderer {

Image render(const CellBuffer& buffer, const GlyphAtlas& atlas, ImageRenderOptions opts)
{
    const int cellW = atlas.cellWidth();
    const int cellH = atlas.cellHeight();
    if (buffer.width() <= 0 || buffer.height() <= 0 || cellW <= 0 || cellH <= 0) return Image();

    const int contentW = buffer.width() * cellW;
    const int contentH = buffer.height() * cellH;

    const int outW = opts.width > 0 ? opts.width : contentW;
    const int outH = opts.height > 0 ? opts.height : contentH;

    const RGB backdrop = opts.backgroundColor;

    Image img(outW, outH, 3);
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) img.setAt(x, y, {backdrop.r, backdrop.g, backdrop.b, 255});

    // Centred. Negative offsets are fine and are how a grid larger than the
    // target gets cropped: setAt drops anything outside the image.
    const int offX = (outW - contentW) / 2;
    const int offY = (outH - contentH) / 2;

    for (int cy = 0; cy < buffer.height(); cy++) {
        for (int cx = 0; cx < buffer.width(); cx++) {
            const Cell& cell = buffer.getAt(cx, cy);
            const uint8_t* glyph = atlas.getGlyphBegin(cell.glyphIndex);

            for (int y = 0; y < cellH; y++) {
                for (int x = 0; x < cellW; x++) {
                    // Coverage blends background toward foreground, the same as a
                    // terminal drawing the cell.
                    const int a = glyph[x + y * cellW];
                    const PixelColor c {
                        (byte)(cell.bg.r + (cell.fg.r - cell.bg.r) * a / 255),
                        (byte)(cell.bg.g + (cell.fg.g - cell.bg.g) * a / 255),
                        (byte)(cell.bg.b + (cell.fg.b - cell.bg.b) * a / 255),
                        255
                    };
                    img.setAt(offX + cx * cellW + x, offY + cy * cellH + y, c);
                }
            }
        }
    }

    return img;
}

bool save(
    const std::filesystem::path& filepath, const CellBuffer& buffer, const GlyphAtlas& atlas,
    ImageRenderOptions opts
)
{
    const Image img = render(buffer, atlas, opts);
    if (!img.pixels) return false;

    return ImageManager::saveImage(filepath, img);
}

}   // namespace ImageRenderer
