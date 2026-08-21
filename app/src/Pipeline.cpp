#include "Pipeline.hpp"
#include "core/Profiler.hpp"
#include "Assets.hpp"
#include "bitmap/Resample.hpp"
#include "bitmap/Bitmask.hpp"
#include "bitmap/Ramp.hpp"
#include "bitmap/Structure.hpp"
#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include "dithering/Dithering.hpp"
#include "edges/Edges.hpp"
#include "file_management/ImageManager.hpp"
#include "file_management/OutputManager.hpp"
#include "filters/CellFilters.hpp"
#include "filters/ImageFilters.hpp"
#include "filters/Palettes.hpp"
#include "font/Font.hpp"
#include "font/GlyphAtlas.hpp"
#include "output/AnsiRenderer.hpp"
#include "output/ImageRenderer.hpp"
#include "output/Terminal.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>

using namespace App;

namespace Pipeline {

namespace {

std::string lowerExtension(const std::filesystem::path& p)
{
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return ext;
}

int passthrough(const Options& opts)
{
    std::ifstream file(opts.input.path, std::ios::binary);
    if (!file) {
        std::cerr << "asciigen: cannot read \"" << opts.input.path << "\"\n";
        return 3;
    }

    // The whole point of this path: `cat` and `type` do not put the console into
    // VT mode, so a .ans file prints as literal escape sequences without this.
    Terminal::enableAnsi();
    Terminal::enableUtf8();

    std::cout << file.rdbuf();
    return 0;
}

// A system font is the default rather than a bundled one, so the project ships
// no third-party font and no third-party licence. A copy left in assets/fonts is
// still honoured, which keeps a checkout that has one working unchanged.
std::filesystem::path resolveFont(const Options& opts)
{
    if (!opts.font.path.empty()) return opts.font.path;

    const std::filesystem::path system = Assets::defaultFont();
    if (!system.empty()) return system;

    return Assets::font("SpaceMono-Regular.ttf");
}

Charset buildCharset(const Options& opts)
{
    switch (opts.charset.name) {
    case CharsetName::Blocks: return Charset::blocks();
    case CharsetName::Braille: return Charset::braille();
    case CharsetName::Ramp: return Charset(opts.algo.rampChars);
    case CharsetName::Custom: break;
    case CharsetName::Ascii: break;
    }

    std::u32string glyphs;
    if (opts.charset.name == CharsetName::Custom) glyphs.assign(opts.charset.chars.begin(), opts.charset.chars.end());
    else for (char32_t cp = 0x20; cp < 0x7F; cp++) glyphs += cp;

    for (const auto& range : opts.charset.ranges)
        for (char32_t cp = range.first; cp <= range.second; cp++)
            if (glyphs.find(cp) == std::u32string::npos) glyphs += cp;

    return Charset(std::move(glyphs));
}

// Cells are twice as tall as they are wide, so a grid that matches the source's
// shape needs half as many rows as the raw aspect suggests.
void resolveGridSize(const Options& opts, const Image& img, int& cols, int& rows)
{
    cols = opts.grid.width;
    rows = opts.grid.height;

    if (cols > 0 && rows > 0) return;

    if (cols <= 0 && rows <= 0) {
        int termCols = 0, termRows = 0;
        if (Terminal::isTty() && Terminal::getSize(termCols, termRows) && termCols > 0)
            cols = termCols;
        else cols = 160;
    }

    if (cols > 0 && rows <= 0) rows = std::max(1, (int)std::lround((double)cols * img.height / (img.width * 2.0)));
    else if (rows > 0 && cols <= 0) cols = std::max(1, (int)std::lround((double)rows * img.width * 2.0 / img.height));
}

// A path that names an existing directory means "put it in here, called after
// the input". Saves renaming the output every time you convert a new picture,
// and png is the default because that is what a directory cannot tell us.
std::filesystem::path resolveOutputPath(const Options& opts, const std::string& given)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(given, ec)) return given;

