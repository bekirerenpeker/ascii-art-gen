#include "core/Color.hpp"
#include <cmath>

static uint8_t toByte(float v) { return (uint8_t)std::lround(std::clamp(v, 0.f, 255.f)); }

void solveCellColor(
    const RGB* tile, const uint8_t* mask, int cellPx, float inkWeight, CellColorOptions opts,
    RGB& outFg, RGB& outBg
)
{
    if (!tile || !mask || cellPx <= 0) return;

    float sumR = 0, sumG = 0, sumB = 0;
    float inkR = 0, inkG = 0, inkB = 0;

    for (int i = 0; i < cellPx; i++) {
        const float a = mask[i] / 255.f;
        sumR += tile[i].r, sumG += tile[i].g, sumB += tile[i].b;
        inkR += tile[i].r * a, inkG += tile[i].g * a, inkB += tile[i].b * a;
    }

    const float w = inkWeight;

    if (opts.allowBackground) {
        // Whatever the glyph does not cover is the paper, so its colour is just
        // the rest of the tile averaged over the leftover area.
        const float m = cellPx - w;

        outBg = m > 0.f ? RGB {toByte((sumR - inkR) / m), toByte((sumG - inkG) / m),
                               toByte((sumB - inkB) / m)}
                        : RGB {0, 0, 0};
        outFg = w > 0.f ? RGB {toByte(inkR / w), toByte(inkG / w), toByte(inkB / w)} : outBg;
        return;
    }

    float fr, fg, fb;
    if (w > 0.f) fr = inkR / w, fg = inkG / w, fb = inkB / w;
    else fr = sumR / cellPx, fg = sumG / cellPx, fb = sumB / cellPx;

    // Coverage already tracks brightness, so rendering it in a colour that also
    // tracks brightness darkens the cell twice over. A gamma below 1 wins some
    // back; unlike a coverage-derived multiplier it can never blow past white.
    if (opts.brightnessGamma != 1.f) {
        const float e = opts.brightnessGamma;
        fr = 255.f * std::pow(std::clamp(fr / 255.f, 0.f, 1.f), e);
        fg = 255.f * std::pow(std::clamp(fg / 255.f, 0.f, 1.f), e);
        fb = 255.f * std::pow(std::clamp(fb / 255.f, 0.f, 1.f), e);
    }

    // Always black. Selection has no business choosing a backdrop -- that is a
    // rendering decision, applied later with CellBuffer::fillBackground().
    outFg = {toByte(fr), toByte(fg), toByte(fb)};
    outBg = {0, 0, 0};
}
