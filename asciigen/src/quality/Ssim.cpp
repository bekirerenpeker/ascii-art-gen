#include "quality/Ssim.hpp"

namespace Quality {

static double luma(const PixelColor& c)
{
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

double computeSsim(const Image& a, const Image& b, int window, int stride)
{
    if (!a.pixels || !b.pixels) return 0.0;
    if (a.width != b.width || a.height != b.height) return 0.0;
    if (window < 2 || stride < 1) return 0.0;
    if (a.width < window || a.height < window) return 0.0;

    // Stabilisers from the original SSIM paper: (0.01*255)^2 and (0.03*255)^2.
    // They keep the ratio finite where a window is flat in both images.
    constexpr double kC1 = 6.5025;
    constexpr double kC2 = 58.5225;

    const double n = (double)window * (double)window;
    double total = 0.0;
    long long windows = 0;

    for (int y = 0; y + window <= a.height; y += stride) {
        for (int x = 0; x + window <= a.width; x += stride) {
            double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;

            for (int wy = 0; wy < window; wy++) {
                for (int wx = 0; wx < window; wx++) {
                    const double la = luma(a.getAt(x + wx, y + wy));
                    const double lb = luma(b.getAt(x + wx, y + wy));
                    sa += la;
                    sb += lb;
                    saa += la * la;
                    sbb += lb * lb;
                    sab += la * lb;
                }
            }

            const double ma = sa / n, mb = sb / n;
            const double va = saa / n - ma * ma;
            const double vb = sbb / n - mb * mb;
            const double cov = sab / n - ma * mb;

            total += ((2 * ma * mb + kC1) * (2 * cov + kC2))
                   / ((ma * ma + mb * mb + kC1) * (va + vb + kC2));
            windows++;
        }
    }

    return windows ? total / (double)windows : 0.0;
}

}   // namespace Quality
