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

    // el-3: quality/speed trade, off (1) by default. See Descriptor.hpp.
    int gradientStride = 1;

    // el-4: on by default. Swaps buildDescriptor's std::atan2 + std::fmod for
    // a cheaper polynomial approximation of the same folded-to-[0,pi) angle
    // -- see Descriptor.cpp's fastFoldedAngle. Measured a ~43% cut on
    // buildDescriptor for a 0.14% cell-level glyph change on a real photo
    // (optimizations.md item 7); set false for the exact path.
    bool fastAtan = true;

    // Flat-tile path in pickGlyph (Structure.cpp): a tile whose |orientW| and
    // |massW| both fall at or under this skips the full glyph scan for a
    // binary search by ink weight, tie-broken by how centred each candidate
    // glyph's own ink is. A tile this flat has nothing for the shape terms to
    // say anyway. 0 would only ever fire on an exactly-uniform tile -- vanishingly
    // rare on a real photo -- so this defaults a little above that, small
    // enough to still mean "basically flat", not "mostly flat".
    float flatThreshold = 0.02f;
};

void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, StructureOptions opts = {}
);

};   // namespace Structure
