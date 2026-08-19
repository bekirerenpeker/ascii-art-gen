#pragma once

#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include "core/Image.hpp"

namespace Edges {

enum class Algorithm
{
    Scharr,
};

struct Options
{
    bool enabled = false;
    Algorithm algorithm = Algorithm::Scharr;
    int subsamples = 4;
    float threshold = 0.3f;
    float coherence = 0.55f;
};

inline Options options;

void apply(const Image& image, CellBuffer& buffer, const Charset& charset);

};   // namespace Edges
