#pragma once

#include "core/Image.hpp"

namespace Dithering {

enum class Algorithm
{
    Bayer4,
};

struct Options
{
    bool enabled = false;
    Algorithm algorithm = Algorithm::Bayer4;
    int levels = 32;
};

inline Options options;

void apply(Image& plane, int blockW = 1, int blockH = 1);

};   // namespace Dithering
