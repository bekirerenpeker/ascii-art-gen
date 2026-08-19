#pragma once

#include "bitmap/Descriptor.hpp"
#include "core/CellBuffer.hpp"
#include "core/Color.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"

namespace Structure {

struct StructureOptions
{
    // How the cell is carved up before edge directions are counted. More blocks
    // means the descriptor knows better WHERE a stroke is, but drifts back
    // towards per-pixel matching; fewer means it only knows the cell's overall
    // flow. bins is how finely direction itself is split, over a half turn.
    DescriptorShape shape {.blocksX = 2, .blocksY = 4, .bins = 4};

    // Which way the ink runs. This is the term that makes a glyph get picked for
    // its shape rather than its weight.
    float orientationWeight = 1.f;

    // Where the ink sits, block by block. Catches what orientation cannot: a
    // shape with no strong direction, like a dot low in the cell.
    float massWeight = 1.f;

    // Pull towards glyphs whose ink coverage matches the tile's brightness. Both
    // shape terms are mean-centred and so say nothing about how much ink there
    // is; without this the picture comes out the right shape at the wrong exposure.
    float toneWeight = 4.f;

    // Passed straight through to solveCellColor once a glyph has been chosen.
    bool allowBackground = false;
    float brightnessGamma = 1.f;
    RGB backgroundColor {0, 0, 0};
};

void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, StructureOptions opts = {}
);

};   // namespace Structure
