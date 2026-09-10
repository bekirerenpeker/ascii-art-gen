#include "core/Profiler.hpp"
#include "bitmap/Bitmask.hpp"
#include "filters/ImageFilters.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Bitmask {

static float luma(const RGB& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

void buildModel(const GlyphAtlas& atlas, const BitmaskOptions& opts, Model& out)
{
    const int cellPx = atlas.glpyhSize();
    const int atlasW = atlas.cellWidth();
    const int atlasH = atlas.cellHeight();
    const int glyphCount = atlas.glyphCount();
    out.glyphCount = glyphCount;

    if (cellPx <= 0 || glyphCount <= 0) return;
    ASCIIGEN_PROFILE("Bitmask::buildModel", "select");


    // Coverage is used as a weight rather than thresholded to 1-bit: downsampled
    // antialiased glyphs peak well below 255, so any fixed cutoff throws most of
    // the shape away.
    //
    // B2: maskF is the same masks as floats, built once here instead of a byte
    // load + /255.f on every pixel, per glyph, per cell in the loop below (the
    // main cost this glyph-scoring pass has).
    out.inkWeight.assign(glyphCount, 0.f);
    std::vector<float> inkWeightSq(glyphCount, 0.f);
    out.maskF.assign((size_t)glyphCount * (size_t)cellPx, 0.f);
    float maxCoverage = 0.f;
    for (int g = 0; g < glyphCount; g++) {
        const uint8_t* mask = atlas.getGlyphBegin(g);
        float* mf = out.maskF.data() + (size_t)g * (size_t)cellPx;
        for (int i = 0; i < cellPx; i++) {
            const float a = mask[i] / 255.f;
            mf[i] = a;
            out.inkWeight[g] += a;
            inkWeightSq[g] += a * a;
        }
        maxCoverage = std::max(maxCoverage, out.inkWeight[g] / cellPx);
    }

    // Glyphs only ink part of their cell and antialiasing dims them further, so
    // the densest one still averages well under full coverage. Brightness has to
    // be scaled into that real range or everything above it saturates onto the
    // single heaviest glyph.
    out.maxCoverage = maxCoverage > 0.f ? maxCoverage : 1.f;

    // B1: wMean and wStd depend only on g, not on the cell being scored, but sat
    // inside the per-cell glyph loop below regardless -- 45,700 redundant
    // recomputations of the same value per glyph, including a sqrt each. Built
    // once here alongside inkWeight instead.
    out.wMeanByGlyph.assign(glyphCount, 0.f);
    out.wStdByGlyph.assign(glyphCount, 0.f);
    for (int g = 0; g < glyphCount; g++) {
        const float wMean = out.inkWeight[g] / cellPx;
        out.wMeanByGlyph[g] = wMean;
        out.wStdByGlyph[g] = std::sqrt(std::max(0.f, inkWeightSq[g] - cellPx * wMean * wMean));
    }

    // Blurred twin of every glyph mask, built once. Matching these instead of the
    // raw masks is what buys misalignment tolerance: a stroke a pixel off still
    // overlaps, where the sharp masks would score it as a total miss.
    out.soft = opts.softness > 0.f && !opts.allowBackground;
    out.blurMask.clear();
    out.blurMaskMean.assign(glyphCount, 0.f);
    out.blurMaskStd.assign(glyphCount, 0.f);

    // F2: blur's own internal scratch buffer, reused across every glyph below
    // instead of allocated fresh each time. Model-local: only buildModel needs it,
    // never generate() itself.
    std::vector<float> blurScratch;

    if (out.soft) {
        out.blurMask.assign((size_t)glyphCount * (size_t)cellPx, 0.f);

        for (int g = 0; g < glyphCount; g++) {
            // B2: maskF already holds this glyph's mask as floats -- no need
            // for a second, separate conversion here.
            const float* sharp = out.maskF.data() + (size_t)g * (size_t)cellPx;
            float* dst = out.blurMask.data() + (size_t)g * (size_t)cellPx;
            ImageFilters::blur(sharp, dst, atlasW, atlasH, opts.blurRadius, blurScratch);

            float sum = 0.f, sumSq = 0.f;
            for (int i = 0; i < cellPx; i++) sum += dst[i], sumSq += dst[i] * dst[i];

            out.blurMaskMean[g] = sum / cellPx;
            out.blurMaskStd[g] =
                std::sqrt(std::max(0.f, sumSq - cellPx * out.blurMaskMean[g] * out.blurMaskMean[g]));
        }
    }
}

void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, const Model& model,
    Scratch& scratch, BitmaskOptions opts
)
{
    const int cellPx = atlas.glpyhSize();
    const int atlasW = atlas.cellWidth();
    const int atlasH = atlas.cellHeight();
    const int glyphCount = atlas.glyphCount();

    if (cellPx <= 0 || glyphCount <= 0 || outBuffer.width() <= 0 || outBuffer.height() <= 0) return;
    ASCIIGEN_PROFILE("Bitmask::generate", "select");


    const std::vector<float>& inkWeight = model.inkWeight;
    const std::vector<float>& maskF = model.maskF;
    const std::vector<float>& wMeanByGlyph = model.wMeanByGlyph;
    const std::vector<float>& wStdByGlyph = model.wStdByGlyph;
    const std::vector<float>& blurMask = model.blurMask;
    const std::vector<float>& blurMaskMean = model.blurMaskMean;
    const std::vector<float>& blurMaskStd = model.blurMaskStd;
    const float maxCoverage = model.maxCoverage;
    const bool soft = model.soft;

    // `image` is already resampled to exactly this grid AND dithered -- both are
    // the caller's job now (once, for whichever algorithm actually runs), not
    // this function's (every call). See Bitmask.hpp's note on generate().
    std::vector<RGB>& tile = scratch.tile;
    std::vector<float>& tileLuma = scratch.tileLuma;
    std::vector<float>& blurLuma = scratch.blurLuma;
    std::vector<float>& blurScratch = scratch.blurScratch;
    tile.resize(cellPx);
    tileLuma.resize(cellPx);
    blurLuma.resize(soft ? cellPx : 0);

    // toGrid always hands back a 3-channel plane sized exactly to the grid, so
    // the tile gather can walk it directly instead of bounds-checking and
    // re-branching on depth once per subpixel.
    const byte* const planePx = image.pixels;
    const size_t planeStride = (size_t)image.width * 3;

    for (int cy = 0; cy < outBuffer.height(); cy++) {
        for (int cx = 0; cx < outBuffer.width(); cx++) {
            float sumL = 0;
            for (int py = 0; py < atlasH; py++) {
                const byte* p = planePx + (size_t)(cy * atlasH + py) * planeStride
                                + (size_t)cx * atlasW * 3;

                for (int px = 0; px < atlasW; px++, p += 3) {
                    const RGB c {p[0], p[1], p[2]};
                    const int i = px + py * atlasW;
                    tile[i] = c;
                    tileLuma[i] = luma(c);
                    sumL += tileLuma[i];
                }
            }

            const float lumaMean = sumL / cellPx;
            float lumaVar = 0.f;
            for (int i = 0; i < cellPx; i++) {
                const float d = tileLuma[i] - lumaMean;
                lumaVar += d * d;
            }
            const float lumaStd = std::sqrt(lumaVar);

            // The tile gets the same treatment as the masks, or the two sides of
            // the correlation would be measuring detail at different scales.
            float blurMean = 0.f, blurStd = 0.f;
            if (soft) {
                ImageFilters::blur(
                    tileLuma.data(), blurLuma.data(), atlasW, atlasH, opts.blurRadius, blurScratch
                );

                float sum = 0.f;
                for (int i = 0; i < cellPx; i++) sum += blurLuma[i];
                blurMean = sum / cellPx;

                float var = 0.f;
                for (int i = 0; i < cellPx; i++) {
                    const float d = blurLuma[i] - blurMean;
                    var += d * d;
                }
                blurStd = std::sqrt(var);
            }

            float bestScore = -1e30f;
            float bestW = 0;
            int bestGlyph = 0;

            for (int g = 0; g < glyphCount; g++) {
                // B2: the float mask, precomputed once above -- no per-pixel
                // byte load + /255.f here anymore.
                const float* mf = maskF.data() + (size_t)g * (size_t)cellPx;
                const float w = inkWeight[g];

                float score;
                if (opts.allowBackground) {
                    // Two-colour split scored on luma only: structure is a
                    // luminance phenomenon, and a hue change at constant
                    // brightness is not an edge the eye reads as one.
                    float inkL = 0;
                    for (int i = 0; i < cellPx; i++) inkL += tileLuma[i] * mf[i];

                    const float m = cellPx - w;
                    const float paperL = sumL - inkL;

                    score = 0;
                    if (w > 0.f) score += inkL * inkL / w;
                    if (m > 0.f) score += paperL * paperL / m;
                } else {
                    // Per-pixel matching goes binary here -- ASCII has no even
                    // mid-density glyph, so a flat tile always lands on space or
                    // the heaviest glyph. Match mean coverage to mean brightness
                    // instead, and let shape correlation settle the rest.
                    float lw = 0;
                    for (int i = 0; i < cellPx; i++) lw += tileLuma[i] * mf[i];

                    // B1: precomputed alongside inkWeight above -- these depend
                    // only on g, not on this cell.
                    const float wMean = wMeanByGlyph[g];
                    const float wStd = wStdByGlyph[g];

                    const float coverErr = wMean / maxCoverage - lumaMean / 255.f;

                    float corr = 0.f;
                    if (wStd > 1e-4f && lumaStd > 1e-4f)
                        corr = (lw - cellPx * lumaMean * wMean) / (lumaStd * wStd);

                    // Same correlation, both sides blurred, so a stroke sitting a
                    // pixel off the image's still overlaps instead of scoring zero.
                    if (soft) {
                        const float* bm = blurMask.data() + (size_t)g * (size_t)cellPx;

                        float corrBlur = 0.f;
                        if (blurMaskStd[g] > 1e-4f && blurStd > 1e-4f) {
                            float lwBlur = 0;
                            for (int i = 0; i < cellPx; i++) lwBlur += blurLuma[i] * bm[i];

                            corrBlur = (lwBlur - cellPx * blurMean * blurMaskMean[g])
                                     / (blurStd * blurMaskStd[g]);
                        }

                        // Mixed, not added: the two weights sum to 1 so the coverage
                        // term keeps the same relative pull at any softness.
                        corr = (1.f - opts.softness) * corr + opts.softness * corrBlur;
                    }

                    score = corr - 4.f * coverErr * coverErr;
                }

                // Exact ties are common on flat tiles; break toward less ink so
                // a featureless cell renders as blank rather than a solid blob.
                const bool better =
                    score > bestScore + 1e-4f || (score > bestScore - 1e-4f && w < bestW);
                if (!better) continue;

                bestScore = score;
                bestW = w;
                bestGlyph = g;
            }

            Cell& cell = outBuffer.getAt(cx, cy);
            cell.glyphIndex = (uint16_t)bestGlyph;

            solveCellColor(
                tile.data(), atlas.getGlyphBegin(bestGlyph), cellPx, inkWeight[bestGlyph],
                {.allowBackground = opts.allowBackground, .brightnessGamma = opts.brightnessGamma},
                cell.fg, cell.bg
            );
        }
    }
}

}   // namespace Bitmask
