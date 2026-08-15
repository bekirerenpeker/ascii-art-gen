#include "font/GlyphAtlas.hpp"
#include <cstdint>

GlyphAtlas::GlyphAtlas(const Font& font, const Charset& charset, int cellW, int cellH)
    : m_cellW(cellW), m_cellH(cellH)
{
    m_glyphPixels = new uint8_t[cellW * cellH * charset.size()];

    uint8_t* begin = m_glyphPixels;
    for (int i = 0; i < charset.size(); i++) {
        font.rasterize(charset.codepointAt(i), begin, cellW, cellH);
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
