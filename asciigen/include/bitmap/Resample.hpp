#pragma once

#include "core/Image.hpp"

namespace Resample {

// Auto picks stb's own default by direction -- Mitchell shrinking, Catmull-Rom
// growing, both real cubic interpolation. Box and Triangle force one filter
// both ways instead. Box is not a reconstruction of this file's old hand-rolled
// per-pixel average -- measured meaningfully different from it -- so treat it
// as "the other stb filter", not "the safe old behaviour".
enum class Filter
{
    Auto,
    Box,
    Triangle,
};

void toGrid(const Image& src, Image& outPlane, int outW, int outH, Filter filter = Filter::Auto);

};   // namespace Resample
