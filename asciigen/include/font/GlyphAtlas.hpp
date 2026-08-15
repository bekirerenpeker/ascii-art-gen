#pragma once

#include "core/Charset.hpp"
#include "font/Font.hpp"
#include <cstdint>

class GlyphAtlas
{
  private:
    // each glpyh is cellW*cellH bytes. they are stored x first then y
    uint8_t* m_glyphPixels = nullptr;
    int m_cellW = 0, m_cellH = 0;

  public:
    GlyphAtlas() = default;
    GlyphAtlas(const Font& font, const Charset& charset, int cellW, int cellH);
    ~GlyphAtlas();

    int cellWidth() const { return m_cellW; }
    int cellHeight() const { return m_cellH; }
    int glpyhSize() const { return m_cellW * m_cellH; }

    const uint8_t* getGlyphBegin(int glpyhIndex) const;
    uint8_t getGlyphPixel(int glpyhIndex, int x, int y) const;
};
