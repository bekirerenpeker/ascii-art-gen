#include "Pipeline.hpp"
#include "core/Profiler.hpp"
#include "Assets.hpp"
#include "FrameProcessor.hpp"
#include "FrameWorkerPool.hpp"
#include "ProgressDisplay.hpp"
#include "bitmap/Resample.hpp"
#include "core/Charset.hpp"
#include "core/FramePool.hpp"
#include "dithering/Dithering.hpp"
#include "edges/Edges.hpp"
#include "file_management/ImageManager.hpp"
#include "file_management/OutputManager.hpp"
#include "font/Font.hpp"
#include "font/GlyphAtlas.hpp"
#include "output/AnsiRenderer.hpp"
#include "output/ImageRenderer.hpp"
#include "output/Terminal.hpp"
#include "file_management/VideoManager.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <thread>

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

// --preview: decode a video only up to (and keep) one frame, for the still-image
// path below to treat exactly like any other loaded image. No seeking -- there
// isn't one, see VideoManager.hpp -- so reaching frame N costs decoding 0..N
// regardless; this is the cheapest a specific frame can ever be gotten to, and
// frame 0 (the default) is instant.
Image loadPreviewFrame(const std::filesystem::path& path, int frameIndex)
{
    VideoManager::VideoReader reader(path);
    if (!reader.isOpen()) return Image();

    Image frame;
    for (int i = 0; i <= frameIndex; i++) {
        if (!reader.nextFrame(frame)) {
            std::cerr << "asciigen: video ended after " << i << " frame(s), can't preview frame "
                      << frameIndex << "\n";
            return Image();
        }
    }
    return frame;
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
// shape needs half as many rows as the raw aspect suggests. Takes plain
// dimensions rather than an Image so a video (which only has VideoInfo's
// width/height, not a decoded frame, at the point this needs to run) can call
// it the same way a still image does.
void resolveGridSize(const Options& opts, int srcW, int srcH, int& cols, int& rows)
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

    if (cols > 0 && rows <= 0) rows = std::max(1, (int)std::lround((double)cols * srcH / (srcW * 2.0)));
    else if (rows > 0 && cols <= 0) cols = std::max(1, (int)std::lround((double)rows * srcW * 2.0 / srcH));
}

// A path that names an existing directory means "put it in here, called after
// the input". Saves renaming the output every time you convert a new picture.
// `defaultExt` is what a bare directory can't tell us -- png for a still image,
// mp4 for video; getting this wrong for video isn't cosmetic, it's a real
// failure (the wrong extension makes FFmpeg guess an image-sequence muxer for
// what's actually one continuous file, which then refuses every frame after
// the first).
std::filesystem::path resolveOutputPath(
    const Options& opts, const std::string& given, const char* defaultExt = ".png"
)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(given, ec)) return given;

    const std::string stem = std::filesystem::path(opts.input.path).stem().string();
    return std::filesystem::path(given) / (stem + defaultExt);
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

