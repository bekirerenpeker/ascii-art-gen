#include "core/CellBuffer.hpp"
#include <algorithm>
#include <cmath>

RGB CellBuffer::suggestedBackground(float darken, float lumaThreshold) const
{
    double r = 0, g = 0, b = 0;
    long long lit = 0;

    for (const Cell& cell : m_cells) {
        const float luma = 0.299f * cell.fg.r + 0.587f * cell.fg.g + 0.114f * cell.fg.b;
        if (luma < lumaThreshold) continue;

        r += cell.fg.r, g += cell.fg.g, b += cell.fg.b;
        lit++;
    }

    if (lit == 0) return {0, 0, 0};

    auto scale = [&](double sum) {
        return (uint8_t)std::clamp(std::lround(sum / (double)lit * darken), 0L, 255L);
    };

    return {scale(r), scale(g), scale(b)};
}
