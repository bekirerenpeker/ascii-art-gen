#pragma once

#include "core/CellBuffer.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"

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

    // What every cell sits on when allowBackground is false, so the glyph is the
    // only thing carrying the picture. Ignored when the two-colour split is on,
    // since that solves a paper colour per cell instead of being handed one.
    RGB backgroundColor {0, 0, 0};
};

void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, BitmaskOptions opts = {}
);

};   // namespace Bitmask
