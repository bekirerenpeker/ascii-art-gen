#pragma once

#include "core/CellBuffer.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"

namespace Bitmask {

struct BitmaskOptions
{
    bool allowBackground = true;
    float brightnessGamma = 1.f;
};

void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, BitmaskOptions opts = {}
);

};   // namespace Bitmask
