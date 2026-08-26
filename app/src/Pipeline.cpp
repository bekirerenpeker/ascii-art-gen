#include "Pipeline.hpp"
#include "core/Profiler.hpp"
#include "Assets.hpp"
#include "FrameProcessor.hpp"
#include "FrameWorkerPool.hpp"
#include "ProgressDisplay.hpp"
#include "SaveQueue.hpp"
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
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

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

// A text/ANSI video (see runVideo) is a plain .txt/.ans file with one extra
// header line in front and this byte between every frame -- 0x1E, ASCII's own
// "record separator", chosen specifically because it can't appear in either a
// UTF-8 glyph or any of AnsiRenderer's escape sequences, so splitting on it
// can never mistake real frame content for a boundary.
constexpr const char* kTextVideoMagic = "ASCIIGEN-VIDEO";
constexpr char kFrameSeparator = '\x1E';

// Reads "key=value" pairs out of the header line written by runVideo below.
// Unknown keys are ignored rather than rejected, so a line a future version
// adds more fields to still parses here.
bool parseHeaderField(const std::string& line, const std::string& key, double& out)
{
    const std::string needle = key + "=";
    const size_t pos = line.find(needle);
    if (pos == std::string::npos) return false;

    try {
        out = std::stod(line.substr(pos + needle.size()));
    } catch (...) {
        return false;
    }

    return true;
}

// How much of the file to have in flight at once during playback -- big
// enough to amortise a read syscall over many frames for a typical grid size,
// small enough that memory use stays flat for the length of the video
// instead of scaling with it. Not a limit on any single frame's size: a
// frame bigger than this still gets read in full, just over more than one
// fill() call.
constexpr size_t kPlaybackChunkSize = 256 * 1024;

// Plays a text/ANSI video back in the terminal: print a frame, wait out its
// share of the video's own frame rate, print the next. Repositions the
// cursor itself by counting the previous frame's own lines and moving back up
// that many -- rather than relying on a .ans frame's own embedded cursor-home
// escape (see AnsiRenderer::render's screenControls) -- so a plain .txt
// video, which can't carry any escape bytes of its own, redraws in place
// exactly the same way a colored one does. Harmless overlap for a .ans file:
// its own embedded home-cursor lands at the same absolute position regardless
// of where this relative move already put the cursor. Paced off a fixed start
// time rather than sleeping a flat 1/fps after each frame, so per-frame
// printing overhead doesn't accumulate into drift over a long clip.
//
// Streams `file` rather than reading it whole first -- a several-thousand-
// frame clip's saved file can run into the hundreds of MB to low GB
// (especially a colored .ans, at maybe a hundred-plus bytes per cell), and
// reading all of that into one std::string before the first frame ever shows
// was measured to cost a long stall up front and, worse, kept degrading
// through the whole rest of playback (memory pressure from one enormous
// allocation, not anything algorithmic in how the frames were split out of
// it). `buffer` only ever holds one chunk plus at most one frame's worth of
// leftover from the read before it, so memory use here stays flat regardless
// of how long the video is.
int playTextVideo(std::ifstream& file, const std::string& headerLine, PlaybackPosition position)
{
    ASCIIGEN_PROFILE("playTextVideo", "playback");

    double fps = 24.0;
    parseHeaderField(headerLine, "fps", fps);
    if (fps <= 0.0) fps = 24.0;

    Terminal::CursorGuard cursorGuard;

    const bool topLeft = position == PlaybackPosition::TopLeft;

    // Once, before the first frame -- not per frame, and not for Inline,
    // which is specifically for starting below whatever's already on screen
    // rather than taking it over.
    if (topLeft) std::fputs("\x1b[H\x1b[2J", stdout);

    const std::chrono::duration<double> frameDuration(1.0 / fps);
    const auto start = std::chrono::steady_clock::now();

    std::vector<char> chunk(kPlaybackChunkSize);
    std::string buffer;
    bool eof = false;

    int frameIndex = 0;
    int previousLines = 0;

    for (;;) {
        size_t sep = buffer.find(kFrameSeparator);
        while (sep == std::string::npos && !eof) {
            ASCIIGEN_PROFILE("fill buffer", "playback");
            file.read(chunk.data(), (std::streamsize)chunk.size());
            const std::streamsize got = file.gcount();
            if (got > 0) buffer.append(chunk.data(), (size_t)got);
            if (got < (std::streamsize)chunk.size()) eof = true;
            sep = buffer.find(kFrameSeparator);
        }
        if (sep == std::string::npos) break;   // no more complete frames in the file

        {
            ASCIIGEN_PROFILE("print frame", "playback");

            // Plain stdio throughout, not std::cout -- mixing the two here
            // would risk the cursor-move escape and the frame text landing in
            // whichever order their separate buffers happened to flush in,
            // the same interleaving hazard ProgressDisplay.cpp's own drawing
            // avoids by sticking to one output mechanism throughout.
            //
            // TopLeft homes the cursor absolutely before every frame,
            // including the first -- immune to a shorter frame leaving a
            // longer one's leftover glyphs around it, and simpler than
            // tracking line counts at all. Inline only ever moves up by
            // exactly the PREVIOUS frame's own line count, starting from
            // wherever the cursor already was for the first frame -- correct
            // for a plain .txt file, which can carry no cursor-home escape
            // of its own to fall back on.
            if (topLeft) std::fputs("\x1b[H", stdout);
            else if (previousLines > 0) std::printf("\x1b[%dA", previousLines);
            std::fwrite(buffer.data(), 1, sep, stdout);
            std::fflush(stdout);
        }

        if (!topLeft) previousLines = (int)std::count(buffer.begin(), buffer.begin() + (std::ptrdiff_t)sep, '\n');
        frameIndex++;
        buffer.erase(0, sep + 1);

        std::this_thread::sleep_until(start + frameIndex * frameDuration);
    }

    return 0;
}

