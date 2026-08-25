#include "FrameProcessor.hpp"
#include "core/Profiler.hpp"
#include "bitmap/Bitmask.hpp"
#include "bitmap/Ramp.hpp"
#include "bitmap/Structure.hpp"
#include "dithering/Dithering.hpp"
#include "edges/Edges.hpp"
#include "filters/CellFilters.hpp"
#include "filters/ImageFilters.hpp"
#include "filters/Palettes.hpp"
#include "output/AnsiRenderer.hpp"
#include "output/ImageRenderer.hpp"
#include <cstring>
#include <iostream>

using namespace App;

namespace FrameProcessor {

namespace {

ImageRenderer::Fit toFit(ImageFit f)
{
    switch (f) {
    case ImageFit::None: return ImageRenderer::Fit::None;
    case ImageFit::Width: return ImageRenderer::Fit::Width;
    case ImageFit::Height: return ImageRenderer::Fit::Height;
    case ImageFit::Cover: return ImageRenderer::Fit::Cover;
    case ImageFit::Stretch: return ImageRenderer::Fit::Stretch;
    case ImageFit::Contain: break;
    }
    return ImageRenderer::Fit::Contain;
}

ImageRenderer::Align toAlign(ImageAlign a)
{
    switch (a) {
    case ImageAlign::TopLeft: return ImageRenderer::Align::TopLeft;
    case ImageAlign::Top: return ImageRenderer::Align::Top;
    case ImageAlign::TopRight: return ImageRenderer::Align::TopRight;
    case ImageAlign::Left: return ImageRenderer::Align::Left;
    case ImageAlign::Right: return ImageRenderer::Align::Right;
    case ImageAlign::BottomLeft: return ImageRenderer::Align::BottomLeft;
    case ImageAlign::Bottom: return ImageRenderer::Align::Bottom;
    case ImageAlign::BottomRight: return ImageRenderer::Align::BottomRight;
    case ImageAlign::Center: break;
    }
    return ImageRenderer::Align::Center;
}

AnsiRenderer::ColorDepth toDepth(ColorMode c)
{
    switch (c) {
    case ColorMode::Ansi16: return AnsiRenderer::ColorDepth::Ansi16;
    case ColorMode::None: return AnsiRenderer::ColorDepth::None;
    case ColorMode::TrueColor: break;
    }
    return AnsiRenderer::ColorDepth::TrueColor;
}

// Extracted before anything downstream can touch `input`: the resample step always
// produces a 3-channel plane, so this is the only point where the source's own alpha
// channel (if it has one) still exists to read.
void extractAlphaSource(const Image& input, Image& alphaSource, bool wanted)
{
    if (!wanted) return;

    if (input.depth != 4 && input.depth != 2) {
        std::cerr << "asciigen: --edge-alpha requested but the source has no alpha "
                     "channel; skipped.\n";
        return;
    }

    if (alphaSource.width != input.width || alphaSource.height != input.height
        || alphaSource.depth != 1)
        alphaSource = Image(input.width, input.height, 1);

    const int d = input.depth;
    const byte* src = input.pixels + (d - 1);
    byte* dst = alphaSource.pixels;
    const size_t n = (size_t)input.width * (size_t)input.height;

    for (size_t i = 0; i < n; i++, src += d, dst++) *dst = *src;
}

void applySourceFilters(Image& img, const Options& opts)
{
    if (opts.source.autoLevels)
        ImageFilters::autoLevels(img, opts.source.autoLevelsLow, opts.source.autoLevelsHigh);
    if (opts.source.levels)
        ImageFilters::levels(
            img, opts.source.levelsBlack, opts.source.levelsWhite, opts.source.levelsGamma
        );
    if (opts.source.contrast != 1.f) ImageFilters::contrast(img, opts.source.contrast);
    if (opts.source.blurRadius > 0) ImageFilters::blur(img, opts.source.blurRadius);
    if (opts.source.sharpenAmount > 0.f)
        ImageFilters::unsharpMask(img, opts.source.sharpenAmount, opts.source.sharpenRadius);
}

}   // namespace

void run(FrameStorage& frame, const Options& opts, const Context& ctx)
{
    ASCIIGEN_PROFILE("FrameProcessor::run", "pipeline");

    extractAlphaSource(frame.input, frame.alphaSource, opts.edge.alphaOutline);

    // Whichever side has fewer pixels goes first. A source bigger than the plane (the
    // common case) gets resampled down before filtering -- most of it would be smoothed
    // away regardless. A source smaller than the plane gets filtered first instead:
    // resampling it up before filtering would mean filtering mostly-interpolated pixels
    // the resample just invented. See optimizations.md's "plane is upsampled" entry.
    {
        ASCIIGEN_PROFILE("resample + source filters", "resample");

        if ((size_t)frame.input.width * (size_t)frame.input.height
            < (size_t)ctx.planeW * (size_t)ctx.planeH) {
            applySourceFilters(frame.input, opts);
            Resample::toGrid(frame.input, frame.plane, ctx.planeW, ctx.planeH, ctx.resampleFilter);
        } else {
            Resample::toGrid(frame.input, frame.plane, ctx.planeW, ctx.planeH, ctx.resampleFilter);
            applySourceFilters(frame.plane, opts);
        }
    }

    // Dithered once here, into its own persistent buffer, instead of by whichever
    // algorithm runs below -- all three used to make their own internal copy of an
    // already-correctly-sized plane and dither THAT, duplicating the same copy+dither
    // step inside each one despite only ever running one of them per frame.
    // `frame.plane` stays undithered for Edges::apply below, which judges edges
    // against the same plane the algorithm scored shapes against, not what
    // dithering nudged it to -- matches what every algorithm already did on its own
    // internal copy before this moved out to here.
    {
        ASCIIGEN_PROFILE("dither", "dither");
        const size_t planeBytes =
            (size_t)frame.plane.width * (size_t)frame.plane.height * (size_t)frame.plane.depth;
        std::memcpy(frame.ditheredPlane.pixels, frame.plane.pixels, planeBytes);

        // Ramp reads one sample per cell, already the granularity dithering should
        // act at; Bitmask/Structure work at atlas resolution, one dither block per
        // cell across the whole atlas cell.
        const int blockW = opts.algo.name == AlgoName::Ramp ? 1 : ctx.matchAtlas->cellWidth();
        const int blockH = opts.algo.name == AlgoName::Ramp ? 1 : ctx.matchAtlas->cellHeight();
        Dithering::apply(frame.ditheredPlane, blockW, blockH);
    }

    switch (opts.algo.name) {
    case AlgoName::Ramp:
        Ramp::generate(frame.ditheredPlane, frame.buffer, *ctx.charset, opts.algo.rampChars);
        break;

    case AlgoName::Bitmask:
        Bitmask::generate(
            frame.ditheredPlane, frame.buffer, *ctx.matchAtlas, ctx.bitmaskModel,
            frame.bitmaskScratch,
            {.allowBackground = opts.algo.allowBackground,
             .brightnessGamma = opts.algo.brightnessGamma,
             .softness = opts.algo.bitmaskSoftness,
             .blurRadius = opts.algo.bitmaskBlurRadius}
        );
        break;

    case AlgoName::Structure:
        Structure::generate(
            frame.ditheredPlane, frame.buffer, *ctx.matchAtlas, ctx.structureModel,
            frame.structureScratch,
            {.shape = {.orientBlocksX = opts.algo.structureOrientBlocksX,
                       .orientBlocksY = opts.algo.structureOrientBlocksY,
                       .bins = opts.algo.structureBins,
                       .massBlocksX = opts.algo.structureMassBlocksX,
                       .massBlocksY = opts.algo.structureMassBlocksY},
             .orientationWeight = opts.algo.structureOrientationWeight,
             .massWeight = opts.algo.structureMassWeight,
             .toneWeight = opts.algo.structureToneWeight,
             .allowBackground = opts.algo.allowBackground,
             .brightnessGamma = opts.algo.brightnessGamma,
             .gradientStride = opts.algo.structureGradientStride,
             .fastAtan = opts.algo.structureFastAtan,
             .flatThreshold = opts.algo.structureFlatThreshold}
        );
        break;
    }

    // May append the directional glyphs, so anything rasterised from the charset has to
    // come after this.
    {
        ASCIIGEN_PROFILE("edges", "edges");
        Edges::apply(frame.plane, frame.buffer, *ctx.charset, frame.alphaSource, frame.edgesScratch);
    }

    {
        ASCIIGEN_PROFILE("cell filters", "filter");

        if (opts.grid.despeckle > 0.f)
            CellFilters::despeckle(frame.buffer, *ctx.matchAtlas, opts.grid.despeckle);

        if (opts.grid.brightness != 1.f || opts.grid.gamma != 1.f)
            CellFilters::brightness(frame.buffer, opts.grid.brightness, opts.grid.gamma);
        if (opts.grid.vibrance != 0.f) CellFilters::vibrance(frame.buffer, opts.grid.vibrance);

        if (opts.grid.palette == PaletteName::Gruvbox)
            CellFilters::paletteMap(frame.buffer, Palettes::gruvbox(), opts.grid.paletteStrength);
        else if (opts.grid.palette == PaletteName::Nord)
            CellFilters::paletteMap(frame.buffer, Palettes::nord(), opts.grid.paletteStrength);
    }

    // Last, so the colour filters cannot shift the chosen backdrop, and so cells blanked
    // by despeckle carry it too rather than punching black holes.
    RGB backdrop {0, 0, 0};
    if (opts.backdrop.mode == BackdropMode::Auto)
        backdrop = frame.buffer.suggestedBackground(opts.backdrop.darken, opts.backdrop.lumaThreshold);
    else if (opts.backdrop.mode == BackdropMode::Fixed) backdrop = opts.backdrop.color;

    if (opts.backdrop.mode != BackdropMode::None && opts.backdrop.mode != BackdropMode::Transparent
        && !opts.algo.allowBackground)
        frame.buffer.fillBackground(backdrop);

    frame.text = AnsiRenderer::render(
        frame.buffer, *ctx.charset,
        {.depth = toDepth(opts.output.color),
         .transparentBackground = opts.backdrop.mode == BackdropMode::Transparent}
    );

    if (ctx.renderAtlas) {
        ImageRenderer::renderToSize(
            frame.buffer, *ctx.renderAtlas,
            {.width = opts.output.imageWidth,
             .height = opts.output.imageHeight,
             .fit = toFit(opts.output.fit),
             .align = toAlign(opts.output.align),
             .margin = opts.output.imageMargin,
             .scale = opts.output.imageScale,
             .aspect = opts.output.imageAspect,
             .backgroundColor = backdrop},
            frame.renderedImage
        );
    }
}

}   // namespace FrameProcessor
