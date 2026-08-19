#pragma once

#include <vector>

namespace Structure {

struct DescriptorShape
{
    int blocksX = 2;
    int blocksY = 4;
    int bins = 4;
};

struct Descriptor
{
    std::vector<float> orientation;
    std::vector<float> mass;

    float orientationStrength = 0.f;
    float massStrength = 0.f;
};

void buildDescriptor(const float* cell, int w, int h, DescriptorShape shape, Descriptor& out);

float similarity(const std::vector<float>& a, const std::vector<float>& b);

};   // namespace Structure
