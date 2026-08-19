#include "bitmap/Bitmask.hpp"
#include "bitmap/Resample.hpp"
#include "dithering/Dithering.hpp"
#include "filters/Blur.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Bitmask {

static float luma(const RGB& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

static uint8_t toByte(float v) { return (uint8_t)std::round(std::clamp(v, 0.f, 255.f)); }

void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, BitmaskOptions opts
)
{
    const int cellPx = atlas.glpyhSize();
    const int atlasW = atlas.cellWidth();
    const int atlasH = atlas.cellHeight();
    const int glyphCount = atlas.glyphCount();

    if (cellPx <= 0 || glyphCount <= 0 || outBuffer.width() <= 0 || outBuffer.height() <= 0) return;

    // Coverage is used as a weight rather than thresholded to 1-bit: downsampled
    // antialiased glyphs peak well below 255, so any fixed cutoff throws most of
    // the shape away.
    std::vector<float> inkWeight(glyphCount, 0.f);
    std::vector<float> inkWeightSq(glyphCount, 0.f);
    float maxCoverage = 0.f;
    for (int g = 0; g < glyphCount; g++) {
        const uint8_t* mask = atlas.getGlyphBegin(g);
        for (int i = 0; i < cellPx; i++) {
            const float a = mask[i] / 255.f;
            inkWeight[g] += a;
            inkWeightSq[g] += a * a;
        }
        maxCoverage = std::max(maxCoverage, inkWeight[g] / cellPx);
    }

    // Glyphs only ink part of their cell and antialiasing dims them further, so
    // the densest one still averages well under full coverage. Brightness has to
    // be scaled into that real range or everything above it saturates onto the
    // single heaviest glyph.
    if (maxCoverage <= 0.f) maxCoverage = 1.f;

    // Blurred twin of every glyph mask, built once. Matching these instead of the
    // raw masks is what buys misalignment tolerance: a stroke a pixel off still
    // overlaps, where the sharp masks would score it as a total miss.
    const bool soft = opts.softness > 0.f && !opts.allowBackground;
    std::vector<float> blurMask;
    std::vector<float> blurMaskMean(glyphCount, 0.f);
    std::vector<float> blurMaskStd(glyphCount, 0.f);

    if (soft) {
        blurMask.resize((size_t)glyphCount * (size_t)cellPx);
        std::vector<float> sharp(cellPx);

        for (int g = 0; g < glyphCount; g++) {
            const uint8_t* mask = atlas.getGlyphBegin(g);
            for (int i = 0; i < cellPx; i++) sharp[i] = mask[i] / 255.f;

            float* dst = blurMask.data() + (size_t)g * (size_t)cellPx;
            Blur::box(sharp.data(), dst, atlasW, atlasH, opts.blurRadius);

            float sum = 0.f, sumSq = 0.f;
            for (int i = 0; i < cellPx; i++) sum += dst[i], sumSq += dst[i] * dst[i];

            blurMaskMean[g] = sum / cellPx;
            blurMaskStd[g] =
                std::sqrt(std::max(0.f, sumSq - cellPx * blurMaskMean[g] * blurMaskMean[g]));
        }
    }

    // One resample for the whole frame at atlas resolution, then dither it: the
    // plane is where tones actually get quantised down to glyph coverages.
    Image plane;
    Resample::toGrid(image, plane, outBuffer.width() * atlasW, outBuffer.height() * atlasH);
    Dithering::apply(plane, atlasW, atlasH);

    std::vector<RGB> tile(cellPx);
    std::vector<float> tileLuma(cellPx);
    std::vector<float> blurLuma(soft ? cellPx : 0);

    for (int cy = 0; cy < outBuffer.height(); cy++) {
        for (int cx = 0; cx < outBuffer.width(); cx++) {
            float sumR = 0, sumG = 0, sumB = 0, sumL = 0;
            for (int py = 0; py < atlasH; py++) {
                for (int px = 0; px < atlasW; px++) {
                    const PixelColor p = plane.getAt(cx * atlasW + px, cy * atlasH + py);
                    const RGB c {p.r, p.g, p.b};
                    const int i = px + py * atlasW;
                    tile[i] = c;
                    tileLuma[i] = luma(c);
                    sumR += c.r, sumG += c.g, sumB += c.b;
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
                Blur::box(tileLuma.data(), blurLuma.data(), atlasW, atlasH, opts.blurRadius);

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
                const uint8_t* mask = atlas.getGlyphBegin(g);
                const float w = inkWeight[g];

                float score;
                if (opts.allowBackground) {
                    // Two-colour split scored on luma only: structure is a
                    // luminance phenomenon, and a hue change at constant
                    // brightness is not an edge the eye reads as one.
                    float inkL = 0;
                    for (int i = 0; i < cellPx; i++) inkL += tileLuma[i] * (mask[i] / 255.f);

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
                    for (int i = 0; i < cellPx; i++) lw += tileLuma[i] * (mask[i] / 255.f);

                    const float wMean = w / cellPx;
                    const float wStd = std::sqrt(std::max(0.f, inkWeightSq[g] - cellPx * wMean * wMean));

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

            const uint8_t* mask = atlas.getGlyphBegin(bestGlyph);
            const float w = inkWeight[bestGlyph];

            float inkR = 0, inkG = 0, inkB = 0;
            for (int i = 0; i < cellPx; i++) {
                const float a = mask[i] / 255.f;
                inkR += tile[i].r * a, inkG += tile[i].g * a, inkB += tile[i].b * a;
            }

            Cell& cell = outBuffer.getAt(cx, cy);
            cell.glyphIndex = (uint16_t)bestGlyph;

            if (opts.allowBackground) {
                const float m = cellPx - w;
                cell.bg = m > 0.f ? RGB {toByte((sumR - inkR) / m), toByte((sumG - inkG) / m),
                                         toByte((sumB - inkB) / m)}
                                  : RGB {0, 0, 0};
                cell.fg = w > 0.f ? RGB {toByte(inkR / w), toByte(inkG / w), toByte(inkB / w)}
                                  : cell.bg;
            } else {
                float fr, fg_, fb;
                if (w > 0.f) fr = inkR / w, fg_ = inkG / w, fb = inkB / w;
                else fr = sumR / cellPx, fg_ = sumG / cellPx, fb = sumB / cellPx;

                // Coverage already tracks brightness, so rendering it in a colour
                // that also tracks brightness darkens the cell twice over. A gamma
                // below 1 wins some back; unlike a coverage-derived multiplier it
                // is bounded and can never blow past white.
                if (opts.brightnessGamma != 1.f) {
                    const float e = opts.brightnessGamma;
                    fr = 255.f * std::pow(std::clamp(fr / 255.f, 0.f, 1.f), e);
                    fg_ = 255.f * std::pow(std::clamp(fg_ / 255.f, 0.f, 1.f), e);
                    fb = 255.f * std::pow(std::clamp(fb / 255.f, 0.f, 1.f), e);
                }

                cell.fg = {toByte(fr), toByte(fg_), toByte(fb)};
                cell.bg = opts.backgroundColor;
            }
        }
    }
}

}   // namespace Bitmask
