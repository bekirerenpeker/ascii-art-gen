#pragma once

#include "bitmap/Resample.hpp"
#include "core/Image.hpp"
#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include <string>

namespace Ramp {

void generate(
    const Image& image, CellBuffer& outBuffer, Charset& outCharset,
    const std::string& ramp = " .:-=+*#%@", Resample::Filter resampleFilter = Resample::Filter::Auto
);

};   // namespace Ramp