int passthrough(const Options& opts)
{
    ASCIIGEN_PROFILE("passthrough", "pipeline");

    std::ifstream file(opts.input.path, std::ios::binary);
    if (!file) {
        std::cerr << "asciigen: cannot read \"" << opts.input.path << "\"\n";
        return 3;
    }

    // The whole point of this path: `cat` and `type` do not put the console into
    // VT mode, so a .ans file prints as literal escape sequences without this.
    Terminal::enableAnsi();
    Terminal::enableUtf8();

    // Peeking at just the first line (then rewinding if it's not a match)
    // keeps the common case -- an ordinary still-image .txt/.ans someone
    // wants echoed as-is -- exactly as cheap as it was before this format
    // existed: no reason to read a plain file fully into memory just to
    // check whether it's the other kind.
    std::string firstLine;
    std::getline(file, firstLine);

    if (firstLine.rfind(kTextVideoMagic, 0) == 0)
        return playTextVideo(file, firstLine, opts.input.playPosition);

    file.clear();
    file.seekg(0);
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
//
// --grid-fit only matters when NEITHER --grid-width nor --grid-height was
// given -- giving either of those already fully determines the other via the
// aspect math below and has nothing to do with the terminal at all.
void resolveGridSize(const Options& opts, int srcW, int srcH, int& cols, int& rows)
{
    cols = opts.grid.width;
    rows = opts.grid.height;

    if (cols > 0 && rows > 0) return;

    if (cols <= 0 && rows <= 0) {
        int termCols = 0, termRows = 0;
        const bool haveTerminal =
            Terminal::isTty() && Terminal::getSize(termCols, termRows) && termCols > 0 && termRows > 0;

        // -1: every row printed ends in its own newline (see AnsiRenderer::
        // render), including the last one, so N rows leaves the cursor on
        // row N+1, not row N -- filling every one of the terminal's own
        // termRows rows therefore always scrolls it by exactly one line,
        // regardless of how it's rendered (a still image's single print, or
        // a video's top-left-homed redraw). One fewer row is what actually
        // fits without moving the window.
        const int usableRows = std::max(1, termRows - 1);

        if (!haveTerminal) {
            cols = 160;
        } else if (opts.grid.fitAxis == GridFitAxis::Width) {
            cols = termCols;
        } else if (opts.grid.fitAxis == GridFitAxis::Height) {
            rows = usableRows;
        } else {
            // Auto: the largest grid that fits inside BOTH terminal dimensions
            // at once while keeping the source's own shape, the same idea as
            // an image's "contain" fit -- rather than always sizing to the
            // terminal's width and letting whatever rows that implies overflow
            // the window's height, which is exactly what a portrait source at
            // a wide terminal used to do. Whichever axis the terminal is
            // relatively SHORTER on than the content needs is the one that
            // ends up constraining the result; the other is derived from it
            // by the same aspect math below, same as the Width/Height cases.
            const double idealAspect = 2.0 * srcW / srcH;   // cols:rows ratio that reproduces the source's shape
            if ((double)termCols / termRows > idealAspect) rows = usableRows;
            else cols = termCols;
        }
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
//
// --format overrides defaultExt when given, which is its whole purpose: a way
// to pick the output kind without spelling out a filename, going through this
// exact same bare-directory path and therefore the exact same downstream
// extension checks a hand-written filename would. Accepted with or without a
// leading dot and in any case ("mp4", ".MP4", "mp4" all resolve the same) --
// normalised here rather than at the flag, so every caller of this function
// benefits without having to know --format exists.
std::filesystem::path resolveOutputPath(
    const Options& opts, const std::string& given, const char* defaultExt = ".png"
)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(given, ec)) return given;

    std::string ext = defaultExt;
    if (!opts.output.format.empty()) {
        ext = opts.output.format;
        if (ext[0] != '.') ext = "." + ext;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
    }

    const std::string stem = std::filesystem::path(opts.input.path).stem().string();
    return std::filesystem::path(given) / (stem + ext);
}

std::string formatBytes(uint64_t bytes)
{
    static const char* units[] = {"B", "KB", "MB", "GB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        u++;
    }

    std::ostringstream ss;
    ss << std::fixed;
    ss.precision(u == 0 ? 0 : (v < 10.0 ? 2 : 1));
    ss << v << " " << units[u];
    return ss.str();
}

std::string formatBitrate(double bitsPerSecond)
{
    std::ostringstream ss;
    ss << std::fixed;
    if (bitsPerSecond >= 1e6) {
        ss.precision(1);
        ss << bitsPerSecond / 1e6 << " Mbps";
    } else {
        ss.precision(0);
        ss << bitsPerSecond / 1e3 << " kbps";
    }
    return ss.str();
}

// Empirical, not a spec constant: every sample file that actually opened in a
// standards-strict player during testing measured under 35 Mbps; the one that
// didn't measured over 100. There's no real MPEG-4 Part 2 profile/level that
// legitimately covers ASCII-render resolutions in the first place (see
// VideoWriter's own note), so this is a "loud enough to notice" line in that
// gap, not a guarantee on either side of it.
constexpr double kBitrateWarningThreshold = 50e6;

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

// A charset built from a fixed Unicode range (Charset::blocks/braille) has no
// way to know ahead of time whether the FONT actually has every one of those
// codepoints -- FT_Load_Char silently substitutes the font's own .notdef
// glyph for one that's missing rather than failing (see Font::hasGlyph's own
// note on why that's invisible to the caller otherwise), so an unfiltered
// charset lets the algorithm select glyphs that render as a placeholder box
// in the middle of otherwise-correct output -- discovered this exact way:
// Consolas (first in this project's own default-font search order) is
// missing some of Charset::blocks()'s quadrant glyphs (U+2596-U+259F), and
// they showed up as boxed "?" tofu once colour stopped masking them.
//
// Left untouched if filtering would remove everything: distinctGlyphCount's
// own check downstream already covers total failure, and an empty charset
// would be worse than a fully-unsupported one.
void filterUnsupportedGlyphs(Charset& charset, const Font& font, const std::string& fontName)
{
    std::u32string kept;
    std::vector<char32_t> dropped;

    for (uint16_t i = 0; i < charset.size(); i++) {
        const char32_t cp = charset.codepointAt(i);
        if (font.hasGlyph(cp)) kept += cp;
        else dropped.push_back(cp);
    }

    if (dropped.empty() || kept.empty()) return;

    std::ostringstream list;
    for (size_t i = 0; i < dropped.size(); i++) {
        if (i > 0) list << ' ';
        list << "U+" << std::hex << std::uppercase << (uint32_t)dropped[i];
    }

    std::cerr << "asciigen: \"" << fontName << "\" is missing " << dropped.size()
              << (dropped.size() == 1 ? " glyph" : " glyphs")
              << " this charset wanted -- skipped so none render as a placeholder box: "
              << list.str() << "\n";

    charset = Charset(kept);
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
    // first. Worth failing on up front rather than mid-encode. .txt/.ans are
    // accepted too -- see runTextVideo below -- as a genuinely different kind
    // of "video" output, not a pixel container.
    const std::string outExt = lowerExtension(outPath);
    static const std::set<std::string> kVideoExtensions {".mp4", ".mkv", ".mov", ".avi", ".webm", ".m4v"};
    static const std::set<std::string> kTextVideoExtensions {".txt", ".ans"};
    const bool isTextOutput = kTextVideoExtensions.count(outExt) > 0;
    if (!isTextOutput && !kVideoExtensions.count(outExt)) {
        std::cerr << "asciigen: \"" << outExt << "\" isn't a video container asciigen can write to -- "
                  << "video input needs a video --out (.mp4, .mkv, .mov, .avi, .webm, .m4v, .txt, .ans)\n";
        return 4;
    }

    if (!opts.output.overwrite && std::filesystem::exists(outPath)) {
        std::cerr << "asciigen: " << outPath << " exists (use --overwrite)\n";
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

    filterUnsupportedGlyphs(charset, font, fontPath.filename().string());

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

    // Only built for a pixel output -- a text/ANSI one has no use for a
    // rendered glyph atlas at all (see FrameProcessor::run's own note: a null
    // renderAtlas skips that whole step), so skipping the construction here
    // saves rasterising every glyph in the charset for nothing.
    std::unique_ptr<GlyphAtlas> renderAtlas;
    if (!isTextOutput) {
        int renderH = opts.font.renderSize > 0 ? opts.font.renderSize : 32;
        renderH = std::max(2, renderH);
        renderAtlas = std::make_unique<GlyphAtlas>(font, charset, std::max(1, renderH / 2), renderH, opts.font.bold);
    }

    FrameProcessor::Context ctx {
        .font = &font,
        .charset = &charset,
        .matchAtlas = &matchAtlas,
        .renderAtlas = renderAtlas.get(),
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
    //
    // One core deliberately left unclaimed (when there's more than one to
    // begin with): workerCount here plus the decoder, saver and closer
    // threads already oversubscribes every core by a few threads, and this
    // project's own progress display -- running on the thread that's
    // otherwise just waiting -- was measured going visibly choppy under that
    // load (updates arriving every several hundred ms instead of the ~80ms it
    // asks for) specifically for a text/ANSI output, where every worker's
    // extra AnsiRenderer::render() call adds real CPU time on top of the
    // usual per-frame work. Giving the display's own thread a fighting chance
    // at getting scheduled promptly is worth marginally more contention
    // between the workers themselves.
    const unsigned hw = std::thread::hardware_concurrency();
    const int workerCount = std::max(1, (int)(hw > 1 ? hw - 1 : hw ? hw : 4));
    const int slotCount = workerCount + 3;

    FramePool pool;
    pool.allocate(slotCount, cols, rows, planeW, planeH);

    const double sourceFps = info.fps > 0.0 ? info.fps : 24.0;

    // --fps only ever drops frames to reach a lower rate -- see VideoOptions'
    // own note on why duplicating frames to fake a higher one isn't supported.
    const double outputFps =
        (opts.video.fps > 0.0 && opts.video.fps < sourceFps) ? opts.video.fps : sourceFps;

    // --start-/--end-frame take precedence over their --start-/--end-time
    // equivalents when both are somehow given; converted to seconds here so
    // the decode loop below only ever has to reason about one timeline.
    double startT = 0.0;
    if (opts.video.startFrame >= 0) startT = opts.video.startFrame / sourceFps;
    else if (opts.video.startTime >= 0.0) startT = opts.video.startTime;

    double endT = std::numeric_limits<double>::infinity();
    if (opts.video.endFrame >= 0) endT = (opts.video.endFrame + 1) / sourceFps;
    else if (opts.video.endTime >= 0.0) endT = opts.video.endTime;

    if (endT <= startT) {
        std::cerr << "asciigen: --end-time/--end-frame is before --start-time/--start-frame\n";
        return 4;
    }

    // Re-estimated from the actual output window and rate rather than the raw
    // container frame count, which doesn't mean much once trimming and/or
    // --fps are in play. Used for the progress bar's total below, and (for a
    // text/ANSI output) written into the header up front, before the real
    // count is known -- playback (see playTextVideo) counts actual frame
    // separators rather than trusting that number, so an estimate that turns
    // out wrong costs nothing worse than a slightly-off progress percentage.
    int64_t estimatedTotalFrames = info.frameCount;
    if (info.durationSeconds > 0.0) {
        const double windowEnd = std::min(endT, info.durationSeconds);
        if (windowEnd > startT) estimatedTotalFrames = (int64_t)((windowEnd - startT) * outputFps + 0.5);
    }

    int outW = 0, outH = 0;
    std::optional<VideoManager::VideoWriter> pixelWriter;
    std::ofstream textOut;

    if (isTextOutput) {
        textOut.open(outPath, std::ios::binary);
        if (!textOut) {
            std::cerr << "asciigen: couldn't open " << outPath << " for writing\n";
            return 6;
        }

        textOut << kTextVideoMagic << " fps=" << outputFps << " frames=" << estimatedTotalFrames
                << " duration=" << (outputFps > 0.0 ? estimatedTotalFrames / outputFps : 0.0) << "\n";
    } else {
        outW = renderAtlas->cellWidth() * cols;
        outH = renderAtlas->cellHeight() * rows;

        pixelWriter.emplace(outPath, outW, outH, outputFps);
        if (!pixelWriter->isOpen()) {
            std::cerr << "asciigen: couldn't open " << outPath << " for writing\n";
            return 6;
        }
    }

    // Bounded ordered handoff between however many workers finish rendering (out
    // of order) and the one thread that writes frames out in sequence -- see
    // SaveQueue's own note on why capacity exists and why the next-in-line
    // frame is the one exception to it. Capacity matches workerCount: that's
    // "one wave" of concurrently-finishing workers' worth of backlog, which is
    // as much slack as there's ever a reason to want -- a bigger number just
    // lets the write side fall further behind before anything feels it.
    //
    // Both declared unconditionally (one stays empty and unused) rather than
    // picked with a pointer -- SaveQueue<Image> and SaveQueue<std::string> are
    // different types, and everything below that needs to outlive this
    // function's if/else branches (the manager's callback, the saver and
    // closer threads) would otherwise be referencing a queue that already
    // went out of scope by the time it runs.
    SaveQueue<Image> pixelSaveQueue(workerCount);
    SaveQueue<std::string> textSaveQueue(workerCount);

    std::atomic<bool> saverFinished {false};
    std::atomic<int> framesWritten {0};

    // Each worker pushes its own rendered frame here as soon as it has one --
    // may block if the relevant queue is full (see SaveQueue), which only
    // holds up that one worker, never the decoder or any other worker; the
    // slot it was using is already freed by the time this runs (see
    // FrameWorkerPool).
    std::optional<FrameWorkerPool::Manager> manager;
    if (isTextOutput) {
        AnsiRenderer::ColorDepth depth = AnsiRenderer::ColorDepth::None;
        if (outExt == ".ans") {
            switch (opts.output.color) {
            case ColorMode::Ansi16: depth = AnsiRenderer::ColorDepth::Ansi16; break;
            case ColorMode::TrueColor: depth = AnsiRenderer::ColorDepth::TrueColor; break;
            case ColorMode::None: break;
            }
        }

        manager.emplace(
            pool, opts, ctx, workerCount,
            [&](int frameIndex, Image&&, std::string&& text) {
                textSaveQueue.push(frameIndex, std::move(text));
            },
            AnsiRenderer::AnsiRenderOptions {
                .depth = depth,
                .transparentBackground = opts.backdrop.mode == BackdropMode::Transparent,
                // Only for .ans -- a .txt frame has to stay pure text with no
                // escape bytes in it at all, same reason its depth is forced
                // to None just above. playTextVideo doesn't depend on this
                // either way: it repositions the cursor itself by counting
                // each frame's own lines, so a .txt file plays back correctly
                // with no embedded control codes of its own; this is purely
                // an extra for a .ans file opened some other way than through
                // this project's own player.
                .screenControls = outExt == ".ans"
            }
        );
    } else {
        manager.emplace(pool, opts, ctx, workerCount, [&](int frameIndex, Image&& img, std::string&&) {
            pixelSaveQueue.push(frameIndex, std::move(img));
        });
    }

    // The only thread ever allowed to call reader.nextFrame() -- decode is
    // inherently sequential internal state, see VideoManager.hpp. Also where
    // --start-/--end-time(-frame) and --fps downsampling happen: there's no
    // seeking (same note), so reaching startT or skipping a frame the target
    // rate doesn't need still costs decoding it -- just into `scratch`
    // instead of a pool slot, so a frame nobody will submit never occupies
    // one. nextOutputTime is recomputed from outFrameIndex each pass rather
    // than accumulated by repeated += so float drift can't creep in over a
    // long clip and eventually cost or duplicate a frame at the boundary.
    // Entirely independent of what kind of output this run is producing --
    // it only ever feeds decoded pixels into pool slots for
    // FrameProcessor::run -- so unlike everything above it, it isn't inside
    // either branch.
    std::thread decoderThread([&] {
        Profiler::nameThread("decoder");

        int outFrameIndex = 0;
        int64_t srcFrameIndex = 0;
        Image scratch;

        for (;;) {
            const double nextOutputTime = startT + outFrameIndex / outputFps;
            if (nextOutputTime >= endT) break;

            const double srcTime = (double)srcFrameIndex / sourceFps;
            const double srcFrameEnd = (double)(srcFrameIndex + 1) / sourceFps;
            srcFrameIndex++;
            if (srcTime >= endT) break;

            if (srcFrameEnd <= nextOutputTime) {
                if (!reader.nextFrame(scratch)) break;   // end of stream
                continue;
            }

            const int idx = pool.waitForFreeSlot();
            if (idx < 0) break;   // pool closed before we ever got here -- shouldn't happen

            if (!reader.nextFrame(pool.slot(idx).storage.input)) break;   // end of stream

            pool.submit(idx, outFrameIndex);
            outFrameIndex++;
        }
        pool.closeQueue();
    });

    std::thread saverThread;
    if (isTextOutput) {
        saverThread = std::thread([&] {
            Profiler::nameThread("saver");

            std::string text;
            while (textSaveQueue.popNextInOrder(text)) {
                textOut << text << kFrameSeparator;
                framesWritten.fetch_add(1, std::memory_order_relaxed);
            }
            textOut.close();
            saverFinished.store(true, std::memory_order_release);
        });
    } else {
        saverThread = std::thread([&] {
            Profiler::nameThread("saver");

            Image img;
            while (pixelSaveQueue.popNextInOrder(img)) {
                pixelWriter->writeFrame(img);
                framesWritten.fetch_add(1, std::memory_order_relaxed);
            }
            pixelWriter->finish();
            saverFinished.store(true, std::memory_order_release);
        });
    }

    // The relevant queue's close() can only run once every push() that will
    // ever happen already has -- i.e. after every worker has exited, which is
    // after the decoder has stopped submitting. Doing that wait on a fourth
    // thread (not the main one) is what lets the main thread block in
    // runUntilDone below without deadlocking against this: it can't wait for
    // the saver to finish AND be the thing responsible for telling the saver
    // nothing more is coming.
    std::thread closerThread([&] {
        decoderThread.join();
        manager->join();
        if (isTextOutput) textSaveQueue.close();
        else pixelSaveQueue.close();
    });

    std::vector<ProgressDisplay::Line> lines;

    lines.push_back(ProgressDisplay::Line {[&] {
        const int64_t total = estimatedTotalFrames;
        const int done = framesWritten.load(std::memory_order_relaxed);
        std::string label =
            "overall (" + std::to_string(done) + "/" + (total > 0 ? std::to_string(total) : "?") + ")";
        const float fraction = total > 0 ? std::clamp((float)done / (float)total, 0.f, 1.f) : 0.f;
        return ProgressDisplay::Snapshot {std::move(label), "frames", fraction};
    }});

    for (int i = 0; i < workerCount; i++) {
        lines.push_back(ProgressDisplay::Line {[&manager, &pool, i] {
            const int s = manager->currentSlot(i);
            if (s < 0) {
                // Distinguishes the two ways a worker can be idle: nothing decoded
                // yet to give it, vs. it's holding a finished frame the saver
                // hasn't made room for -- both used to collapse into plain "idle".
                const char* stage = manager->workerState(i) == FrameWorkerPool::WorkerState::HandingOff
                    ? "saving" : "decode";
                return ProgressDisplay::Snapshot {"thread " + std::to_string(i + 1), stage, 0.f};
            }

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

    // Both should already be finished, or nearly so -- saverFinished only ever
    // becomes true after the relevant queue closes, which closerThread only
    // does once decoderThread and every worker are already done. These joins
    // are just making that explicit rather than relying on it.
    closerThread.join();
    saverThread.join();

    // Real numbers off the finished file, not either writer's own nominal
    // target -- see VideoWriter's own note on why those two can differ a lot
    // for a pixel output. durationSeconds comes from frames actually written
    // over outputFps rather than the source's duration, since trimming/--fps
    // can make them different on purpose.
    std::error_code sizeEc;
    const uint64_t fileBytes = std::filesystem::file_size(outPath, sizeEc);
    const int writtenFrames = framesWritten.load(std::memory_order_relaxed);
    const double durationSeconds = outputFps > 0.0 ? writtenFrames / outputFps : 0.0;

    std::cout << "asciigen: wrote " << outPath;
    if (!sizeEc) std::cout << " (" << formatBytes(fileBytes) << ")";

    if (isTextOutput) {
        std::cout << " -- " << cols << "x" << rows << " cells, " << outputFps << "fps, " << writtenFrames
                  << " frames, " << durationSeconds << "s\n";
        return 0;
    }

    const double bitrate =
        (!sizeEc && durationSeconds > 0.0) ? (double)fileBytes * 8.0 / durationSeconds : 0.0;
    std::cout << " -- " << outW << "x" << outH << ", " << outputFps << "fps, " << writtenFrames
              << " frames, " << durationSeconds << "s";
    if (bitrate > 0.0) std::cout << ", " << formatBitrate(bitrate);
    std::cout << "\n";

    if (bitrate > kBitrateWarningThreshold) {
        std::cerr << "asciigen: warning: " << outPath << " encoded at roughly "
                  << formatBitrate(bitrate) << " -- MPEG-4 (the only codec this LGPL build can "
                  << "write, see VideoManager.hpp) doesn't really have a legal profile/level for "
                  << "bitrates this high, and some players' decoders will refuse to open it. If it "
                  << "won't open: try a lower --fps, a smaller --grid-width/--image-width, or "
                  << "trimming with --start-time/--end-time.\n";
    }

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

    filterUnsupportedGlyphs(charset, font, fontPath.filename().string());

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
    // still image, workerCount for video, identical Manager either way.
    // Everything else (submission, progress display, waiting) stays right here
    // on the main thread; only FrameProcessor::run itself ever leaves it.
    //
    // The rendered image is handed back through onRendered rather than read
    // from the slot afterward -- FrameStorage no longer holds one at all, see
    // its own note on why. imageDone is this path's equivalent of video's
    // saverFinished: a plain flag the single callback sets once the one frame
    // that will ever exist here has arrived.
    Image renderedImage;
    std::atomic<bool> imageDone {false};

    pool.submit(0);
    {
        FrameWorkerPool::Manager manager(
            pool, opts, ctx, /*workerCount=*/1,
            [&](int, Image&& img, std::string&&) {
                renderedImage = std::move(img);
                imageDone.store(true, std::memory_order_release);
            }
        );

        // Blocks THIS thread, not a new one -- see ProgressDisplay.hpp's note on
        // why that's correct now that the work being watched already left the
        // main thread.
        ProgressDisplay::runUntilDone(
            [&] { return imageDone.load(std::memory_order_acquire); },
            {ProgressDisplay::Line {[&] {
                FrameProgress& p = pool.slot(0).storage.progress;
                return ProgressDisplay::Snapshot {
                    "frame", p.stage.load(std::memory_order_relaxed),
                    p.fraction.load(std::memory_order_relaxed)
                };
            }}}
        );

        // Explicit, not left to the Manager destructor, purely for thread
        // hygiene: onRendered's own store/load pair already guarantees
        // renderedImage is visible once imageDone reads true, so this isn't
        // needed for correctness -- it's here so the worker thread itself is
        // fully wound down before this scope ends rather than sitting
        // joined-on-exit.
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
            std::cerr << "asciigen: " << path << " exists (use --overwrite)\n";
            status = 6;
            continue;
        }

        const std::string ext = lowerExtension(path);
        bool ok = false;

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            ok = ImageManager::saveImage(path, renderedImage);
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
            std::cerr << "asciigen: failed writing " << path << "\n";
            status = 6;
            continue;
        }

        std::error_code sizeEc;
        const uint64_t fileBytes = std::filesystem::file_size(path, sizeEc);
        std::cout << "asciigen: wrote " << path;
        if (!sizeEc) std::cout << " (" << formatBytes(fileBytes) << ")";
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            std::cout << " -- " << renderedImage.width << "x" << renderedImage.height;
        std::cout << "\n";
    }

    return status;
}

}   // namespace Pipeline
