#include "edges/AlphaOutline.hpp"
#include "edges/GradientKernel.hpp"
#include <algorithm>
#include <cmath>

namespace Edges {

static constexpr float kPi = 3.14159265f;
static constexpr float kBucketArc = kPi / 4.f;
static constexpr float kMaxResponse = 16.f * 255.f;

// Plain box downsample of a single coverage byte per pixel -- Resample::toGrid
// does the equivalent job for RGB weighted BY alpha, which is a different
// thing from resampling alpha itself.
static void resampleAlpha(const Image& alpha, Image& outPlane, int outW, int outH)
{
    if (outPlane.width != outW || outPlane.height != outH || outPlane.depth != 1)
        outPlane = Image(outW, outH, 1);
    if (!alpha.pixels || outW <= 0 || outH <= 0) return;

    const float scaleX = alpha.width / (float)outW;
    const float scaleY = alpha.height / (float)outH;

    const byte* const src = alpha.pixels;
    const int srcW = alpha.width, srcH = alpha.height;
    byte* const dst = outPlane.pixels;

    for (int y = 0; y < outH; y++) {
        const int y0 = (int)(y * scaleY);
        const int y1 = std::min(srcH, std::max(y0 + 1, (int)((y + 1) * scaleY)));

        for (int x = 0; x < outW; x++) {
            const int x0 = (int)(x * scaleX);
            const int x1 = std::min(srcW, std::max(x0 + 1, (int)((x + 1) * scaleX)));

            int sum = 0, count = 0;
            for (int sy = y0; sy < y1; sy++) {
                const byte* row = src + (size_t)sy * srcW + x0;
                for (int sx = x0; sx < x1; sx++, row++) sum += *row, count++;
            }

            dst[(size_t)x + (size_t)y * outW] = count ? (byte)(sum / count) : 0;
        }
    }
}

static float alphaAt(const Image& plane, int x, int y)
{
    x = std::clamp(x, 0, plane.width - 1);
    y = std::clamp(y, 0, plane.height - 1);
    return plane.pixels[x + y * plane.width];
}

void detectAlphaOutline(
    const Image& alpha, int cols, int rows, int subsamples, EdgeField& out, Image& planeScratch
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

    if (!alpha.pixels || cols <= 0 || rows <= 0) return;

    resampleAlpha(alpha, planeScratch, planeW, planeH);
    const Image& plane = planeScratch;

    // Same non-square-pixel correction as Scharr: a plane sample covers a
    // roughly 1:2 footprint of the source, so raw gradients read angles off a
    // vertically squashed copy unless each axis is scaled back first.
    const float footX = (float)alpha.width / planeW;
    const float footY = (float)alpha.height / planeH;
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
                        [&](int sx, int sy) { return alphaAt(plane, sx, sy); }, x, y
                    );

                    const float mag = g.mag / kMaxResponse;
                    sumMag += mag;
                    if (mag <= 1e-6f) continue;

                    float angle = std::atan2(g.gy * scaleY, g.gx * scaleX) + kPi * 0.5f;
                    angle = std::fmod(angle, kPi);
                    if (angle < 0.f) angle += kPi;

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

};   // namespace Edges
