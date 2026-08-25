#include "core/Profiler.hpp"
#include "bitmap/Structure.hpp"
#include "bitmap/DescriptorBasis.hpp"
#include "bitmap/Resample.hpp"
#include "dithering/Dithering.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace Structure {

static float luma(const RGB& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

static float dot(const float* a, const float* b, int n)
{
    float s = 0.f;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

// How many directions of a glyph's shape-term to keep for the S2 shortlist
// bound (see pickGlyph). Small on purpose: projecting onto them still has to
// beat evaluating the real 32- and 128-wide dot products for it to be worth
// doing at all.
static constexpr int kOriBasisSize = 4;
static constexpr int kMassBasisSize = 8;

// el-1: a second, cheaper bound checked before the one above. Reuses the same
// basis and the same projected coefficients -- see DescriptorBasis.hpp's note
// on residualNorm's kUse -- so this costs one more dot product of length 2
// per glyph, not a second projection. Still a true Cauchy-Schwarz bound (any
// subspace works, a smaller one is just looser), so this is exact, not a
// quality trade: it can only reject a glyph the full bound would also have
// rejected, never one it would have kept.
static constexpr int kOriFastSize = 2;
static constexpr int kMassFastSize = 2;

// Flat-tile path: how much a glyph's own off-centeredness pulls against it
// once shape can no longer discriminate -- see pickGlyphFlat. Small on
// purpose, a nudge among near-ties rather than a term that could outweigh a
// real tone difference.
static constexpr float kOffCenterWeight = 0.5f;

// Per-glyph facts that never change while a frame is being built: how much ink
// the glyph carries, what shape that ink makes, and (S1) that shape packed
// glyph-contiguous rather than as N separate heap vectors, plus (S2) a cheap
// subspace to bound a glyph's score against before paying for its real one.
struct GlyphModel
{
    std::vector<float> inkWeight;
    float maxCoverage = 1.f;

    int oriLen = 0;
    int massLen = 0;
    std::vector<float> oriMatrix;    // glyphCount x oriLen, row-major
    std::vector<float> massMatrix;   // glyphCount x massLen, row-major

    DescriptorBasis oriBasis;
    DescriptorBasis massBasis;
    std::vector<float> oriCoeffs;      // glyphCount x oriBasis.k
    std::vector<float> massCoeffs;     // glyphCount x massBasis.k
    std::vector<float> oriResidNorm;   // glyphCount, full kOriBasisSize bound
    std::vector<float> massResidNorm;  // glyphCount, full kMassBasisSize bound

    // el-1: residual for the cheap cascade tier, using only the first
    // kOriFastSize/kMassFastSize of the same coefficients above.
    std::vector<float> oriResidNormFast;   // glyphCount
    std::vector<float> massResidNormFast;  // glyphCount

    // Flat-tile path: how far each glyph's ink centroid sits from the cell's
    // own geometric centre, normalised by half the cell diagonal (so it's
    // roughly 0..1 regardless of cell size) -- and the same glyph indices
    // sorted by inkWeight, for the binary search in pickGlyphFlat.
    std::vector<float> offCenter;   // glyphCount
    std::vector<int> inkOrder;      // glyphCount, indices into the arrays above
};

static void buildGlyphModel(const GlyphAtlas& atlas, DescriptorShape shape, GlyphModel& out)
{
    const int cellPx = atlas.glpyhSize();
    const int glyphCount = atlas.glyphCount();

    out.inkWeight.assign(glyphCount, 0.f);
    out.offCenter.assign(glyphCount, 0.f);
    out.maxCoverage = 0.f;
    out.oriLen = 0;
    out.massLen = 0;
    ASCIIGEN_PROFILE("buildGlyphModel", "select");


    const int cellW = atlas.cellWidth();
    const int cellH = atlas.cellHeight();
    const float centerX = (cellW - 1) * 0.5f;
    const float centerY = (cellH - 1) * 0.5f;
    const float halfDiag = std::sqrt(centerX * centerX + centerY * centerY);

    std::vector<float> mask(cellPx);
    Descriptor desc;
    std::vector<int> massCounts;   // T2: reused across every glyph below

    // S1: descriptors go straight into a matrix, glyph g's row next to glyph
    // g+1's, instead of into N separate heap-allocated Descriptors scattered
    // across memory -- that scatter, not the arithmetic, was most of the cost
    // of scoring against them.
    for (int g = 0; g < glyphCount; g++) {
        const uint8_t* src = atlas.getGlyphBegin(g);

        float sum = 0.f, cx = 0.f, cy = 0.f;
        for (int i = 0; i < cellPx; i++) {
            mask[i] = src[i] / 255.f;
            sum += mask[i];

            // Flat-tile path's off-centre term, accumulated in the same pass
            // as inkWeight rather than a second one over the glyph.
            cx += mask[i] * (float)(i % cellW);
            cy += mask[i] * (float)(i / cellW);
        }

        out.inkWeight[g] = sum;
        out.maxCoverage = std::max(out.maxCoverage, sum / cellPx);

        // A blank glyph (space) has no ink to centre -- treat it as perfectly
        // centred rather than dividing by zero; it competes on ink alone.
        out.offCenter[g] = (sum > 1e-9f && halfDiag > 1e-9f)
            ? std::sqrt((cx / sum - centerX) * (cx / sum - centerX)
                        + (cy / sum - centerY) * (cy / sum - centerY))
                  / halfDiag
            : 0.f;

        // Always full precision here -- this builds the reference glyph model
        // itself, only ~95 to 256 calls total, not the per-cell hot path
        // el-3's stride targets.
        buildDescriptor(
            mask.data(), atlas.cellWidth(), atlas.cellHeight(), shape, desc, massCounts, 1, false
        );

        if (g == 0) {
            out.oriLen = (int)desc.orientation.size();
            out.massLen = (int)desc.mass.size();
            out.oriMatrix.assign((size_t)glyphCount * out.oriLen, 0.f);
            out.massMatrix.assign((size_t)glyphCount * out.massLen, 0.f);
        }

        std::copy(
            desc.orientation.begin(), desc.orientation.end(),
            out.oriMatrix.begin() + (size_t)g * out.oriLen
        );
        std::copy(
            desc.mass.begin(), desc.mass.end(), out.massMatrix.begin() + (size_t)g * out.massLen
        );
    }

    // Glyphs only ink part of their cell and antialiasing dims them further, so
    // the densest one still averages well under full coverage. Brightness has to
    // be scaled into that real range or everything saturates onto one glyph.
    if (out.maxCoverage <= 0.f) out.maxCoverage = 1.f;

    // Flat-tile path: glyph indices sorted by ink weight, so pickGlyphFlat can
    // binary-search for the closest tone match instead of scanning all of them.
    out.inkOrder.resize(glyphCount);
    std::iota(out.inkOrder.begin(), out.inkOrder.end(), 0);
    std::sort(out.inkOrder.begin(), out.inkOrder.end(), [&](int a, int b) {
        return out.inkWeight[a] < out.inkWeight[b];
    });

    // S2: a subspace of each shape term's own vector space, built once here so
    // pickGlyph can bound a glyph's contribution before computing it exactly.
    out.oriBasis = buildBasis(out.oriMatrix.data(), glyphCount, out.oriLen, kOriBasisSize);
    out.massBasis = buildBasis(out.massMatrix.data(), glyphCount, out.massLen, kMassBasisSize);

    out.oriCoeffs.assign((size_t)glyphCount * out.oriBasis.k, 0.f);
    out.massCoeffs.assign((size_t)glyphCount * out.massBasis.k, 0.f);
    out.oriResidNorm.assign(glyphCount, 1.f);
    out.massResidNorm.assign(glyphCount, 1.f);
    out.oriResidNormFast.assign(glyphCount, 1.f);
    out.massResidNormFast.assign(glyphCount, 1.f);

    for (int g = 0; g < glyphCount; g++) {
        if (out.oriBasis.k > 0) {
            const float* row = out.oriMatrix.data() + (size_t)g * out.oriLen;
            float* c = out.oriCoeffs.data() + (size_t)g * out.oriBasis.k;
            project(out.oriBasis, row, c);
            out.oriResidNorm[g] = residualNorm(out.oriBasis, row, c, out.oriBasis.k);
            out.oriResidNormFast[g] = residualNorm(out.oriBasis, row, c, kOriFastSize);
        }
        if (out.massBasis.k > 0) {
            const float* row = out.massMatrix.data() + (size_t)g * out.massLen;
            float* c = out.massCoeffs.data() + (size_t)g * out.massBasis.k;
            project(out.massBasis, row, c);
            out.massResidNorm[g] = residualNorm(out.massBasis, row, c, out.massBasis.k);
            out.massResidNormFast[g] = residualNorm(out.massBasis, row, c, kMassFastSize);
        }
    }
}

// Flat-tile path: |orientW| and |massW| are both small enough that the shape
// terms cannot swing the winner by more than a bounded amount, so the full
// 95-glyph scan can be replaced with a binary search by ink weight plus a
// small window around it, and an off-centre tie-break the full scan does not
// have (mass already carries far richer positional information on a
// textured tile, which is why this term only exists here).
//
// The window is derived, not guessed. Every dot product below is between two
// vectors of norm <= 1 (centreAndNormalize's own guarantee), so by
// Cauchy-Schwarz the shape contribution to any glyph's score is bounded by
// |orientW| + |massW|. Call that (plus the usual 1e-4 tie slack) `budget`.
// The nearest-ink glyph is always in the window and achieves some exact
// score; any glyph whose tone term alone is more than 2*budget below the
// nearest's tone term cannot reach `bestScore - 1e-4` even in the best case
// for its shape term and the worst case for the nearest's -- see
// optimizations.md for the full derivation. Excluding it is therefore exact
// given the bound, not an approximation on top of an approximation.
//
// First measurement of this (optimizations.md item 9): the window ended up
// about as wide as the candidate count S1/S2's own bound already narrows a
// full scan down to, so replacing bound-checking with ink-window narrowing
// was a wash -- comparable work through a different door, not less of it.
// This version composes the two instead of picking one: narrow by ink
// distance first, then run the SAME cascade bound (el-1's cheap tier, then
// S2's) over only what's left, and pay for the real 160-wide dot product
// only on what survives both.
static int pickGlyphFlat(
    const Descriptor& tileDesc, float lumaMean, const GlyphModel& model, int cellPx,
    const StructureOptions& opts, float orientW, float massW, std::vector<float>& tileOriCoeffs,
    std::vector<float>& tileMassCoeffs, size_t& exactEvals
)
{
    const int glyphCount = (int)model.inkOrder.size();
    if (glyphCount == 0) return 0;

    const float targetInk = (lumaMean / 255.f) * model.maxCoverage * (float)cellPx;
    auto inkAt = [&](int idx) { return model.inkWeight[model.inkOrder[idx]]; };

    // Insertion point for the target, then whichever neighbour is actually
    // closer -- the insertion point alone doesn't tell you which side wins.
    int lo = 0, hi = glyphCount;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (inkAt(mid) < targetInk) lo = mid + 1; else hi = mid;
    }
    int nearestIdx = std::min(lo, glyphCount - 1);
    if (lo > 0 && std::fabs(inkAt(lo - 1) - targetInk) < std::fabs(inkAt(nearestIdx) - targetInk))
        nearestIdx = lo - 1;

    int winLo = 0, winHi = glyphCount;
    if (opts.toneWeight > 1e-9f) {
        const float budget = std::fabs(orientW) + std::fabs(massW) + 1e-4f;

        const int gNearest = model.inkOrder[nearestIdx];
        const float inkNearest = model.inkWeight[gNearest];
        const float coverErrNearest = (inkNearest / cellPx) / model.maxCoverage - lumaMean / 255.f;
        const float toneTermNearest = -opts.toneWeight * coverErrNearest * coverErrNearest;

        // coverErr^2 <= (2*budget - toneTermNearest) / toneWeight, i.e. every
        // glyph whose tone term could still land within 2*budget of the best
        // achievable one (the nearest's).
        const float maxCoverErrSq = (2.f * budget - toneTermNearest) / opts.toneWeight;
        const float halfWindow =
            maxCoverErrSq > 0.f ? (float)cellPx * model.maxCoverage * std::sqrt(maxCoverErrSq) : 0.f;

        int l = 0, h = glyphCount;
        while (l < h) {
            const int mid = (l + h) / 2;
            if (inkAt(mid) < targetInk - halfWindow) l = mid + 1; else h = mid;
        }
        winLo = l;

        l = winLo, h = glyphCount;
        while (l < h) {
            const int mid = (l + h) / 2;
            if (inkAt(mid) <= targetInk + halfWindow) l = mid + 1; else h = mid;
        }
        winHi = l;
    }

    // The nearest candidate is always safe to include -- it achieves the best
    // possible tone term by construction -- so the window covers it even if
    // float rounding somehow put it just outside the computed bounds.
    winLo = std::clamp(std::min(winLo, nearestIdx), 0, glyphCount);
    winHi = std::clamp(std::max(winHi, nearestIdx + 1), 0, glyphCount);

    // Same cascade bound as the full scan, over only the window above --
    // ink-distance narrowing and shape bound-checking compose instead of one
    // substituting for the other. Same non-negative-weight requirement as
    // pickGlyph's own canBound: see that comment for why.
    const bool canBound =
        orientW >= 0.f && massW >= 0.f && model.oriBasis.k > 0 && model.massBasis.k > 0;
    if (canBound) {
        project(model.oriBasis, tileDesc.orientation.data(), tileOriCoeffs.data());
        project(model.massBasis, tileDesc.mass.data(), tileMassCoeffs.data());
    }

    float bestScore = -1e30f;
    float bestInk = 0.f;
    int best = model.inkOrder[nearestIdx];

    for (int idx = winLo; idx < winHi; idx++) {
        const int g = model.inkOrder[idx];
        const float ink = model.inkWeight[g];
        const float coverErr = (ink / cellPx) / model.maxCoverage - lumaMean / 255.f;
        // Off-centre is folded in here, alongside tone -- both are exact,
        // neither needs bounding, so both stay valid additions to the bound
        // check below exactly the way toneTerm alone already was.
        const float toneTerm =
            -opts.toneWeight * coverErr * coverErr - kOffCenterWeight * model.offCenter[g];

        if (canBound) {
            const float ubOriFast =
                dot(tileOriCoeffs.data(), model.oriCoeffs.data() + (size_t)g * model.oriBasis.k,
                    kOriFastSize)
                + model.oriResidNormFast[g];
            const float ubMassFast =
                dot(tileMassCoeffs.data(), model.massCoeffs.data() + (size_t)g * model.massBasis.k,
                    kMassFastSize)
                + model.massResidNormFast[g];
            if (orientW * ubOriFast + massW * ubMassFast + toneTerm < bestScore - 1e-4f) continue;

            const float ubOri =
                dot(tileOriCoeffs.data(), model.oriCoeffs.data() + (size_t)g * model.oriBasis.k,
                    model.oriBasis.k)
                + model.oriResidNorm[g];
            const float ubMass =
                dot(tileMassCoeffs.data(), model.massCoeffs.data() + (size_t)g * model.massBasis.k,
                    model.massBasis.k)
                + model.massResidNorm[g];
            if (orientW * ubOri + massW * ubMass + toneTerm < bestScore - 1e-4f) continue;
        }

        exactEvals++;

        const float score =
            orientW * dot(tileDesc.orientation.data(), model.oriMatrix.data() + (size_t)g * model.oriLen,
                           model.oriLen)
            + massW * dot(tileDesc.mass.data(), model.massMatrix.data() + (size_t)g * model.massLen,
                          model.massLen)
            + toneTerm;

        const bool better =
            score > bestScore + 1e-4f || (score > bestScore - 1e-4f && ink < bestInk);
        if (!better) continue;

        bestScore = score;
        bestInk = ink;
        best = g;
    }

    return best;
}

static int pickGlyph(
    const Descriptor& tileDesc, float lumaMean, const GlyphModel& model, int cellPx,
    const StructureOptions& opts, std::vector<float>& tileOriCoeffs, std::vector<float>& tileMassCoeffs,
    size_t& exactEvals, size_t& flatHits
)
{
    // Both shape terms are scaled by how much the TILE actually has to say. A
    // flat cell centres to nothing, both terms collapse to zero, and the choice
    // falls through to tone alone -- which is what should decide a blank patch.
    const float orientW = opts.orientationWeight * tileDesc.orientationStrength;
    const float massW = opts.massWeight * tileDesc.massStrength;
    const int glyphCount = (int)model.inkWeight.size();

    if (std::fabs(orientW) <= opts.flatThreshold && std::fabs(massW) <= opts.flatThreshold) {
        flatHits++;
        return pickGlyphFlat(
            tileDesc, lumaMean, model, cellPx, opts, orientW, massW, tileOriCoeffs, tileMassCoeffs,
            exactEvals
        );
    }

    // S2's Cauchy-Schwarz bound only stays an upper bound once it is scaled by
    // a non-negative weight -- scaling by a negative one flips it into a lower
    // bound. Both weights here are ordinarily >= 0 (only the tile-dependent
    // *Strength factor above can zero them); a caller-supplied negative
    // --algo-structure-*-weight falls back to the exact scan below, which is
    // always correct.
    const bool canBound =
        orientW >= 0.f && massW >= 0.f && model.oriBasis.k > 0 && model.massBasis.k > 0;

    if (canBound) {
        project(model.oriBasis, tileDesc.orientation.data(), tileOriCoeffs.data());
        project(model.massBasis, tileDesc.mass.data(), tileMassCoeffs.data());
    }

    float bestScore = -1e30f;
    float bestInk = 0.f;
    int best = 0;

    for (int g = 0; g < glyphCount; g++) {
        const float ink = model.inkWeight[g];
        const float coverErr = (ink / cellPx) / model.maxCoverage - lumaMean / 255.f;
        const float toneTerm = -opts.toneWeight * coverErr * coverErr;

        if (canBound) {
            // el-1: the cheap tier first -- kOriFastSize/kMassFastSize
            // components instead of the full kOriBasisSize/kMassBasisSize, off
            // the same coefficients already projected above. A strictly looser
            // bound than the full one below, so anything it rejects the full
            // one would have too; the reverse is not true, which is exactly
            // why it is checked first rather than instead.
            const float ubOriFast =
                dot(tileOriCoeffs.data(), model.oriCoeffs.data() + (size_t)g * model.oriBasis.k,
                    kOriFastSize)
                + model.oriResidNormFast[g];
            const float ubMassFast =
                dot(tileMassCoeffs.data(), model.massCoeffs.data() + (size_t)g * model.massBasis.k,
                    kMassFastSize)
                + model.massResidNormFast[g];
            if (orientW * ubOriFast + massW * ubMassFast + toneTerm < bestScore - 1e-4f) continue;

            const float ubOri =
                dot(tileOriCoeffs.data(), model.oriCoeffs.data() + (size_t)g * model.oriBasis.k,
                    model.oriBasis.k)
                + model.oriResidNorm[g];
            const float ubMass =
                dot(tileMassCoeffs.data(), model.massCoeffs.data() + (size_t)g * model.massBasis.k,
                    model.massBasis.k)
                + model.massResidNorm[g];

            // A true upper bound on the exact score below: if even this cannot
            // reach the tie threshold, the exact score cannot either, and the
            // running best/tie state ends up exactly where the exact scan would
            // have left it -- see optimizations.md's S2 entry.
            if (orientW * ubOri + massW * ubMass + toneTerm < bestScore - 1e-4f) continue;
        }

        exactEvals++;

        const float score =
            orientW * dot(tileDesc.orientation.data(), model.oriMatrix.data() + (size_t)g * model.oriLen,
                           model.oriLen)
            + massW * dot(tileDesc.mass.data(), model.massMatrix.data() + (size_t)g * model.massLen,
                          model.massLen)
            + toneTerm;

        // Ties are common on flat tiles, where neither shape term has anything to
        // say; break towards less ink so a featureless cell goes blank rather than
        // landing on whichever heavy glyph happened to come first.
        const bool better =
            score > bestScore + 1e-4f || (score > bestScore - 1e-4f && ink < bestInk);
        if (!better) continue;

        bestScore = score;
        bestInk = ink;
        best = g;
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

    // Scratch for S2's projection, sized once from the model built above and
    // reused every cell -- pickGlyph only ever overwrites it.
    std::vector<float> tileOriCoeffs(model.oriBasis.k);
    std::vector<float> tileMassCoeffs(model.massBasis.k);
    std::vector<int> massCounts;   // T2: reused every cell, same as buildGlyphModel above

    // How much of S2's shortlist is actually pruning, and how many cells took
    // the flat-tile path, for the trace -- see generate()'s Profiler::describe
    // calls below.
    size_t exactEvals = 0;
    size_t flatHits = 0;
    const size_t totalCells = (size_t)outBuffer.width() * outBuffer.height();
    const size_t totalGlyphChecks = totalCells * model.inkWeight.size();

    // See Bitmask.cpp: the plane is always 3-channel and exactly grid-sized, so
    // the gather walks a row pointer rather than paying getAt's per-subpixel
    // bounds check and depth branch.
    const byte* const planePx = plane.pixels;
    const size_t planeStride = (size_t)plane.width * 3;

    // Call/cell-granularity scopes, not per-pixel, for the same reason as
    // buildDescriptor's own in Descriptor.cpp -- 45,700 events a frame is the
    // safe ceiling for the mutex-locked trace write; splitting further than
    // this would make the write itself the thing being measured.
    for (int cy = 0; cy < outBuffer.height(); cy++) {
        for (int cx = 0; cx < outBuffer.width(); cx++) {
            float sumL = 0.f;
            {
                ASCIIGEN_PROFILE("tile gather", "select");
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
            }

            buildDescriptor(
                tileLuma.data(), atlasW, atlasH, opts.shape, tileDesc, massCounts,
                opts.gradientStride, opts.fastAtan
            );

            int glyph = 0;
            {
                ASCIIGEN_PROFILE("pickGlyph", "select");
                glyph = pickGlyph(
                    tileDesc, sumL / cellPx, model, cellPx, opts, tileOriCoeffs, tileMassCoeffs,
                    exactEvals, flatHits
                );
            }

            Cell& cell = outBuffer.getAt(cx, cy);
            cell.glyphIndex = (uint16_t)glyph;

            {
                ASCIIGEN_PROFILE("solveCellColor", "select");
                solveCellColor(
                    tile.data(), atlas.getGlyphBegin(glyph), cellPx, model.inkWeight[glyph],
                    {.allowBackground = opts.allowBackground, .brightnessGamma = opts.brightnessGamma},
                    cell.fg, cell.bg
                );
            }
        }
    }

    // How many of S2's bound checks it actually pruned -- shows up in the
    // trace's otherData, next to which command produced it. Guarded the same
    // way ASCIIGEN_PROFILE's own macro is, so a run with no --profile pays
    // nothing for this beyond the one enabled() check.
    if (totalGlyphChecks > 0 && Profiler::enabled()) {
        Profiler::describe(
            "Structure pickGlyph exact evals",
            std::to_string(exactEvals) + " / " + std::to_string(totalGlyphChecks) + " ("
                + std::to_string((100.0 * (double)exactEvals) / (double)totalGlyphChecks) + "%)"
        );
    }
    if (totalCells > 0 && Profiler::enabled()) {
        Profiler::describe(
            "Structure flat-tile hits",
            std::to_string(flatHits) + " / " + std::to_string(totalCells) + " ("
                + std::to_string((100.0 * (double)flatHits) / (double)totalCells) + "%)"
        );
    }
}

}   // namespace Structure