    const std::string stem = std::filesystem::path(opts.input.path).stem().string();
    return std::filesystem::path(given) / (stem + ".png");
}

bool wantsImage(const Options& opts)
{
    for (const std::string& p : opts.output.paths) {
        const std::string ext = lowerExtension(resolveOutputPath(opts, p));
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return true;
    }
    return false;
}

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

// How many genuinely different shapes the atlas holds. Counting inked glyphs is
// not enough: a font missing a codepoint hands back .notdef, which in most faces
// is a box outline and therefore very much inked. Every glyph then rasterises to
// the SAME box, they all score identically, selection collapses onto whichever
// comes first, and the picture comes out uniform -- with nothing in the output
// to explain why. Distinctness is what actually matters.
int distinctGlyphCount(const GlyphAtlas& atlas)
{
    const int cellPx = atlas.glpyhSize();
    std::set<uint64_t> shapes;

    for (int g = 0; g < atlas.glyphCount(); g++) {
        const uint8_t* mask = atlas.getGlyphBegin(g);

        // FNV-1a over the mask. Collisions would only ever under-report, and a
        // false "looks fine" is the harmless direction here.
        uint64_t h = 1469598103934665603ull;
        for (int i = 0; i < cellPx; i++) {
            h ^= mask[i];
            h *= 1099511628211ull;
        }

        shapes.insert(h);
    }

    return (int)shapes.size();
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

}   // namespace

