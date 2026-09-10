#pragma once

#include "core/Image.hpp"
#include "edges/EdgeField.hpp"

namespace Edges {

// `planeScratch` is caller-owned scratch for the resampled luma plane this builds
// internally, reused across calls instead of a fresh Image made here each time.
void detectScharr(
    const Image& image, int cols, int rows, int subsamples, EdgeField& out, Image& planeScratch
);

};   // namespace Edges
