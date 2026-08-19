#pragma once

#include "core/Image.hpp"

namespace Dithering {

enum class Algorithm
{
    Bayer4,
};

struct Options
{
    // Algorithms call apply() unconditionally; this is what decides whether it acts.
    bool enabled = false;

    // Which bias pattern gets added. Bayer4 is ordered: stateless and parallel.
    Algorithm algorithm = Algorithm::Bayer4;

    // Tone steps the consumer downstream can actually resolve. Sets the bias
    // amplitude to 255/(levels-1), so a LOWER value dithers more visibly.
    int levels = 4;

    // Scale the bias down where the neighbourhood already carries detail. With
    // this off, a strong dither shakes clean edges and thin lines apart.
    bool adaptive = true;

    // Luma std-dev (0-255) at or below which a block is flat enough for full bias.
    float flatContrast = 10.f;

    // Luma std-dev at or above which the bias is suppressed entirely. Raise it to
    // let the dither reach further into textured areas, lower it to protect more.
    float edgeContrast = 45.f;
};

inline Options options;

void apply(Image& plane, int blockW = 1, int blockH = 1);

};   // namespace Dithering
