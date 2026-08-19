#pragma once

#include "core/Image.hpp"
#include "dithering/ContrastField.hpp"

namespace Dithering {

void applyBayer4(Image& image, int levels, int blockW, int blockH, const ContrastField& gate);

};   // namespace Dithering
