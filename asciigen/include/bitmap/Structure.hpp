#pragma once

#include "bitmap/Descriptor.hpp"
#include "core/CellBuffer.hpp"
#include "core/Color.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"

namespace Structure {

struct StructureOptions
{
    // Block grids for the two shape terms; see DescriptorShape. Coarsening the
    // mass grid trades surface detail away, which is what makes glyphs that only
    // differ in fine structure -- T against -, E against = -- become
    // interchangeable to the scorer.
    DescriptorShape shape {};

    // Which way the ink runs. This is the term that makes edges come out clean
    // and directional rather than dissolving into tone.
    float orientationWeight = 0.25f;

    // Where the ink sits. At one pixel per block this is plain per-pixel
    // correlation, which is what resolves shading on flat surfaces.
    float massWeight = 1.f;

    // Pull towards glyphs whose ink coverage matches the tile's brightness. Both
    // shape terms are mean-centred and so say nothing about how much ink there
    // is; without this the picture comes out the right shape at the wrong exposure.
    float toneWeight = 4.f;

    // Passed straight through to solveCellColor once a glyph has been chosen.
    bool allowBackground = false;
    float brightnessGamma = 1.f;
};

void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, StructureOptions opts = {}
);

};   // namespace Structure
