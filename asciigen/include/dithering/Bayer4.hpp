#pragma once

#include "core/Image.hpp"

namespace Dithering {

void applyBayer4(Image& image, int levels, int blockW = 1, int blockH = 1);

};   // namespace Dithering
