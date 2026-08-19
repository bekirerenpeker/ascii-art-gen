#pragma once

#include "core/CellBuffer.hpp"
#include "font/GlyphAtlas.hpp"

namespace Despeckle {

void apply(CellBuffer& buffer, const GlyphAtlas& atlas, float strength);

};   // namespace Despeckle
