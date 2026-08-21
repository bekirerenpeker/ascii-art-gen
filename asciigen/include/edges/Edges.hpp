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
    //
    // This is a MEAN over every sub-sample in the cell, not the strongest one --
    // deliberately, so one bright pixel can no longer flag a whole flat cell on
    // its own. That also means it reads lower than it used to for the same
    // picture; 0.15 here is roughly what 0.3 was before.
    float threshold = 0.15f;

    // Fraction of a cell's edge energy that must agree on one direction. Rejects
    // texture, where the sub-samples disagree and no line glyph would represent it.
    float coherence = 0.55f;

    // A cell below `threshold` still survives if it clears `threshold * hysteresis`
    // AND touches a cell that cleared `threshold` outright. Given as a ratio, not
    // an absolute value, so it stays sane however `threshold` itself is set.
    float hysteresis = 0.5f;

    // Collapses a gradient ridge -- a real edge blurred across several cells by
    // resampling or depth of field -- down to the one cell that is genuinely the
    // peak, before threshold sees the rest. This is what keeps an outline one
    // character wide instead of three or four; turning it off restores the old
    // thick-outline behaviour.
    bool nms = true;
};

inline Options options;

// Alpha channel edges are a separate pass, not a third Algorithm: they read a
// different input (the source's own alpha, not the RGB match plane) and only
// do anything on a source that actually carries transparency.
struct AlphaOptions
{
    // Off by default. Where it does apply, the alpha channel already IS the
    // object's true silhouette -- no inference needed, unlike Scharr's gradient
    // which only ever guesses at where "inside" ends. Stamped AFTER Scharr, so
    // it has the final say over any cell both passes touch.
    bool enabled = false;

    // How much alpha has to swing across a cell, as a fraction of a full
    // transparent-to-opaque step.
    float threshold = 0.5f;

    // Same meaning as Options::coherence, applied to the alpha gradient's own
    // direction agreement.
    float coherence = 0.6f;
};

inline AlphaOptions alphaOptions;

// Adds the four directional glyphs to the charset if it lacks them, then stamps
// them over cells that clear every gate for whichever pass(es) are enabled.
// `alpha` may be an empty Image if the source has none or alphaOptions is off.
// Returns true if the charset grew, in which case any atlas used for RENDERING
// must be rebuilt from it -- the atlas that drove selection does not, since
// selection is already finished.
bool apply(const Image& image, CellBuffer& buffer, Charset& charset, const Image& alpha);

};   // namespace Edges
