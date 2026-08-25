#pragma once

#include "core/CellBuffer.hpp"
#include "core/Color.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"
#include <vector>

namespace Bitmask {

struct BitmaskOptions
{
    // Solve a two-colour split per cell -- ink colour plus paper colour -- instead
    // of drawing over a black background. Scores on luma only, since a hue change
    // at constant brightness is not an edge the eye reads as one.
    bool allowBackground = false;

    // Exponent on the foreground colour. Coverage already tracks brightness, so a
    // value below 1 wins back some of that double-darkening; 1 disables it.
    float brightnessGamma = 1.f;

    // How much of the shape score comes from a blurred match instead of an exact
    // per-pixel one. Sharp correlation calls a glyph wrong when its stroke sits a
    // pixel off the image's, which a reader would never notice; blurring both
    // sides forgives that. 0 is pure per-pixel, 1 pure blurred, and the two are
    // mixed so the coverage term keeps the same relative weight either way.
    // Only affects the allowBackground = false path -- the two-colour split scores
    // through a different algebraic shortcut that has no correlation term to mix.
    float softness = 0.f;

    // Blur radius in cell pixels, i.e. how far a stroke may drift and still match.
    int blurRadius = 1;
};

// Every glyph-atlas-derived fact generate() needs that never changes while a
// frame is being built: ink weight and coverage stats per glyph, the float masks
// (B2), and -- only when softness > 0 -- their blurred twins (see Bitmask.cpp).
//
// Depends only on `atlas` and the handful of `opts` fields noted below, all of
// which are the same for every frame of a video -- callers build this ONCE
// (alongside the atlas itself) and reuse it across every call to generate(),
// rather than paying to rebuild it per frame. A still image still only builds
// it once, so this changes nothing there.
struct Model
{
    int glyphCount = 0;
    std::vector<float> inkWeight;
    std::vector<float> maskF;   // glyphCount * cellPx
    std::vector<float> wMeanByGlyph;
    std::vector<float> wStdByGlyph;
    float maxCoverage = 1.f;

    // Built only when softness > 0 && !allowBackground -- see BitmaskOptions::softness.
    bool soft = false;
    std::vector<float> blurMask;   // glyphCount * cellPx, empty if !soft
    std::vector<float> blurMaskMean;
    std::vector<float> blurMaskStd;
};

void buildModel(const GlyphAtlas& atlas, const BitmaskOptions& opts, Model& out);

// Every per-cell working buffer generate() needs that would otherwise be a fresh
// local -- reused across frames instead of allocated per call. Contents don't
// need to survive between calls, only the storage does.
struct Scratch
{
    std::vector<RGB> tile;
    std::vector<float> tileLuma;
    std::vector<float> blurLuma;
    std::vector<float> blurScratch;   // ImageFilters::blur's own scratch, reused through it
};

// `image` must already be exactly `outBuffer.width() * atlas.cellWidth()` x
// `outBuffer.height() * atlas.cellHeight()`, resampled AND dithered -- generate()
// used to do both itself, internally, on every call, which for a caller that
// already needed to know the plane's size to build one (every real caller) meant
// resampling an already-correctly-sized plane to its own size a second time.
// That's the caller's job now, once, not this function's, every call.
void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, const Model& model,
    Scratch& scratch, BitmaskOptions opts = {}
);

};   // namespace Bitmask
