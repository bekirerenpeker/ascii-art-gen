#pragma once

#include "core/Image.hpp"
#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include <string>

namespace Ramp {

void generate(
    const Image& image, CellBuffer& outBuffer, Charset& outCharset,
    const std::string& ramp = " .:-=+*#%@"
);

};   // namespace Ramp
