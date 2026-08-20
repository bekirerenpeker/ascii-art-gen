#include "core/Profiler.hpp"
#include "bitmap/Structure.hpp"
#include "bitmap/Resample.hpp"
#include "dithering/Dithering.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Structure {

static float luma(const RGB& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

// Per-glyph facts that never change while a frame is being built: how much ink
// the glyph carries, and what shape that ink makes.
struct GlyphModel
{
    std::vector<Descriptor> descriptors;
    std::vector<float> inkWeight;
    float maxCoverage = 1.f;
};

static void buildGlyphModel(const GlyphAtlas& atlas, DescriptorShape shape, GlyphModel& out)
{
    const int cellPx = atlas.glpyhSize();
    const int glyphCount = atlas.glyphCount();

    out.descriptors.assign(glyphCount, {});
    out.inkWeight.assign(glyphCount, 0.f);
    out.maxCoverage = 0.f;
    ASCIIGEN_PROFILE("buildGlyphModel", "select");


    std::vector<float> mask(cellPx);

    for (int g = 0; g < glyphCount; g++) {
        const uint8_t* src = atlas.getGlyphBegin(g);

        float sum = 0.f;
        for (int i = 0; i < cellPx; i++) {
            mask[i] = src[i] / 255.f;
            sum += mask[i];
        }

        out.inkWeight[g] = sum;
        out.maxCoverage = std::max(out.maxCoverage, sum / cellPx);

        buildDescriptor(
            mask.data(), atlas.cellWidth(), atlas.cellHeight(), shape, out.descriptors[g]
        );
    }

    // Glyphs only ink part of their cell and antialiasing dims them further, so
    // the densest one still averages well under full coverage. Brightness has to
    // be scaled into that real range or everything saturates onto one glyph.
    if (out.maxCoverage <= 0.f) out.maxCoverage = 1.f;
}

static int pickGlyph(
    const Descriptor& tileDesc, float lumaMean, const GlyphModel& model, int cellPx,
    const StructureOptions& opts
)
{
    // Both shape terms are scaled by how much the TILE actually has to say. A
    // flat cell centres to nothing, both terms collapse to zero, and the choice
    // falls through to tone alone -- which is what should decide a blank patch.
    const float orientW = opts.orientationWeight * tileDesc.orientationStrength;
    const float massW = opts.massWeight * tileDesc.massStrength;

    float bestScore = -1e30f;
    float bestInk = 0.f;
    int best = 0;

    for (size_t g = 0; g < model.descriptors.size(); g++) {
        const float ink = model.inkWeight[g];
        const float coverErr = (ink / cellPx) / model.maxCoverage - lumaMean / 255.f;

        const float score = orientW * similarity(tileDesc.orientation, model.descriptors[g].orientation)
                          + massW * similarity(tileDesc.mass, model.descriptors[g].mass)
                          - opts.toneWeight * coverErr * coverErr;

        // Ties are common on flat tiles, where neither shape term has anything to
        // say; break towards less ink so a featureless cell goes blank rather than
        // landing on whichever heavy glyph happened to come first.
        const bool better =
            score > bestScore + 1e-4f || (score > bestScore - 1e-4f && ink < bestInk);
        if (!better) continue;

        bestScore = score;
        bestInk = ink;
        best = (int)g;
    }

    return best;
}

void generate(
    const Image& image, CellBuffer& outBuffer, const GlyphAtlas& atlas, StructureOptions opts
)
{
    const int cellPx = atlas.glpyhSize();
    const int atlasW = atlas.cellWidth();
    const int atlasH = atlas.cellHeight();

    if (cellPx <= 0 || atlas.glyphCount() <= 0) return;
    if (outBuffer.width() <= 0 || outBuffer.height() <= 0) return;
    ASCIIGEN_PROFILE("Structure::generate", "select");


    GlyphModel model;
    buildGlyphModel(atlas, opts.shape, model);

    // Same plane every selector works from: one resample at atlas resolution,
    // then dithered at cell granularity, since that is the unit being quantised.
    Image plane;
    Resample::toGrid(image, plane, outBuffer.width() * atlasW, outBuffer.height() * atlasH);
    Dithering::apply(plane, atlasW, atlasH);

    std::vector<RGB> tile(cellPx);
    std::vector<float> tileLuma(cellPx);
    Descriptor tileDesc;

    // See Bitmask.cpp: the plane is always 3-channel and exactly grid-sized, so
    // the gather walks a row pointer rather than paying getAt's per-subpixel
    // bounds check and depth branch.
    const byte* const planePx = plane.pixels;
    const size_t planeStride = (size_t)plane.width * 3;

    for (int cy = 0; cy < outBuffer.height(); cy++) {
        for (int cx = 0; cx < outBuffer.width(); cx++) {
            float sumL = 0.f;
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

            buildDescriptor(tileLuma.data(), atlasW, atlasH, opts.shape, tileDesc);

            const int glyph = pickGlyph(tileDesc, sumL / cellPx, model, cellPx, opts);

            Cell& cell = outBuffer.getAt(cx, cy);
            cell.glyphIndex = (uint16_t)glyph;

            solveCellColor(
                tile.data(), atlas.getGlyphBegin(glyph), cellPx, model.inkWeight[glyph],
                {.allowBackground = opts.allowBackground, .brightnessGamma = opts.brightnessGamma},
                cell.fg, cell.bg
            );
        }
    }
}

}   // namespace Structure
