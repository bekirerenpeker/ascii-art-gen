#include "bitmap/Descriptor.hpp"
#include <algorithm>
#include <cmath>

namespace Structure {

static constexpr float kPi = 3.14159265f;

static float at(const float* cell, int w, int h, int x, int y)
{
    return cell[std::clamp(x, 0, w - 1) + std::clamp(y, 0, h - 1) * w];
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

void buildDescriptor(const float* cell, int w, int h, DescriptorShape shape, Descriptor& out)
{
    const int bx = std::max(1, shape.blocksX);
    const int by = std::max(1, shape.blocksY);
    const int bins = std::max(2, shape.bins);

    out.orientation.assign((size_t)bx * by * bins, 0.f);
    out.mass.assign((size_t)bx * by, 0.f);
    out.orientationStrength = 0.f;
    out.massStrength = 0.f;

    if (!cell || w <= 0 || h <= 0) return;

    std::vector<int> counts((size_t)bx * by, 0);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // Which block a pixel lands in. Integer maths so a cell size that is
            // not a multiple of the block count still partitions cleanly.
            const int cx = std::min(bx - 1, x * bx / w);
            const int cy = std::min(by - 1, y * by / h);
            const size_t block = (size_t)cx + (size_t)cy * bx;

            out.mass[block] += cell[x + y * w];
            counts[block]++;

            const float gx = 3.f * (at(cell, w, h, x + 1, y + 1) - at(cell, w, h, x - 1, y + 1))
                           + 10.f * (at(cell, w, h, x + 1, y) - at(cell, w, h, x - 1, y))
                           + 3.f * (at(cell, w, h, x + 1, y - 1) - at(cell, w, h, x - 1, y - 1));

            const float gy = 3.f * (at(cell, w, h, x + 1, y + 1) - at(cell, w, h, x + 1, y - 1))
                           + 10.f * (at(cell, w, h, x, y + 1) - at(cell, w, h, x, y - 1))
                           + 3.f * (at(cell, w, h, x - 1, y + 1) - at(cell, w, h, x - 1, y - 1));

            const float mag = std::sqrt(gx * gx + gy * gy);
            if (mag <= 1e-9f) continue;

            // Folded to [0, pi): a stroke has no head or tail, so directions half
            // a turn apart are the same stroke and must land in the same bin.
            const float angle = std::fmod(std::atan2(gy, gx) + kPi, kPi);

            // Split across the two nearest bins by distance. Hard binning makes a
            // stroke jump between bins under a tiny rotation, which reads as noise.
            const float pos = angle / kPi * bins;
            const int b0 = std::min(bins - 1, (int)pos);
            const int b1 = (b0 + 1) % bins;
            const float frac = pos - (float)b0;

            float* hist = out.orientation.data() + block * bins;
            hist[b0] += mag * (1.f - frac);
            hist[b1] += mag * frac;
        }
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
