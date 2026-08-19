#pragma once

#include "core/Charset.hpp"
#include "font/Font.hpp"
#include <cstdint>
#include <filesystem>
#include <utility>

class GlyphAtlas
{
  private:
    // each glpyh is cellW*cellH bytes. they are stored x first then y
    uint8_t* m_glyphPixels = nullptr;
    int m_glyphCount = 0;
    int m_cellW = 0, m_cellH = 0;

  public:
    GlyphAtlas() = default;
    GlyphAtlas(const Font& font, const Charset& charset, int cellW, int cellH, float boldness = 0.f);
    ~GlyphAtlas();

    // Movable, never copyable. It owns a raw allocation, so the implicit copy
    // the compiler would otherwise hand out double-frees; being movable is what
    // lets an atlas be built once the charset it rasterises is finally settled.
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    GlyphAtlas(GlyphAtlas&& other) noexcept { *this = std::move(other); }

    GlyphAtlas& operator=(GlyphAtlas&& other) noexcept
    {
        if (this == &other) return *this;

        delete[] m_glyphPixels;

        m_glyphPixels = other.m_glyphPixels;
        m_glyphCount = other.m_glyphCount;
        m_cellW = other.m_cellW;
        m_cellH = other.m_cellH;

        other.m_glyphPixels = nullptr;
        other.m_glyphCount = 0;
        other.m_cellW = other.m_cellH = 0;

        return *this;
    }

    int cellWidth() const { return m_cellW; }
    int cellHeight() const { return m_cellH; }
    int glyphCount() const { return m_glyphCount; }
    int glpyhSize() const { return m_cellW * m_cellH; }

    const uint8_t* getGlyphBegin(int glpyhIndex) const;
    uint8_t getGlyphPixel(int glpyhIndex, int x, int y) const;

    bool debugDumpToImage(const std::filesystem::path& filepath) const;
};