// The whole video path: a decode thread feeding a FramePool, a FrameWorkerPool
// running the exact same FrameProcessor::run every still image uses, and a save
// thread writing finished frames back out in order. Kept as its own function
// rather than folded into run() below -- the two share the worker-pool
// machinery (that's the point) but differ enough in how frames arrive and
// leave (one decoded stream in, one muxed file out, vs. one loaded image and a
// handful of independent output paths) that forcing them into one function
// would mostly be a pile of `if (isVideo)` branches, not real sharing.
int runVideo(const Options& opts)
{
    ASCIIGEN_PROFILE("runVideo", "pipeline");

    Terminal::enableAnsi();
    Terminal::enableUtf8();

    VideoManager::VideoReader reader(opts.input.path);
    if (!reader.isOpen()) {
        std::cerr << "asciigen: couldn't open video \"" << opts.input.path << "\"\n";
        return 3;
    }
    const VideoManager::VideoInfo& info = reader.info();

    if (opts.output.paths.empty()) {
        std::cerr << "asciigen: video input needs an --out path\n";
        return 4;
    }
    const std::filesystem::path outPath = resolveOutputPath(opts, opts.output.paths[0], ".mp4");

    // Mandatory, not deferred like the general input/output compatibility check
    // (video-roadmap.md item 11) -- an image extension here doesn't "might not
    // work", it makes FFmpeg pick a single-image muxer for what's actually one
    // continuous stream of frames, which then rejects every frame after the
    // first. Worth failing on up front rather than mid-encode.
    const std::string outExt = lowerExtension(outPath);
    static const std::set<std::string> kVideoExtensions {".mp4", ".mkv", ".mov", ".avi", ".webm", ".m4v"};
    if (!kVideoExtensions.count(outExt)) {
        std::cerr << "asciigen: \"" << outExt << "\" isn't a video container asciigen can write to -- "
                  << "video input needs a video --out (.mp4, .mkv, .mov, .avi, .webm, .m4v)\n";
        return 4;
    }

    if (!opts.output.overwrite && std::filesystem::exists(outPath)) {
        std::cerr << "asciigen: \"" << outPath << "\" exists (use --overwrite)\n";
        return 6;
    }

    int cols = 0, rows = 0;
    resolveGridSize(opts, info.width, info.height, cols, rows);

    Charset charset = buildCharset(opts);

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

    auto fontHolder = std::make_unique<Font>(fontPath);
    Font& font = *fontHolder;

    const int matchH = std::max(2, opts.font.matchSize);
    GlyphAtlas matchAtlas(font, charset, std::max(1, matchH / 2), matchH);

    if (distinctGlyphCount(matchAtlas) < 2) {
        std::cerr << "asciigen: \"" << fontPath.filename().string()
                  << "\" has no glyphs for this charset, so the output will be blank.\n"
                  << "  pick a font that covers it, e.g.\n"
                  << "  --font-path C:/Windows/Fonts/CascadiaMono.ttf\n";
    }

    const int planeW = opts.algo.name == AlgoName::Ramp ? cols : cols * std::max(1, matchH / 2);
    const int planeH = opts.algo.name == AlgoName::Ramp ? rows : rows * matchH;

    Resample::Filter resampleFilter = Resample::Filter::Auto;
    if (opts.algo.resampleFilter == ResampleFilterName::Box) resampleFilter = Resample::Filter::Box;
    else if (opts.algo.resampleFilter == ResampleFilterName::Triangle)
        resampleFilter = Resample::Filter::Triangle;

    // Video output is always a rendered pixel frame -- there's no analogue yet
    // of a still image's "maybe .txt, maybe .ans, maybe nothing" output menu.
    // Ansi/text video output is deliberately later work, not this pass.
    int renderH = opts.font.renderSize > 0 ? opts.font.renderSize : 32;
    renderH = std::max(2, renderH);
    GlyphAtlas renderAtlas(font, charset, std::max(1, renderH / 2), renderH, opts.font.bold);

    FrameProcessor::Context ctx {
        .font = &font,
        .charset = &charset,
        .matchAtlas = &matchAtlas,
        .renderAtlas = &renderAtlas,
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

    // Not final values -- see video-roadmap.md item 8. Slack beyond workerCount
    // is what keeps a worker from ever waiting on the decoder for a free slot in
    // the common case (decode is cheap next to a frame's own select/render cost).
    const unsigned hw = std::thread::hardware_concurrency();
    const int workerCount = std::max(1, (int)(hw ? hw : 4));
    const int slotCount = workerCount + 3;

    FramePool pool;
    pool.allocate(slotCount, cols, rows, planeW, planeH);

    const int outW = renderAtlas.cellWidth() * cols;
    const int outH = renderAtlas.cellHeight() * rows;
    const double fps = info.fps > 0.0 ? info.fps : 24.0;

    VideoManager::VideoWriter writer(outPath, outW, outH, fps);
    if (!writer.isOpen()) {
        std::cerr << "asciigen: couldn't open \"" << outPath << "\" for writing\n";
        return 6;
    }

    FrameWorkerPool::Manager manager(pool, opts, ctx, workerCount);

    // Ordered save: workers finish in whatever order they finish, but the
    // muxer needs frames in sequence. `pending` holds whatever's arrived out of
    // turn until the gap in front of it closes -- see video-roadmap.md item 5's
    // "ordered save" writeup, this is that, finally built. The slot is freed
    // (for the decoder to reuse) the moment its frame is copied out, not once
    // it's actually been written -- the decoder has no reason to wait on
    // however far behind the muxer might be.
    std::atomic<bool> saverFinished {false};
    std::atomic<int> framesWritten {0};

    std::thread saverThread([&] {
        std::map<int, Image> pending;
        int nextToWrite = 0;

        auto writeOne = [&](Image& img) {
            writer.writeFrame(img);
            framesWritten.fetch_add(1, std::memory_order_relaxed);
        };

        for (;;) {
            const int idx = pool.waitForDone();
            if (idx < 0) break;   // nothing left, ever

            FrameSlot& slot = pool.slot(idx);
            const int frameIndex = slot.frameIndex;
            const Image& src = slot.storage.renderedImage;

            Image copy;
            if (src.pixels) {
                copy = Image(src.width, src.height, src.depth);
                std::memcpy(
                    copy.pixels, src.pixels, (size_t)src.width * src.height * src.depth
                );
            }
            pool.markFree(idx);

            if (frameIndex == nextToWrite) {
                writeOne(copy);
                nextToWrite++;

                for (auto it = pending.find(nextToWrite); it != pending.end();
                     it = pending.find(nextToWrite)) {
                    writeOne(it->second);
                    pending.erase(it);
                    nextToWrite++;
                }
            } else {
                pending.emplace(frameIndex, std::move(copy));
            }
        }

        writer.finish();
        saverFinished.store(true, std::memory_order_release);
    });

    // The only thread ever allowed to call reader.nextFrame() -- decode is
    // inherently sequential internal state, see VideoManager.hpp.
    std::thread decoderThread([&] {
        int frameIndex = 0;
        for (;;) {
            const int idx = pool.waitForFreeSlot();
            if (idx < 0) break;   // pool closed before we ever got here -- shouldn't happen

            if (!reader.nextFrame(pool.slot(idx).storage.input)) break;   // end of stream

            pool.submit(idx, frameIndex);
            frameIndex++;
        }
        pool.closeQueue();
    });

    std::vector<ProgressDisplay::Line> lines;

    lines.push_back(ProgressDisplay::Line {[&] {
        const int64_t total = info.frameCount;
        const int done = framesWritten.load(std::memory_order_relaxed);
        std::string label =
            "overall (" + std::to_string(done) + "/" + (total > 0 ? std::to_string(total) : "?") + ")";
        const float fraction = total > 0 ? std::clamp((float)done / (float)total, 0.f, 1.f) : 0.f;
        return ProgressDisplay::Snapshot {std::move(label), "frames", fraction};
    }});

    for (int i = 0; i < workerCount; i++) {
        lines.push_back(ProgressDisplay::Line {[&manager, &pool, i] {
            const int s = manager.currentSlot(i);
            if (s < 0) return ProgressDisplay::Snapshot {"thread " + std::to_string(i + 1), "idle", 0.f};

            FrameSlot& slot = pool.slot(s);
            FrameProgress& p = slot.storage.progress;
            return ProgressDisplay::Snapshot {
                "thread " + std::to_string(i + 1) + " (frame " + std::to_string(slot.frameIndex) + ")",
                p.stage.load(std::memory_order_relaxed), p.fraction.load(std::memory_order_relaxed)
            };
        }});
    }

    ProgressDisplay::runUntilDone(
        [&] { return saverFinished.load(std::memory_order_acquire); }, lines
    );

    decoderThread.join();
    saverThread.join();
    manager.join();

    return 0;
}

}   // namespace

