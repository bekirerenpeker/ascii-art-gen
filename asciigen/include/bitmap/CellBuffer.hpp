#pragma once

#include "bitmap/Color.hpp"

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

struct CellBuffer
{
    int width = 0, height = 0;
    std::vector<Cell> cells;

    Cell& getAt(int x, int y) { return cells[x + y * width]; }
    const Cell& getAt(int x, int y) const { return cells[x + y * width]; }
};
