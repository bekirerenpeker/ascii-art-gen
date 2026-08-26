#pragma once

#include "core/Color.hpp"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// Every option the application understands, in one struct, with every default
// written here rather than scattered across the engine. The engine's own struct
// defaults still exist and still apply when it is used as a library -- these
// mirror them, so there is exactly one place to look when asking what the CLI
// does with no flags.
namespace App {

enum class AlgoName
{
    Ramp,
    Bitmask,
    Structure,
};

enum class CharsetName
{
    Ascii,
    Blocks,
    Braille,
    Ramp,
    Custom,
};

enum class DitherName
{
    None,
    Bayer4,
};

enum class ResampleFilterName
{
    Auto,
    Box,
    Triangle,
};

enum class EdgeName
{
    None,
    Scharr,
};

enum class PaletteName
{
    None,
    Gruvbox,
    Nord,
};

enum class BackdropMode
{
    None,
    Auto,
    Fixed,

    // Terminal/text sinks only: no background escape is emitted at all, so
    // whatever the terminal's own background is shows through, the same as
    // ordinary unstyled text. Image output has no such notion to fall back
    // to and renders exactly as None would.
    Transparent,
};

enum class ColorMode
{
    TrueColor,
    Ansi16,
    None,
};

enum class ImageFit
{
    None,
    Width,
    Height,
    Contain,
    Cover,
    Stretch,
};

// How much effort the OUTPUT is worth. Only ever touches render resolution and
// encoding effort -- never which glyph gets picked, never the colours. Meant for
// iterating fast and then committing to a final render, so it deliberately also
// drops the grid size: a preview at full grid but tiny glyphs still costs the
// full selection pass, which is the expensive half.
enum class RenderDetail
{
    None,
    Low,
    Mid,
    High,
};

enum class ImageAlign
{
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight,
};

// Where a text/ANSI video (see Pipeline.cpp's runVideo/playTextVideo) starts
// drawing when played back. TopLeft clears the screen once, before the first
// frame, then homes the cursor before every frame after that too -- so a
// shorter later frame can never leave a longer earlier one's glyphs peeking
// out around it. Inline starts wherever the cursor already was (right under
// whatever command was run) and only ever moves up by exactly the previous
// frame's own line count, never further -- correct for a plain .txt file,
// which can't carry a cursor-home escape of its own to fall back on, but can
// run past the bottom of the window for a grid taller than what's left below
// the prompt. TopLeft is the default specifically because --grid-fit's own
// default (auto) already sizes the grid to fit inside the terminal, so it
// never overflows there either way -- Inline exists for whoever wants the
// video to start below their own scrollback instead of taking over the
// screen.
enum class PlaybackPosition
{
    TopLeft,
    Inline,
};

struct InputOptions
{
    std::string path;

    // Set from the extension, not by a flag: .txt and .ans are printed as-is.
    bool passthrough = false;

    // -1 means "not requested". Set by --preview: decode a video input only up
    // to (and use) this one frame, then run it through the ordinary
    // still-image pipeline instead of the video one -- lets algorithm/render
    // options be tuned against a real frame without waiting for the whole
    // clip. No effect on an input that isn't a video.
    int previewFrame = -1;

    // --play-position: only meaningful when the input turns out to be a saved
    // text/ANSI video (see PlaybackPosition's own note) -- ignored otherwise.
    PlaybackPosition playPosition = PlaybackPosition::TopLeft;
};

struct SourceOptions
{
    bool autoLevels = false;
    float autoLevelsLow = 0.5f;
    float autoLevelsHigh = 99.5f;

    bool levels = false;
    float levelsBlack = 0.f;
    float levelsWhite = 255.f;
    float levelsGamma = 1.f;

    float contrast = 1.f;

    float sharpenAmount = 0.f;
    int sharpenRadius = 1;

    int blurRadius = 0;
};

struct FontOptions
{
    // Empty means the copy bundled in assets/fonts. Width is always half the
    // height, whatever the font is, so a proportional face will look wrong --
    // that is a deliberate trade for never breaking the cell grid.
    std::string path;

