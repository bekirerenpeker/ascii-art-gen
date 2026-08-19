#pragma once

#include <vector>

namespace Edges {

struct EdgeField
{
    int width = 0, height = 0;
    std::vector<float> magnitude;
    std::vector<float> coherence;
    std::vector<int> bucket;
};

};   // namespace Edges
