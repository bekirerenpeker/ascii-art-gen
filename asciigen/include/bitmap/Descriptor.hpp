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

// massCountsScratch is caller-owned and reused across calls (T2) -- resized
// in place rather than allocated fresh every glyph and every cell.
//
// gradientStride > 1 (el-3) skips the gradient -- and so the orientation
// histogram's contribution -- for interior pixels not on a stride-aligned
// row and column, while mass still accumulates from every pixel regardless.
// A quality/speed trade, off by default (1 == every pixel, identical to
// before this existed): the orientation histogram becomes a coarser sample
// of the same cell, which can shift which glyph a tile scores best against.
//
// fastAtan (el-4) swaps std::atan2 + std::fmod for a polynomial
// approximation good to a fraction of a degree -- see Descriptor.cpp's
// fastFoldedAngle. Off by default. Only ever changes which of two adjacent
// bins a pixel's magnitude leans towards; it cannot move a pixel more than
// one bin from where the exact angle would have put it.
void buildDescriptor(
    const float* cell, int w, int h, DescriptorShape shape, Descriptor& out,
    std::vector<int>& massCountsScratch, int gradientStride, bool fastAtan
);

float similarity(const std::vector<float>& a, const std::vector<float>& b);

};   // namespace Structure
