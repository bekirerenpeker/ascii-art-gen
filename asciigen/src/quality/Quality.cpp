#include "quality/Quality.hpp"
#include "bitmap/Resample.hpp"
#include "output/ImageRenderer.hpp"
#include "quality/Ssim.hpp"
#include <cmath>

namespace Quality {

static double luma(const PixelColor& c)
{
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

Report compare(const Image& source, const CellBuffer& buffer, const GlyphAtlas& atlas)
{
    Report report;

    // Same compositing the PNG export uses, at its natural size: the comparison
    // has to line up with the resampled source, so no padding or centring here.
    Image rendered;
    ImageRenderer::render(buffer, atlas, rendered);
    if (!rendered.pixels || !source.pixels) return report;

    // Deliberately the undithered plane. Dither is a means, not the target, so
    // measuring against a dithered reference would score it as free by definition
    // instead of asking whether it bought any real fidelity.
    Image target;
    Resample::toGrid(source, target, rendered.width, rendered.height);

    report.width = rendered.width;
    report.height = rendered.height;

    double lumaSq = 0.0, colorSq = 0.0;
    const double n = (double)rendered.width * (double)rendered.height;

    for (int y = 0; y < rendered.height; y++) {
        for (int x = 0; x < rendered.width; x++) {
            const PixelColor p = rendered.getAt(x, y);
            const PixelColor q = target.getAt(x, y);

            const double dl = luma(p) - luma(q);
            lumaSq += dl * dl;

            const double dr = (double)p.r - q.r;
            const double dg = (double)p.g - q.g;
            const double db = (double)p.b - q.b;
            colorSq += (dr * dr + dg * dg + db * db) / 3.0;
        }
    }

    report.lumaRmse = std::sqrt(lumaSq / n);
    report.colorRmse = std::sqrt(colorSq / n);
    report.psnr = report.lumaRmse > 1e-9 ? 20.0 * std::log10(255.0 / report.lumaRmse) : 99.0;
    report.ssim = computeSsim(rendered, target);

    return report;
}

}   // namespace Quality
