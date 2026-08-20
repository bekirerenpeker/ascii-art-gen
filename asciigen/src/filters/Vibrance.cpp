#include "core/Profiler.hpp"
#include "filters/CellFilters.hpp"
#include <algorithm>
#include <cmath>

namespace CellFilters {

static uint8_t toByte(float v) { return (uint8_t)std::clamp(std::lround(v), 0L, 255L); }

static RGB boost(RGB c, float amount)
{
    const float r = c.r, g = c.g, b = c.b;
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float saturation = mx > 0.f ? (mx - mn) / mx : 0.f;

    // Weakest colours get the most, vivid ones almost nothing. That inverse is
    // the whole point: flat saturation pushes strong colours into clipping while
    // the dull midtones, which are what actually read as washed out, barely move.
    const float gain = 1.f + amount * (1.f - saturation);
    const float l = 0.299f * r + 0.587f * g + 0.114f * b;

    return {toByte(l + (r - l) * gain), toByte(l + (g - l) * gain), toByte(l + (b - l) * gain)};
}

void vibrance(CellBuffer& buffer, float amount)
{
    if (amount == 0.f) return;
    ASCIIGEN_PROFILE("vibrance", "filter");


    for (int y = 0; y < buffer.height(); y++) {
        for (int x = 0; x < buffer.width(); x++) {
            Cell& cell = buffer.getAt(x, y);
            cell.fg = boost(cell.fg, amount);
            cell.bg = boost(cell.bg, amount);
        }
    }
}

}   // namespace CellFilters
