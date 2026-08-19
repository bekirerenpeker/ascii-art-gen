#pragma once

#include "core/Image.hpp"
#include "edges/EdgeField.hpp"

namespace Edges {

void detectScharr(const Image& image, int cols, int rows, int subsamples, EdgeField& out);

};   // namespace Edges
