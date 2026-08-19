#include "filters/CellFilters.hpp"
#include <algorithm>
#include <cmath>

namespace CellFilters {

static uint8_t toByte(float v) { return (uint8_t)std::clamp(std::lround(v), 0L, 255L); }

static RGB lift(RGB c, float gain, float gamma)
{
    auto one = [&](float v) {
        v = std::clamp(v * gain / 255.f, 0.f, 1.f);
        return toByte(255.f * std::pow(v, gamma));
    };

    return {one(c.r), one(c.g), one(c.b)};
}

void brightness(CellBuffer& buffer, float gain, float gamma)
{
    if (gain == 1.f && gamma == 1.f) return;
    if (gain < 0.f || gamma <= 0.f) return;

    for (int y = 0; y < buffer.height(); y++) {
        for (int x = 0; x < buffer.width(); x++) {
            Cell& cell = buffer.getAt(x, y);
            cell.fg = lift(cell.fg, gain, gamma);
            cell.bg = lift(cell.bg, gain, gamma);
        }
    }
}

}   // namespace CellFilters