int run(const Options& opts)
{
    if (opts.input.passthrough) return passthrough(opts);

    ASCIIGEN_PROFILE("run", "pipeline");

    Image img;
    {
        ASCIIGEN_PROFILE("load", "io");
        img = ImageManager::loadImage(opts.input.path);
    }
    if (!img.pixels) return 3;

    // Extracted before anything downstream can touch img: the resample for
    // filters below always produces a 3-channel plane, so this is the only
    // point where the source's own alpha channel still exists.
    Image alphaSource;
    if (opts.edge.alphaOutline) {
        if (img.depth == 4 || img.depth == 2) {
            alphaSource = Image(img.width, img.height, 1);

            const int d = img.depth;
            const byte* src = img.pixels + (d - 1);
            byte* dst = alphaSource.pixels;
            const size_t n = (size_t)img.width * (size_t)img.height;

            for (size_t i = 0; i < n; i++, src += d, dst++) *dst = *src;
        }
        else {
            std::cerr << "asciigen: --edge-alpha requested but the source has no alpha "
                         "channel; skipped.\n";
        }
    }

    int cols = 0, rows = 0;
    resolveGridSize(opts, img, cols, rows);

    Charset charset;
    {
        ASCIIGEN_PROFILE("buildCharset", "font");
        charset = buildCharset(opts);
    }

    CellBuffer buffer;
    buffer.setSize(cols, rows);

    Dithering::options = {
        .enabled = opts.dither.name != DitherName::None,
        .algorithm = Dithering::Algorithm::Bayer4,
        .levels = opts.dither.levels,
        .adaptive = opts.dither.adaptive,
        .flatContrast = opts.dither.flatContrast,
        .edgeContrast = opts.dither.edgeContrast
    };

    Edges::options = {
        .enabled = opts.edge.name != EdgeName::None,
        .algorithm = Edges::Algorithm::Scharr,
        .subsamples = opts.edge.subsamples,
        .threshold = opts.edge.threshold,
        .coherence = opts.edge.coherence,
        .hysteresis = opts.edge.hysteresis,
        .nms = opts.edge.nms
    };

    Edges::alphaOptions = {
        .enabled = opts.edge.alphaOutline,
        .threshold = opts.edge.alphaThreshold,
        .coherence = opts.edge.alphaCoherence
    };

    const std::filesystem::path fontPath = resolveFont(opts);
    if (!std::filesystem::exists(fontPath)) {
        std::cerr << "asciigen: font not found: " << fontPath.string() << "\n";
        return 7;
    }

    std::unique_ptr<Font> fontHolder;
    {
        ASCIIGEN_PROFILE("Font::load", "font");
        fontHolder = std::make_unique<Font>(fontPath);
    }
    Font& font = *fontHolder;


    // Width is half the height, always, so the cell grid can never be broken by
    // the choice of face.
    const int matchH = std::max(2, opts.font.matchSize);
    GlyphAtlas matchAtlas(font, charset, std::max(1, matchH / 2), matchH);

    // Fewer than two distinct shapes means there is nothing to choose between,
    // and the result is a uniform picture. Worth saying out loud -- the usual
    // cause is a charset the font simply does not cover.
    int distinct = 0;
    {
        ASCIIGEN_PROFILE("distinctGlyphCount", "font");
        distinct = distinctGlyphCount(matchAtlas);
    }

    if (distinct < 2) {
        std::cerr << "asciigen: \"" << fontPath.filename().string()
                  << "\" has no glyphs for this charset, so the output will be blank.\n"
                  << "  pick a font that covers it, e.g.\n"
                  << "  --font-path C:/Windows/Fonts/CascadiaMono.ttf\n";
    }

    // Resampled BEFORE the filters, not after. Only cell-resolution detail
    // survives into the selector, so filtering the full-size source spends most
    // of its work on pixels about to be averaged away -- and sharpening in
    // particular is largely undone by that averaging. Doing it here is both far
    // cheaper and more effective, because the detail it enhances is the detail
    // the selector actually sees.
    //
    // Ramp reads one sample per cell; everything else works at atlas resolution.
    const int planeW = opts.algo.name == AlgoName::Ramp ? cols : cols * std::max(1, matchH / 2);
    const int planeH = opts.algo.name == AlgoName::Ramp ? rows : rows * matchH;

    {
        ASCIIGEN_PROFILE("resample for filters", "resample");

        Image plane;
        Resample::toGrid(img, plane, planeW, planeH);
        img = std::move(plane);
    }

    {
        ASCIIGEN_PROFILE("source filters", "filter");

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


    switch (opts.algo.name) {
    case AlgoName::Ramp:
        Ramp::generate(img, buffer, charset, opts.algo.rampChars);
        break;

    case AlgoName::Bitmask:
        Bitmask::generate(
            img, buffer, matchAtlas,
            {.allowBackground = opts.algo.allowBackground,
             .brightnessGamma = opts.algo.brightnessGamma,
             .softness = opts.algo.bitmaskSoftness,
             .blurRadius = opts.algo.bitmaskBlurRadius}
        );
        break;

    case AlgoName::Structure:
        Structure::generate(
            img, buffer, matchAtlas,
            {.shape = {.orientBlocksX = opts.algo.structureOrientBlocksX,
                       .orientBlocksY = opts.algo.structureOrientBlocksY,
                       .bins = opts.algo.structureBins,
                       .massBlocksX = opts.algo.structureMassBlocksX,
                       .massBlocksY = opts.algo.structureMassBlocksY},
             .orientationWeight = opts.algo.structureOrientationWeight,
             .massWeight = opts.algo.structureMassWeight,
             .toneWeight = opts.algo.structureToneWeight,
             .allowBackground = opts.algo.allowBackground,
             .brightnessGamma = opts.algo.brightnessGamma}
        );
        break;
    }

    // May append the directional glyphs, so anything rasterised from the charset
    // has to come after this.
    {
        ASCIIGEN_PROFILE("edges", "edges");
        Edges::apply(img, buffer, charset, alphaSource);
    }

    {
        ASCIIGEN_PROFILE("cell filters", "filter");

        if (opts.grid.despeckle > 0.f)
            CellFilters::despeckle(buffer, matchAtlas, opts.grid.despeckle);

        if (opts.grid.brightness != 1.f || opts.grid.gamma != 1.f)
            CellFilters::brightness(buffer, opts.grid.brightness, opts.grid.gamma);
        if (opts.grid.vibrance != 0.f) CellFilters::vibrance(buffer, opts.grid.vibrance);

        if (opts.grid.palette == PaletteName::Gruvbox)
            CellFilters::paletteMap(buffer, Palettes::gruvbox(), opts.grid.paletteStrength);
        else if (opts.grid.palette == PaletteName::Nord)
            CellFilters::paletteMap(buffer, Palettes::nord(), opts.grid.paletteStrength);
    }

    // Last, so the colour filters cannot shift the chosen backdrop, and so cells
    // blanked by despeckle carry it too rather than punching black holes.
    RGB backdrop {0, 0, 0};
    if (opts.backdrop.mode == BackdropMode::Auto)
        backdrop = buffer.suggestedBackground(opts.backdrop.darken, opts.backdrop.lumaThreshold);
    else if (opts.backdrop.mode == BackdropMode::Fixed) backdrop = opts.backdrop.color;

    // Not when the selector solved a background per cell. Painting one colour
    // over all of them throws that second colour away and leaves the backdrop
    // showing through wherever a glyph does not cover -- which reads as a dark
    // seam between every pair of blocks. The backdrop is still what pads the
    // picture; it just has no business inside the grid here.
    if (opts.backdrop.mode != BackdropMode::None && !opts.algo.allowBackground)
        buffer.fillBackground(backdrop);

    const std::string text =
        AnsiRenderer::render(buffer, charset, {.depth = toDepth(opts.output.color)});

    if (opts.output.stdoutEnabled) {
        Terminal::enableAnsi();
        Terminal::enableUtf8();
        std::cout << text;
    }

    if (opts.output.paths.empty()) return 0;

    ImageManager::setPngCompression(opts.output.pngCompression);

    GlyphAtlas renderAtlas;
    if (wantsImage(opts)) {
        // With no target size the natural render IS the file, so this number is
        // the glyph size you actually get. 16 is what a terminal draws at, but a
        // terminal hints its stems onto the pixel grid and we do not -- at 8x16
        // a letterform is a few grey blobs. 32 is the first size that reads as
        // crisp.
        int renderH = opts.font.renderSize;
        if (renderH <= 0)
            renderH = opts.output.imageHeight > 0
                          ? ImageRenderer::suggestedGlyphHeight(rows, opts.output.imageHeight)
                          : 32;

        renderH = std::max(2, renderH);
        renderAtlas = GlyphAtlas(font, charset, std::max(1, renderH / 2), renderH, opts.font.bold);
    }

    int status = 0;
    for (const std::string& given : opts.output.paths) {
        const std::filesystem::path path = resolveOutputPath(opts, given);

        if (!opts.output.overwrite && std::filesystem::exists(path)) {
            std::cerr << "asciigen: \"" << path << "\" exists (use --overwrite)\n";
            status = 6;
            continue;
        }

        const std::string ext = lowerExtension(path);
        bool ok = false;

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            ok = ImageRenderer::save(
                path, buffer, renderAtlas,
                {.width = opts.output.imageWidth,
                 .height = opts.output.imageHeight,
                 .fit = toFit(opts.output.fit),
                 .align = toAlign(opts.output.align),
                 .margin = opts.output.imageMargin,
                 .scale = opts.output.imageScale,
                 .aspect = opts.output.imageAspect,
                 .backgroundColor = backdrop}
            );
        } else if (ext == ".ans") {
            ok = OutputManager::saveAns(path, text);
        } else if (ext == ".txt") {
            ok = OutputManager::saveAns(
                path, AnsiRenderer::render(buffer, charset, {.depth = AnsiRenderer::ColorDepth::None})
            );
        } else {
            std::cerr << "asciigen: don't know how to write \"" << ext << "\" (.png .jpg .ans .txt)\n";
            status = 4;
            continue;
        }

        if (!ok) {
            std::cerr << "asciigen: failed writing \"" << path << "\"\n";
            status = 6;
        }
    }

    return status;
}

}   // namespace Pipeline
