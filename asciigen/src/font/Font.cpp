#include "font/Font.hpp"
#include "freetype/ftoutln.h"
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

void Font::rasterize(char32_t c, uint8_t* outBuffer, int cellW, int cellH, float boldness) const
{
    if (!m_face || !outBuffer || cellW <= 0 || cellH <= 0) return;

    std::fill_n(outBuffer, static_cast<size_t>(cellW) * cellH, uint8_t(0));

    const int renderW = cellW * kSupersample;
    const int renderH = cellH * kSupersample;

    // ascender..descender is the font's own layout height -- what one line of
    // text occupies. Sizing so that span fills the cell leaves every glyph at
    // whatever size the font says it should be relative to the others: a
    // period stays a period, '@' stays large. Nothing is normalised per glyph.
    const int64_t upem = m_face->units_per_EM;
    const int spanY = m_face->ascender - m_face->descender;
    const int pixelSize =
        std::max(1, spanY > 0 ? static_cast<int>(renderH * upem / spanY) : renderH);

    if (FT_Set_Pixel_Sizes(m_face, 0, static_cast<FT_UInt>(pixelSize)) != 0) return;

    // Loaded and rendered as two steps rather than one, so the outline can be
    // thickened in between. Emboldening a rendered bitmap smears it; growing the
    // outline first and rasterising after keeps the edges as clean as the
    // original weight.
    if (FT_Load_Char(m_face, static_cast<FT_ULong>(c), FT_LOAD_DEFAULT) != 0) return;

    const FT_GlyphSlot slot = m_face->glyph;

    if (boldness > 0.f && slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        // 26.6 fixed point. 4% of the pixel size per unit of boldness lands
        // close to a real bold face without closing up counters like 'e' and 'a'.
        const FT_Pos strength = static_cast<FT_Pos>(boldness * pixelSize * 64.f * 0.04f);
        if (strength > 0) FT_Outline_Embolden(&slot->outline, strength);
    }

    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) return;

    const FT_Bitmap& bmp = slot->bitmap;

    std::vector<uint8_t> cell(static_cast<size_t>(renderW) * renderH, 0);

    // Whitespace carries no ink; the zeroed cell is already correct.
    if (bmp.buffer && bmp.width > 0 && bmp.rows > 0) {
        const int baselineY = static_cast<int>(m_face->size->metrics.ascender >> 6);
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
                    const uint8_t packed = bmp.buffer[row * bmp.pitch + (col >> 3)];
                    sample = (packed & (0x80 >> (col & 7))) ? 255 : 0;
                } else {
                    sample = bmp.buffer[row * bmp.pitch + col];
                }

                // Flipped on the way in: FreeType rasterises top-down, and every
                // buffer past this point is bottom-up.
                cell[static_cast<size_t>(renderH - 1 - y) * renderW + x] = sample;
            }
        }
    }

    stbir_resize_uint8_linear(
        cell.data(), renderW, renderH, 0, outBuffer, cellW, cellH, 0, STBIR_1CHANNEL
    );
}
