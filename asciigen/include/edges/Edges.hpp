#pragma once

#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include "core/Image.hpp"

namespace Edges {

enum class Algorithm
{
    Scharr,
};

struct Options
{
    // Off by default: bitmask's correlation already matches glyph shape to tile
    // shape across the whole charset, so stamping 4 directional glyphs over it
    // only loses detail. Worth enabling for ramp, very low resolutions, or style.
    bool enabled = false;

    // Gradient operator. Scharr costs what Sobel costs with better rotational symmetry.
    Algorithm algorithm = Algorithm::Scharr;

    // Gradient samples per cell per axis; higher resolves finer structure inside a cell.
    int subsamples = 4;

    // Minimum edge strength, as a fraction of full black-to-white contrast, before
    // a glyph is replaced. The "how much edging" dial: raise it for outlines only.
    float threshold = 0.3f;

    // Fraction of a cell's edge energy that must agree on one direction. Rejects
    // texture, where the sub-samples disagree and no line glyph would represent it.
    float coherence = 0.55f;
};

inline Options options;

// Adds the four directional glyphs to the charset if it lacks them, then stamps
// them over cells that clear both gates. Returns true if the charset grew, in
// which case any atlas used for RENDERING must be rebuilt from it -- the atlas
// that drove selection does not, since selection is already finished.
bool apply(const Image& image, CellBuffer& buffer, Charset& charset);

};   // namespace Edges