    int matchSize = 16;

    // 0 derives it from the requested image height, clamped so the atlas is
    // neither upscaled into blur nor rendered huge and thrown away.
    int renderSize = 0;

    // Thickens the outline before rasterising, so a regular face renders bold
    // with no second font file. Applied to the OUTPUT only -- bolding the
    // matching atlas would change every glyph's ink coverage and quietly skew
    // which glyph gets picked.
    float bold = 0.f;
};

struct CharsetOptions
{
    CharsetName name = CharsetName::Ascii;
    std::string chars;
    std::vector<std::pair<char32_t, char32_t>> ranges;
};

// Which terminal dimension --grid-fit uses when both grid.width and
// grid.height are 0 (see resolveGridSize in Pipeline.cpp). Width and Height
// match the terminal-basis behaviour this project always had; Auto is new --
// the largest grid that fits inside BOTH terminal dimensions at once while
// keeping the source's own aspect, so a portrait source at a wide terminal
// doesn't come out taller than the window (Width alone would size it to the
// full terminal width regardless of how many rows that implies).
enum class GridFitAxis
{
    Auto,
    Width,
    Height,
};

struct GridOptions
{
    // 0 derives from the source aspect; 0 for both falls back to the terminal.
    int width = 0;
    int height = 0;

    // Only consulted in the "0 for both" case above.
    GridFitAxis fitAxis = GridFitAxis::Auto;

    float brightness = 1.f;
    float gamma = 1.f;
    float vibrance = 0.f;

    PaletteName palette = PaletteName::None;
    float paletteStrength = 1.f;

    // Cleanup of the selector's own leftovers rather than an edit to the photo,
    // which is why this one is on when nothing else is.
    float despeckle = 0.1f;
};

struct DitherOptions
{
    DitherName name = DitherName::None;
    int levels = 4;
    bool adaptive = true;
    float flatContrast = 10.f;
    float edgeContrast = 45.f;
};

struct EdgeOptions
{
    EdgeName name = EdgeName::None;
    int subsamples = 4;

    // A mean over the cell's sub-samples, not the strongest one -- see
    // Edges::Options::threshold. Reads lower than the old peak-based default.
    float threshold = 0.15f;
    float coherence = 0.55f;
    float hysteresis = 0.5f;
    bool nms = true;

    // Ink colour override for Scharr's own stamped cells. colorSet false means
    // "leave the selector's colour alone" -- there's no sentinel RGB for that.
    bool colorSet = false;
    RGB color {255, 255, 255};
    float brightness = 1.f;

    // Separate pass, stamped after the gradient detector above. Reads the
    // source's own alpha channel rather than inferring a boundary from luma, so
    // it only does anything on a source that actually has transparency.
    bool alphaOutline = false;
    float alphaThreshold = 0.5f;
    float alphaCoherence = 0.6f;

    // Independent of colorSet/color/brightness above, on purpose -- the two
    // passes commonly want different treatment (see --help edge).
    bool alphaColorSet = false;
    RGB alphaColor {255, 255, 255};
    float alphaBrightness = 1.f;
};

struct AlgoOptions
{
    AlgoName name = AlgoName::Structure;

    bool allowBackground = false;
    float brightnessGamma = 1.f;

    float bitmaskSoftness = 0.f;
    int bitmaskBlurRadius = 1;

    float structureOrientationWeight = 0.25f;
    float structureMassWeight = 1.f;
    float structureToneWeight = 4.f;
    int structureOrientBlocksX = 2;
    int structureOrientBlocksY = 4;
    int structureMassBlocksX = 8;
    int structureMassBlocksY = 16;
    int structureBins = 4;

    // el-3: quality/speed trade, off by default -- see --help algo. Changes
    // nothing at 1.
    int structureGradientStride = 1;

