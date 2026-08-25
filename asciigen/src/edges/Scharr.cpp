#include "edges/Scharr.hpp"
#include "bitmap/Resample.hpp"
#include "edges/GradientKernel.hpp"
#include <algorithm>
#include <cmath>

namespace Edges {

static constexpr float kPi = 3.14159265f;
static constexpr float kBucketArc = kPi / 4.f;

// Scharr weights are 3 + 10 + 3 per side, so a full black-to-white step maxes
// one axis out at 16 * 255.
static constexpr float kMaxResponse = 16.f * 255.f;

static float lumaAt(const Image& plane, int x, int y)
{
    x = std::clamp(x, 0, plane.width - 1);
    y = std::clamp(y, 0, plane.height - 1);
    const PixelColor c = plane.getAt(x, y);
    return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
}

void detectScharr(
    const Image& image, int cols, int rows, int subsamples, EdgeField& out, Image& planeScratch
)
{
    const int sub = std::max(1, subsamples);
    const int planeW = cols * sub;
    const int planeH = rows * sub;

    out.width = cols;
    out.height = rows;
    out.magnitude.assign((size_t)cols * (size_t)rows, 0.f);
    out.coherence.assign((size_t)cols * (size_t)rows, 0.f);
    out.bucket.assign((size_t)cols * (size_t)rows, 0);

    if (!image.pixels || cols <= 0 || rows <= 0) return;

    Resample::toGrid(image, planeScratch, planeW, planeH);
    const Image& plane = planeScratch;

    // A plane sample is not square in image space -- glyph cells are roughly 1:2 --
    // so raw gradients describe angles in a vertically squashed copy of the picture.
    // Each axis gets scaled by its own footprint before any angle is read off it.
    const float footX = (float)image.width / planeW;
    const float footY = (float)image.height / planeH;
    const float finest = std::min(footX, footY);
    const float scaleX = finest / footX;
    const float scaleY = finest / footY;

    for (int cy = 0; cy < rows; cy++) {
        for (int cx = 0; cx < cols; cx++) {
            float hist[4] = {0.f, 0.f, 0.f, 0.f};
            float sumMag = 0.f;

            for (int py = 0; py < sub; py++) {
                for (int px = 0; px < sub; px++) {
                    const int x = cx * sub + px;
                    const int y = cy * sub + py;

                    const auto g = scharrGradient(
                        [&](int sx, int sy) { return lumaAt(plane, sx, sy); }, x, y
                    );

                    // Strength stays in plane space so both axes are equally
                    // detectable; only the direction is corrected, or horizontal
                    // edges would need twice the contrast to clear the threshold.
                    const float mag = g.mag / kMaxResponse;

                    // Averaged over EVERY subsample, not just the ones with real
                    // gradient -- a single hot pixel in an otherwise flat cell (a
                    // specular highlight, a noise pixel) gets diluted down by the
                    // 15 flat neighbours around it instead of setting the whole
                    // cell's strength on its own, the way a peak would.
                    sumMag += mag;
                    if (mag <= 1e-6f) continue;

                    // The gradient points across the edge, so the line itself runs
                    // 90 degrees off it. Folded to [0, pi) -- a line has no polarity,
                    // and y grows upward here, so 45 degrees really is up-and-right.
                    float angle = std::atan2(g.gy * scaleY, g.gx * scaleX) + kPi * 0.5f;
                    angle = std::fmod(angle, kPi);
                    if (angle < 0.f) angle += kPi;

                    // Weighted histogram, never an average: 1 and 179 degrees mean
                    // nearly the same line but average to 90, perpendicular to both.
                    hist[(int)std::lround(angle / kBucketArc) & 3] += mag;
                }
            }

            int dominant = 0;
            for (int b = 1; b < 4; b++)
                if (hist[b] > hist[dominant]) dominant = b;

            const float total = hist[0] + hist[1] + hist[2] + hist[3];
            const size_t i = (size_t)cx + (size_t)cy * (size_t)cols;

            out.magnitude[i] = std::min(sumMag / (float)(sub * sub), 1.f);
            out.coherence[i] = total > 0.f ? hist[dominant] / total : 0.f;
            out.bucket[i] = dominant;
        }
    }
}

}   // namespace Edges
