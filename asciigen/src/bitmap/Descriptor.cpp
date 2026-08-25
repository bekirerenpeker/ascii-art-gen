#include "bitmap/Descriptor.hpp"
#include <algorithm>
#include <cmath>

namespace Structure {

static constexpr float kPi = 3.14159265f;
static constexpr float kHalfPi = kPi * 0.5f;

static float at(const float* cell, int w, int h, int x, int y)
{
    return cell[std::clamp(x, 0, w - 1) + std::clamp(y, 0, h - 1) * w];
}

// el-4: minimax approximation of atan(z) for z in [0, 1], good to a fraction
// of a degree -- see https://mazzo.li/posts/vectorized-atan2.html for the
// derivation. Nowhere near IEEE atan2's guarantees, but the bins this feeds
// are 45 degrees wide by default, so an error two orders of magnitude
// smaller than a bin cannot itself explain a pixel landing in the wrong one.
static float fastAtanApprox(float z)
{
    return (0.97239411f - 0.19194795f * z * z) * z;
}

// Undirected line angle of (gx, gy), folded to [0, pi) -- an approximation
// of std::fmod(std::atan2(gy, gx) + kPi, kPi). A stroke has no head or tail,
// so flipping the vector into the upper half-plane before measuring loses
// nothing: the folded angle of v and -v is identical by construction, which
// is what lets this stay in the cheap single-octant polynomial above instead
// of needing one valid over the full circle.
static float fastFoldedAngle(float gx, float gy)
{
    if (gy < 0.f || (gy == 0.f && gx < 0.f)) {
        gx = -gx;
        gy = -gy;
    }

    const float ax = std::fabs(gx);
    const float angle = ax > gy ? fastAtanApprox(gy / ax) : kHalfPi - fastAtanApprox(ax / gy);
    return gx < 0.f ? kPi - angle : angle;
}

// Mean-centre, then scale to unit length, reporting how much of the original
// vector survived the centring. That ratio is the whole confidence story: a
// tile whose blocks all read the same, or whose edges point every which way,
// centres to nothing and ends up agreeing with no glyph in particular.
static float centreAndNormalize(std::vector<float>& v)
{
    if (v.empty()) return 0.f;

    float mean = 0.f, raw = 0.f;
    for (float f : v) mean += f;
    mean /= (float)v.size();

    for (float f : v) raw += f * f;
    raw = std::sqrt(raw);

    float sumSq = 0.f;
    for (float& f : v) {
        f -= mean;
        sumSq += f * f;
    }

    const float len = std::sqrt(sumSq);
    if (len <= 1e-9f) {
        std::fill(v.begin(), v.end(), 0.f);
        return 0.f;
    }

    for (float& f : v) f /= len;

    return raw > 1e-9f ? std::clamp(len / raw, 0.f, 1.f) : 0.f;
}

void buildDescriptor(
    const float* cell, int w, int h, DescriptorShape shape, Descriptor& out,
    std::vector<int>& massCountsScratch, int gradientStride, bool fastAtan
)
{
    gradientStride = std::max(1, gradientStride);
    const int bx = std::max(1, shape.orientBlocksX);
    const int by = std::max(1, shape.orientBlocksY);
    const int bins = std::max(2, shape.bins);

    const int mx = std::max(1, shape.massBlocksX);
    const int my = std::max(1, shape.massBlocksY);

    out.orientation.assign((size_t)bx * by * bins, 0.f);
    out.mass.assign((size_t)mx * my, 0.f);
    out.orientationStrength = 0.f;
    out.massStrength = 0.f;

    if (!cell || w <= 0 || h <= 0) return;

    // T2: caller-owned, so this resizes in place instead of allocating fresh --
    // buildDescriptor runs once per glyph and once per cell, tens of thousands
    // of times a frame.
    massCountsScratch.assign((size_t)mx * my, 0);
    int* counts = massCountsScratch.data();

    // Mass alone -- every pixel, always, regardless of gradientStride. Cheap
    // (no transcendentals), and it is what resolves shading on flat surfaces,
    // so el-3 never touches it.
    auto accumulateMass = [&](int x, int y) __attribute__((always_inline)) {
        const int gx_ = std::min(mx - 1, x * mx / w);
        const int gy_ = std::min(my - 1, y * my / h);
        const size_t massBlock = (size_t)gx_ + (size_t)gy_ * mx;

        out.mass[massBlock] += cell[x + y * w];
        counts[massBlock]++;
    };

    // Orientation bin, magnitude and histogram spread for one pixel, given its
    // gradient -- shared between the fast interior path and the clamped
    // border one below so there is exactly one copy of it.
    auto accumulateOrientation = [&](int x, int y, float gx, float gy) __attribute__((always_inline)) {
        // Which block a pixel lands in. Integer maths so a cell size that is
        // not a multiple of the block count still partitions cleanly.
        const int cx = std::min(bx - 1, x * bx / w);
        const int cy = std::min(by - 1, y * by / h);
        const size_t block = (size_t)cx + (size_t)cy * bx;

        const float mag = std::sqrt(gx * gx + gy * gy);
        if (mag <= 1e-9f) return;

        // Folded to [0, pi): a stroke has no head or tail, so directions half a
        // turn apart are the same stroke and must land in the same bin.
        const float angle =
            fastAtan ? fastFoldedAngle(gx, gy) : std::fmod(std::atan2(gy, gx) + kPi, kPi);

        // Split across the two nearest bins by distance. Hard binning makes a
        // stroke jump between bins under a tiny rotation, which reads as noise.
        const float pos = angle / kPi * bins;
        const int b0 = std::min(bins - 1, (int)pos);
        const int b1 = (b0 + 1) % bins;
        const float frac = pos - (float)b0;

        float* hist = out.orientation.data() + block * bins;
        hist[b0] += mag * (1.f - frac);
        hist[b1] += mag * frac;
    };

    // T1: the clamped, 8-neighbour-fetching path -- unavoidable at the cell's
    // own edge, where a neighbour can fall outside it. Never subsampled: the
    // border is already a minority of pixels, and el-3 only targets the
    // interior's much larger pixel count.
    auto borderGradient = [&](int x, int y) __attribute__((always_inline)) {
        accumulateMass(x, y);

        const float tl = at(cell, w, h, x - 1, y - 1);
        const float tm = at(cell, w, h, x, y - 1);
        const float tr = at(cell, w, h, x + 1, y - 1);
        const float ml = at(cell, w, h, x - 1, y);
        const float mr = at(cell, w, h, x + 1, y);
        const float bl = at(cell, w, h, x - 1, y + 1);
        const float bm = at(cell, w, h, x, y + 1);
        const float br = at(cell, w, h, x + 1, y + 1);

        const float gx = 3.f * (br - bl) + 10.f * (mr - ml) + 3.f * (tr - tl);
        const float gy = 3.f * (br - tr) + 10.f * (bm - tm) + 3.f * (bl - tl);
        accumulateOrientation(x, y, gx, gy);
    };

    // The old code called at() 12 times per pixel -- unconditionally clamped,
    // and 4 of the 12 re-fetched a neighbour the OTHER gradient term already
    // had, since gx and gy share 4 of their 8 corners. Every pixel not on the
    // cell's own border needs neither: its 8 neighbours are always in bounds,
    // so they are read once each, straight off the row, no clamp and no
    // redundant fetch. The outer loop stays strict row-major (y then x) so
    // this changes nothing about the ORDER mass and orientation accumulate in
    // -- only how each pixel's gradient gets computed, and (el-3) whether it
    // gets computed at all.
    for (int y = 0; y < h; y++) {
        if (y == 0 || y == h - 1) {
            for (int x = 0; x < w; x++) borderGradient(x, y);
            continue;
        }

        const float* rowT = cell + (size_t)(y - 1) * w;
        const float* rowM = cell + (size_t)y * w;
        const float* rowB = cell + (size_t)(y + 1) * w;

        borderGradient(0, y);

        // el-3: mass still accumulates every pixel; the gradient -- the
        // transcendental-heavy part -- only on a stride-aligned grid within
        // the interior. gradientStride == 1 makes every pixel stride-aligned,
        // which is why the default is exactly the pre-el-3 behaviour.
        const bool strideRow = (y - 1) % gradientStride == 0;
        for (int x = 1; x < w - 1; x++) {
            accumulateMass(x, y);

            if (gradientStride > 1 && ((x - 1) % gradientStride != 0 || !strideRow)) continue;

            const float gx = 3.f * (rowB[x + 1] - rowB[x - 1]) + 10.f * (rowM[x + 1] - rowM[x - 1])
                            + 3.f * (rowT[x + 1] - rowT[x - 1]);
            const float gy = 3.f * (rowB[x + 1] - rowT[x + 1]) + 10.f * (rowB[x] - rowT[x])
                            + 3.f * (rowB[x - 1] - rowT[x - 1]);
            accumulateOrientation(x, y, gx, gy);
        }

        if (w > 1) borderGradient(w - 1, y);
    }

    for (size_t i = 0; i < out.mass.size(); i++)
        if (counts[i]) out.mass[i] /= (float)counts[i];

    // Centred, not just scaled. Without this both vectors are all-positive and
    // the dot product answers "are these both bright" rather than "is the ink in
    // the same place" -- which lets any flat tile match the densest glyph.
    out.orientationStrength = centreAndNormalize(out.orientation);
    out.massStrength = centreAndNormalize(out.mass);
}

float similarity(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size()) return 0.f;

    float dot = 0.f;
    for (size_t i = 0; i < a.size(); i++) dot += a[i] * b[i];

    return dot;
}

}   // namespace Structure
