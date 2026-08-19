#pragma once

#include <vector>

namespace Structure {

// The two terms want opposite things, so they get their own grids. Orientation
// wants coarse blocks: a stroke's direction is a property of a region, and
// pooling is what makes it robust. Mass wants fine blocks, because surface
// shading is only distinguishable per pixel -- at one pixel per block the mass
// term becomes plain per-pixel correlation.
struct DescriptorShape
{
    int orientBlocksX = 2;
    int orientBlocksY = 4;
    int bins = 4;

    int massBlocksX = 8;
    int massBlocksY = 16;
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