int run(const Options& opts)
{
    if (opts.input.passthrough) return passthrough(opts);

    // --preview deliberately routes around runVideo() entirely rather than
    // adding an `if (preview)` inside it: this whole function's own output
    // handling (png/jpg/ans/txt, whatever's asked for) already applies as-is
    // once execution reaches here, so there's no separate "allow an image
    // output for this one video" case to carve out of runVideo()'s mandatory
    // video-container check below -- that check simply never runs on this path.
    const bool isVideo = VideoManager::looksLikeVideo(opts.input.path);
    if (isVideo && opts.input.previewFrame < 0) return runVideo(opts);

    ASCIIGEN_PROFILE("run", "pipeline");

    // Unconditional and this early on purpose: the progress bar needs both of
    // these active before it ever draws a frame, not just before the final
    // ASCII art gets printed. Without enableUtf8() first, the console is still
    // decoding stdout under its default (non-UTF-8) codepage while the bar's
    // own escape/glyph bytes go out, which is exactly what garbles it. A no-op
    // when stdout is redirected -- see Terminal::enableAnsi()'s own note.
    Terminal::enableAnsi();
    Terminal::enableUtf8();

    // Loaded before there's anywhere to put it -- resolveGridSize below needs the
    // source's own dimensions, and the pool below needs the grid size to know how
    // big to make each slot, so this has to come first regardless.
    Image loadedInput;
    {
        ASCIIGEN_PROFILE("load", "io");
        if (isVideo) loadedInput = loadPreviewFrame(opts.input.path, std::max(0, opts.input.previewFrame));
        else loadedInput = ImageManager::loadImage(opts.input.path);
    }
    if (!loadedInput.pixels) return 3;

    int cols = 0, rows = 0;
    resolveGridSize(opts, loadedInput.width, loadedInput.height, cols, rows);

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

    // One slot, one call -- video will be `pool.allocate(workerCount + slack, ...)`
    // with the same call, the same class, just a bigger number. Sizes every slot's
    // buffer/plane up front (see FrameStorage::allocate), so steady-state (a
    // future video's second frame onward) allocates nothing new inside
    // FrameProcessor::run.
    FramePool pool;
    pool.allocate(1, cols, rows, planeW, planeH);
    pool.slot(0).storage.input = std::move(loadedInput);

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

    // The actual frame processing runs on a worker thread -- one worker for a
    // still image, workerCount for video later, identical Manager either way.
    // Everything else (submission, progress display, waiting) stays right here
    // on the main thread; only FrameProcessor::run itself ever leaves it.
    pool.submit(0);
    {
        FrameWorkerPool::Manager manager(pool, opts, ctx, /*workerCount=*/1);

        // Blocks THIS thread, not a new one -- see ProgressDisplay.hpp's note on
        // why that's correct now that the work being watched already left the
        // main thread. Reads pool.slot(0) directly rather than the `frame`
        // reference below, since that reference isn't declared until the
        // worker's result is guaranteed ready.
        ProgressDisplay::runUntilDone(
            [&] { return pool.allDone(); },
            {ProgressDisplay::Line {[&] {
                FrameProgress& p = pool.slot(0).storage.progress;
                return ProgressDisplay::Snapshot {
                    "frame", p.stage.load(std::memory_order_relaxed),
                    p.fraction.load(std::memory_order_relaxed)
                };
            }}}
        );

        // Explicit, not left to the Manager destructor, purely for thread
        // hygiene: the worker's acquire/release pair on `state` already
        // guarantees everything it wrote to `slot.storage` is visible once
        // allDone() reads true, so this isn't needed for correctness -- it's
        // here so the worker thread itself is fully wound down before this
        // scope ends rather than sitting joined-on-exit.
        manager.join();
    }

    FrameStorage& frame = pool.slot(0).storage;

    if (opts.output.stdoutEnabled) {
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