    // el-4: on by default -- measured a ~43% cut on buildDescriptor's share
    // of Structure::generate for a 0.14% cell-level glyph change on a real
    // photo (see optimizations.md item 7), judged worth it. Turn off with
    // --no-algo-structure-fast-atan for the exact std::atan2 + std::fmod path.
    bool structureFastAtan = true;

    // Flat-tile shortcut threshold in pickGlyph -- see Structure.hpp's
    // flatThreshold. 0 would only ever fire on an exactly-uniform tile, which
    // real photos essentially never produce, so this defaults a little above
    // that.
    float structureFlatThreshold = 0.02f;

    // Only Ramp reads this: it wants an ordered string, not a glyph set.
    std::string rampChars = " .:-=+*#%@";

    // Shared by all three algorithms -- see Resample.hpp. Auto (the default)
    // picks a real cubic filter by direction; Box and Triangle force one
    // filter both ways. Box is not a match for the resample's own old,
    // hand-rolled per-pixel average -- measured meaningfully different from
    // it -- so it is offered as "the other stb filter", not "the exact
    // previous behaviour".
    ResampleFilterName resampleFilter = ResampleFilterName::Auto;
};

struct BackdropOptions
{
    // Bare invocation, no preset: your terminal's own background shows through
    // rather than a painted black rectangle. Every preset that cares sets its
    // own mode explicitly, so this default is only ever seen unstyled.
    BackdropMode mode = BackdropMode::Transparent;
    RGB color {0, 0, 0};
    float darken = 0.15f;
    float lumaThreshold = 40.f;
};

struct VideoOptions
{
    // 0 keeps the source's own frame rate. Only ever drops frames to reach a
    // lower target -- a value at or above the source rate is left alone
    // rather than duplicating frames to fake a higher one.
    double fps = 0.0;

    // -1 means unset. Whichever of the time- or frame-based form for a given
    // edge is given wins; there's no dedicated error for setting both on the
    // same edge, same as any other pair of flags that can express the same
    // thing -- last one parsed simply wins.
    double startTime = -1.0;
    double endTime = -1.0;
    int startFrame = -1;
    int endFrame = -1;
};

struct OutputOptions
{
    std::vector<std::string> paths;

    // Terminal unless files were asked for. Writing a file is always explicit.
    bool stdoutEnabled = true;
    bool stdoutExplicit = false;

    // --format: which extension a bare-directory --out should use, so a
    // filename never has to be spelled out just to pick a format. Empty means
    // "use whatever this output kind's own default extension is" -- resolved
    // in Pipeline.cpp's resolveOutputPath, which is also where a leading dot
    // (or its absence) and case get normalised; stored here exactly as typed.
    std::string format;

    ColorMode color = ColorMode::TrueColor;
    bool overwrite = false;

    int imageWidth = 0;
    int imageHeight = 0;
    ImageFit fit = ImageFit::Contain;
    ImageAlign align = ImageAlign::Center;

    // The art is fitted into a box inside the picture, not the whole picture.
    // Shrinking that box is what leaves deliberate empty space, and what gives
    // align something to position within.
    int imageMargin = 0;
    float imageScale = 1.f;

    // Width over height. Only grows the final picture; never crops it and never
    // resamples the art. 0 leaves its shape alone.
    float imageAspect = 0.f;

    // 0 fastest and largest, 9 slowest and smallest. stb's default is 8, which
    // makes PNG encoding the single most expensive stage of a normal run.
    int pngCompression = 8;
};

struct Options
{
    InputOptions input;
    SourceOptions source;
    FontOptions font;
    CharsetOptions charset;
    GridOptions grid;
    DitherOptions dither;
    EdgeOptions edge;
    AlgoOptions algo;
    BackdropOptions backdrop;
    OutputOptions output;
    VideoOptions video;

    std::vector<std::string> presets;
    RenderDetail renderDetail = RenderDetail::None;

    // Where to write the Chrome-trace JSON. Empty means profiling stays off, so
    // the flag's presence is the whole switch.
    std::string profilePath;

    bool helpRequested = false;
    std::string helpTopic;
    bool showVersion = false;
};

};   // namespace App
