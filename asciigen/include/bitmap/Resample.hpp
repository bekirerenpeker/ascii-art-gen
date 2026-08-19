#pragma once

#include "core/Image.hpp"

namespace Resample {

void toGrid(const Image& src, Image& outPlane, int outW, int outH);

};   // namespace Resample
