#pragma once

#include "core/Image.hpp"
#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include <string>

namespace Ramp {

// `image` must already be exactly `outBuffer.width()` x `outBuffer.height()`,
// resampled AND dithered -- one sample per cell is Ramp's whole working
// resolution, so the caller building that plane and Ramp needing it are the
// same size by construction; no reason for this to resample it again itself.
void generate(const Image& image, CellBuffer& outBuffer, Charset& outCharset,
              const std::string& ramp = " .:-=+*#%@");

};   // namespace Ramp
