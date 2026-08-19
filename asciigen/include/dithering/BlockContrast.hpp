#pragma once

#include "core/Image.hpp"
#include "dithering/ContrastField.hpp"

namespace Dithering {

void computeBlockContrast(
    const Image& plane, int blockW, int blockH, float flat, float edge, float headroom,
    ContrastField& out
);

};   // namespace Dithering
