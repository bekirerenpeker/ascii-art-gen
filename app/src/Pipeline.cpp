#include "Pipeline.hpp"
#include "core/Profiler.hpp"
#include "Assets.hpp"
#include "FrameProcessor.hpp"
#include "ProgressDisplay.hpp"
#include "bitmap/Resample.hpp"
#include "core/Charset.hpp"
#include "core/FrameStorage.hpp"
#include "dithering/Dithering.hpp"
#include "edges/Edges.hpp"
#include "file_management/ImageManager.hpp"
#include "file_management/OutputManager.hpp"
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

}   // namespace

int run(const Options& opts)
{
    if (opts.input.passthrough) return passthrough(opts);

    ASCIIGEN_PROFILE("run", "pipeline");

    // FrameStorage is the same object a video worker thread will process a frame into
    // later -- a still image is just the one-instance, one-call case of that. Loading
    // fills `input`; everything else is FrameProcessor::run's job, then this function
    // only decides what to do with `text`/`renderedImage` afterward, same as it always
    // decided what to do with the locals those used to be.
    FrameStorage frame;

    // One Line today -- a still image is the whole picture. Video will construct
    // this the same way, just with one Line per worker plus an aggregate; nothing
    // about Renderer/Watcher itself changes shape when that lands. Starts polling
    // immediately, stops (and does its own final draw + erase) via RAII on every
    // return path below, including the early ones.
    ProgressDisplay::Watcher watcher({{"frame", &frame.progress}});

    frame.progress.set("loading", 0.f);
    {
        ASCIIGEN_PROFILE("load", "io");
        frame.input = ImageManager::loadImage(opts.input.path);
    }
    if (!frame.input.pixels) return 3;

    frame.progress.set("setup", 0.02f);

    int cols = 0, rows = 0;
    resolveGridSize(opts, frame.input, cols, rows);

    Charset charset;
    {
        ASCIIGEN_PROFILE("buildCharset", "font");
        charset = buildCharset(opts);
    }

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
        .nms = opts.edge.nms,
        .colorSet = opts.edge.colorSet,
        .color = opts.edge.color,
        .brightness = opts.edge.brightness
    };

    Edges::alphaOptions = {
        .enabled = opts.edge.alphaOutline,
        .threshold = opts.edge.alphaThreshold,
        .coherence = opts.edge.alphaCoherence,
        .colorSet = opts.edge.alphaColorSet,
        .color = opts.edge.alphaColor,
        .brightness = opts.edge.alphaBrightness
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

    // Ramp reads one sample per cell; everything else works at atlas resolution.
    const int planeW = opts.algo.name == AlgoName::Ramp ? cols : cols * std::max(1, matchH / 2);
    const int planeH = opts.algo.name == AlgoName::Ramp ? rows : rows * matchH;

    Resample::Filter resampleFilter = Resample::Filter::Auto;
    if (opts.algo.resampleFilter == ResampleFilterName::Box) resampleFilter = Resample::Filter::Box;
    else if (opts.algo.resampleFilter == ResampleFilterName::Triangle)
        resampleFilter = Resample::Filter::Triangle;

    ImageManager::setPngCompression(opts.output.pngCompression);

    // Built once, same as matchAtlas above -- an image-format output shares one render
    // regardless of how many paths ask for one (renderToSize used to run again per path;
    // now FrameProcessor::run does it exactly once, into frame.renderedImage below).
    GlyphAtlas renderAtlas;
    const bool needsImage = wantsImage(opts);
    if (needsImage) {
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

    // Sizes buffer/plane up front -- steady-state (a future video's second frame
    // onward) allocates nothing new inside FrameProcessor::run.
    frame.allocate(cols, rows, planeW, planeH);

    // Built once, whichever one the algorithm actually needs -- both depend only on
    // matchAtlas and options that don't change frame to frame, so generate() no
    // longer rebuilds its own copy of this on every call the way it used to.
    FrameProcessor::Context ctx {
        .font = &font,
        .charset = &charset,
        .matchAtlas = &matchAtlas,
        .renderAtlas = needsImage ? &renderAtlas : nullptr,
        .resampleFilter = resampleFilter,
        .planeW = planeW,
        .planeH = planeH,
    };

    if (opts.algo.name == AlgoName::Structure) {
        Structure::buildGlyphModel(
            matchAtlas,
            {.orientBlocksX = opts.algo.structureOrientBlocksX,
             .orientBlocksY = opts.algo.structureOrientBlocksY,
             .bins = opts.algo.structureBins,
             .massBlocksX = opts.algo.structureMassBlocksX,
             .massBlocksY = opts.algo.structureMassBlocksY},
            ctx.structureModel
        );
    } else if (opts.algo.name == AlgoName::Bitmask) {
        Bitmask::buildModel(
            matchAtlas,
            {.allowBackground = opts.algo.allowBackground,
             .softness = opts.algo.bitmaskSoftness,
             .blurRadius = opts.algo.bitmaskBlurRadius},
            ctx.bitmaskModel
        );
    }

    FrameProcessor::run(frame, opts, ctx);

    // Stopped explicitly, not left to the destructor, so its background thread is
    // fully joined before anything below writes to the same stdout -- otherwise
    // the watcher's next redraw could land mid-write of the ASCII art itself.
    watcher.stop();

    if (opts.output.stdoutEnabled) {
        Terminal::enableAnsi();
        Terminal::enableUtf8();
        std::cout << frame.text;
    }

    if (opts.output.paths.empty()) return 0;

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
            ok = ImageManager::saveImage(path, frame.renderedImage);
        } else if (ext == ".ans") {
            ok = OutputManager::saveAns(path, frame.text);
        } else if (ext == ".txt") {
            ok = OutputManager::saveAns(
                path,
                AnsiRenderer::render(frame.buffer, charset, {.depth = AnsiRenderer::ColorDepth::None})
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
