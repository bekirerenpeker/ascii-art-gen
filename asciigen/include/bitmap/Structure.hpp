#pragma once

#include "bitmap/Descriptor.hpp"
#include "bitmap/DescriptorBasis.hpp"
#include "core/CellBuffer.hpp"
#include "core/Color.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"
#include <vector>

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

// Per-glyph facts that never change while a frame is being built: how much ink
// the glyph carries, what shape that ink makes, and (S1) that shape packed
// glyph-contiguous rather than as N separate heap vectors, plus (S2) a cheap
// subspace to bound a glyph's score against before paying for its real one.
//
// Depends only on `atlas` and `shape`, both of which are the same for every
// frame of a video -- callers build this ONCE (alongside the atlas itself) and
// reuse it across every call to generate(), rather than paying to rebuild it
// per frame. A still image still only builds it once, so this changes nothing
// there.
struct GlyphModel
{
    std::vector<float> inkWeight;
    float maxCoverage = 1.f;

    int oriLen = 0;
    int massLen = 0;
    std::vector<float> oriMatrix;    // glyphCount x oriLen, row-major
    std::vector<float> massMatrix;   // glyphCount x massLen, row-major

    DescriptorBasis oriBasis;
    DescriptorBasis massBasis;
    std::vector<float> oriCoeffs;      // glyphCount x oriBasis.k
    std::vector<float> massCoeffs;     // glyphCount x massBasis.k
    std::vector<float> oriResidNorm;   // glyphCount, full kOriBasisSize bound
    std::vector<float> massResidNorm;  // glyphCount, full kMassBasisSize bound

    // el-1: residual for the cheap cascade tier, using only the first
    // kOriFastSize/kMassFastSize of the same coefficients above.
    std::vector<float> oriResidNormFast;   // glyphCount
    std::vector<float> massResidNormFast;  // glyphCount

    // Flat-tile path: how far each glyph's ink centroid sits from the cell's
    // own geometric centre, normalised by half the cell diagonal (so it's
    // roughly 0..1 regardless of cell size) -- and the same glyph indices
    // sorted by inkWeight, for the binary search in pickGlyphFlat.
    std::vector<float> offCenter;   // glyphCount
    std::vector<int> inkOrder;      // glyphCount, indices into the arrays above
};

void buildGlyphModel(const GlyphAtlas& atlas, DescriptorShape shape, GlyphModel& out);

// Every per-cell working buffer generate() needs that would otherwise be a fresh
// local -- reused across frames instead of allocated per call. Contents don't
// need to survive between calls, only the storage does.
struct Scratch
{
    std::vector<RGB> tile;
    std::vector<float> tileLuma;
    Descriptor tileDesc;
    std::vector<float> tileOriCoeffs;
    std::vector<float> tileMassCoeffs;
    std::vector<int> massCounts;
};

// `image` must already be exactly `outBuffer.width() * atlas.cellWidth()` x
// `outBuffer.height() * atlas.cellHeight()`, resampled AND dithered -- generate()
// used to do both itself, internally, on every call, which for a caller that
// already needed to know the plane's size to build one (every real caller) meant
// resampling an already-correctly-sized plane to its own size a second time.
// That's the caller's job now, once, not this function's, every call.
void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, const GlyphModel& model,
    Scratch& scratch, StructureOptions opts = {}
);

};   // namespace Structure
