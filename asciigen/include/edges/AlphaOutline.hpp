#pragma once

#include "core/Image.hpp"
#include "edges/EdgeField.hpp"

namespace Edges {

// `alpha` is a depth-1 image, one byte of coverage per source pixel, at the
// SOURCE's own resolution -- not the RGB plane the rest of the pipeline works
// from, which has already lost its alpha channel by the time edges run.
void detectAlphaOutline(const Image& alpha, int cols, int rows, int subsamples, EdgeField& out);

};   // namespace Edges
