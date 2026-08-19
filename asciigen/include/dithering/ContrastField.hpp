#pragma once

#include <cstddef>
#include <vector>

namespace Dithering {

struct ContrastField
{
    int cols = 0, rows = 0;
    std::vector<float> amplitude;

    float at(int blockX, int blockY) const
    {
        if (amplitude.empty()) return 1.f;
        if (blockX < 0 || blockY < 0 || blockX >= cols || blockY >= rows) return 1.f;
        return amplitude[(size_t)blockX + (size_t)blockY * (size_t)cols];
    }
};

};   // namespace Dithering
