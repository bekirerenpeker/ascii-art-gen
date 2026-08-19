#include "filters/CellFilters.hpp"
#include <algorithm>
#include <cmath>

namespace CellFilters {

static uint8_t toByte(float v) { return (uint8_t)std::clamp(std::lround(v), 0L, 255L); }

static RGB nearest(const std::vector<RGB>& palette, RGB c)
{
    RGB best = palette[0];
    float bestDist = 1e30f;

    for (const RGB& p : palette) {
        const float dr = (float)c.r - p.r;
        const float dg = (float)c.g - p.g;
        const float db = (float)c.b - p.b;

        // Weighted by the luma coefficients, so the match tracks how different
        // two colours look rather than how far apart they sit in the cube --
        // plain RGB distance badly overrates blue and underrates green.
        const float dist = 0.299f * dr * dr + 0.587f * dg * dg + 0.114f * db * db;
        if (dist >= bestDist) continue;

        bestDist = dist;
        best = p;
    }

    return best;
}

void paletteMap(CellBuffer& buffer, const std::vector<RGB>& palette, float strength)
{
    if (palette.empty() || strength <= 0.f) return;

    const float t = std::clamp(strength, 0.f, 1.f);

    auto blend = [&](RGB c) {
        const RGB p = nearest(palette, c);
        return RGB {toByte(c.r + (p.r - c.r) * t), toByte(c.g + (p.g - c.g) * t),
                    toByte(c.b + (p.b - c.b) * t)};
    };

    for (int y = 0; y < buffer.height(); y++) {
        for (int x = 0; x < buffer.width(); x++) {
            Cell& cell = buffer.getAt(x, y);
            cell.fg = blend(cell.fg);
            cell.bg = blend(cell.bg);
        }
    }
}

}   // namespace CellFilters
