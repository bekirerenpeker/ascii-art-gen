#include "font/Font.hpp"
#include "stb/stb_image_resize2.h"

#include <algorithm>
#include <vector>

bool Font::s_libInitialized = false;
FT_Library Font::s_library;

Font::Font(std::filesystem::path filepath)
{
    if (!s_libInitialized) {
        FT_Init_FreeType(&s_library);
        s_libInitialized = true;
    }

    FT_New_Face(s_library, filepath.string().c_str(), 0, &m_face);
}

void Font::rasterize(char32_t c, uint8_t* outBuffer, int width, int height) const
{
    if (!m_face || !outBuffer || width <= 0 || height <= 0) return;

    std::fill_n(outBuffer, static_cast<size_t>(width) * height, uint8_t(0));

    // Rasterize into a cell several times larger than the target, then filter
    // down. Note what gets resized: a fully composed *cell*, not FreeType's
    // glyph bitmap -- that bitmap is the glyph's tight ink extent, so scaling
    // it to fill the cell would blow '.' up to the same solid 8x8 block as
    // '@' and leave nothing to discriminate on.
    constexpr int kSupersample = 4;
    const int renderW = width * kSupersample;
    const int renderH = height * kSupersample;

    // FT_Set_Pixel_Sizes sizes the EM square, but most fonts span more than one
    // EM from ascender to descender -- asking for renderH directly would clip
    // the tails off 'g', 'y' and 'p'. Scale so the full span fits instead.
    int pixelSize = renderH;
    if (FT_IS_SCALABLE(m_face)) {
        const int span = m_face->ascender - m_face->descender;   // font units
        if (span > 0)
            pixelSize = static_cast<int>(
                static_cast<int64_t>(renderH) * m_face->units_per_EM / span
            );
    }
    pixelSize = std::max(1, pixelSize);

    if (FT_Set_Pixel_Sizes(m_face, 0, static_cast<FT_UInt>(pixelSize)) != 0) return;
    if (FT_Load_Char(m_face, static_cast<FT_ULong>(c), FT_LOAD_RENDER) != 0) return;

    const FT_GlyphSlot slot = m_face->glyph;
    const FT_Bitmap& bmp = slot->bitmap;

    std::vector<uint8_t> cell(static_cast<size_t>(renderW) * renderH, 0);

    // Empty for whitespace -- leaving the cell zeroed is the correct result.
    if (bmp.buffer && bmp.width > 0 && bmp.rows > 0) {
        const int baselineY = static_cast<int>(m_face->size->metrics.ascender >> 6);
        // Centred rather than placed at bitmap_left: a cell is a fixed slot,
        // not a pen position in a run of proportional text.
        const int destX = (renderW - static_cast<int>(bmp.width)) / 2;
        const int destY = baselineY - slot->bitmap_top;

        for (int row = 0; row < static_cast<int>(bmp.rows); row++) {
            const int y = destY + row;
            if (y < 0 || y >= renderH) continue;

            for (int col = 0; col < static_cast<int>(bmp.width); col++) {
                const int x = destX + col;
                if (x < 0 || x >= renderW) continue;

                uint8_t sample;
                if (bmp.pixel_mode == FT_PIXEL_MODE_MONO) {
                    // Embedded bitmap fonts (BDF/PCF) render 1bpp, not 8bpp.
                    const uint8_t packed = bmp.buffer[row * bmp.pitch + (col >> 3)];
                    sample = (packed & (0x80 >> (col & 7))) ? 255 : 0;
                } else {
                    sample = bmp.buffer[row * bmp.pitch + col];
                }

                cell[static_cast<size_t>(y) * renderW + x] = sample;
            }
        }
    }

    // Coverage is linear alpha, not sRGB-encoded, so the linear variant is the
    // correct one. Writes directly into outBuffer -- no intermediate copy.
    stbir_resize_uint8_linear(
        cell.data(), renderW, renderH, 0, outBuffer, width, height, 0, STBIR_1CHANNEL
    );
}
