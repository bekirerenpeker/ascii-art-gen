#pragma once

#include "core/Color.hpp"
#include <cstdint>
#include <vector>

// Colors are stored as true RGB -- the values bitmask actually computes as the
// means of the ink and paper pixel groups. Downconversion to the 16 ANSI
// colors is the sink's job, via RGB::toGlyphColor(), so one Cell layout serves
// every color depth.
struct Cell
{
    uint16_t glyphIndex = 0;
    RGB fg, bg;
};

class CellBuffer
{
  private:
    int m_width = 0, m_height = 0;
    std::vector<Cell> m_cells;

  public:
    void setSize(int width, int height)
    {
        m_width = width;
        m_height = height;
        m_cells.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    }

    int width() const { return m_width; }
    int height() const { return m_height; }

    Cell& getAt(int x, int y) { return m_cells[x + y * m_width]; }
    const Cell& getAt(int x, int y) const { return m_cells[x + y * m_width]; }

    // A backdrop drawn from the picture's own colours: the mean foreground of
    // every cell bright enough to be subject rather than background, taken down
    // by `darken`. Dark cells are skipped deliberately -- averaging them in just
    // pulls the answer towards black no matter what the subject looks like.
    // Call it after the colour filters, since it reads whatever fg holds then.
    RGB suggestedBackground(float darken = 0.15f, float lumaThreshold = 40.f) const;

    void fillBackground(RGB color)
    {
        for (Cell& cell : m_cells) cell.bg = color;
    }

    void fillForeground(RGB color)
    {
        for (Cell& cell : m_cells) cell.fg = color;
    }
};
