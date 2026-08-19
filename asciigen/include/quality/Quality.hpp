#pragma once

#include "core/CellBuffer.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"

namespace Quality {

struct Report
{
    // Resolution the comparison ran at: cells times the atlas cell size.
    int width = 0, height = 0;

    // Root-mean-square luma error in 0-255 units. Lower is better.
    double lumaRmse = 0.0;

    // Same, averaged over the three colour channels. Separates "picked the wrong
    // glyph" from "picked the right glyph in the wrong colour".
    double colorRmse = 0.0;

    // Luma RMSE expressed in dB. Higher is better; capped at 99 for an exact match.
    double psnr = 0.0;

    // Structural similarity, 0 to 1, higher is better. The one that tracks whether
    // output actually looks right -- RMSE happily rewards a flat grey smear.
    double ssim = 0.0;
};

Report compare(const Image& source, const CellBuffer& buffer, const GlyphAtlas& atlas);

};   // namespace Quality
