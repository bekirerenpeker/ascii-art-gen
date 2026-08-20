#include "core/Profiler.hpp"
#include "font/GlyphAtlas.hpp"
#include "core/Image.hpp"
#include "file_management/ImageManager.hpp"
#include <cstdint>

GlyphAtlas::GlyphAtlas(const Font& font, const Charset& charset, int cellW, int cellH, float boldness)
    : m_glyphCount(charset.size()), m_cellW(cellW), m_cellH(cellH)
{
    m_glyphPixels = new uint8_t[cellW * cellH * charset.size()];
    ASCIIGEN_PROFILE("GlyphAtlas::build", "font");


    uint8_t* begin = m_glyphPixels;
    for (int i = 0; i < charset.size(); i++) {
        font.rasterize(charset.codepointAt(i), begin, cellW, cellH, boldness);
        begin += cellW * cellH;
    }
}

GlyphAtlas::~GlyphAtlas() { delete[] m_glyphPixels; }

const uint8_t* GlyphAtlas::getGlyphBegin(int glpyhIndex) const
{
    return m_glyphPixels + glpyhIndex * glpyhSize();
}

uint8_t GlyphAtlas::getGlyphPixel(int glpyhIndex, int x, int y) const
{
    return getGlyphBegin(glpyhIndex)[x + y * m_cellW];
}

bool GlyphAtlas::debugDumpToImage(const std::filesystem::path& filepath) const
{
    if (!m_glyphPixels || m_glyphCount <= 0) return false;

    // One border column/row between neighbours, plus one on the far edge, so
    // every cell ends up fully enclosed rather than sharing only left edges.
    const int strideX = m_cellW + 1;
    const int imgW = m_glyphCount * strideX + 1;
    const int imgH = m_cellH + 2;

    Image img(imgW, imgH, 4);

    for (int y = 0; y < imgH; y++)
        for (int x = 0; x < imgW; x++) img.setAt(x, y, {0, 0, 0, 255});

    for (int i = 0; i < m_glyphCount; i++) {
        const int originX = i * strideX + 1;
        const uint8_t* glyph = getGlyphBegin(i);

        for (int y = 0; y < m_cellH; y++) {
            for (int x = 0; x < m_cellW; x++) {
                const uint8_t v = glyph[x + y * m_cellW];
                img.setAt(originX + x, y + 1, {v, v, v, 255});
            }
        }

        // Box this cell: verticals down both sides, horizontals across top and
        // bottom. Drawn after the glyph so the border always stays visible.
        for (int y = 0; y < imgH; y++) {
            img.setAt(originX - 1, y, {255, 255, 255, 255});
            img.setAt(originX + m_cellW, y, {255, 255, 255, 255});
        }
        for (int x = -1; x <= m_cellW; x++) {
            img.setAt(originX + x, 0, {255, 255, 255, 255});
            img.setAt(originX + x, imgH - 1, {255, 255, 255, 255});
        }
    }

    return ImageManager::saveImage(filepath, img);
}
