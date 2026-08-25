#pragma once

#include <cmath>

namespace Edges {

struct Gradient
{
    float gx, gy, mag;
};

// Scharr weights, 3+10+3 per side, so a full black-to-white step maxes one
// axis out at 16 * 255. Shared by every gradient-based detector in this
// folder -- `sample(x, y)` is the one thing that differs between them.
template <typename Sample>
Gradient scharrGradient(Sample&& sample, int x, int y)
{
    const float gx = 3.f * (sample(x + 1, y + 1) - sample(x - 1, y + 1))
                    + 10.f * (sample(x + 1, y) - sample(x - 1, y))
                    + 3.f * (sample(x + 1, y - 1) - sample(x - 1, y - 1));

    const float gy = 3.f * (sample(x + 1, y + 1) - sample(x + 1, y - 1))
                    + 10.f * (sample(x, y + 1) - sample(x, y - 1))
                    + 3.f * (sample(x - 1, y + 1) - sample(x - 1, y - 1));

    return {gx, gy, std::sqrt(gx * gx + gy * gy)};
}

};   // namespace Edges
