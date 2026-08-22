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
    Test,
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

struct InputOptions
{
    std::string path;

    // Set from the extension, not by a flag: .txt and .ans are printed as-is.
    bool passthrough = false;
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

struct GridOptions
{
    // 0 derives from the source aspect; 0 for both falls back to the terminal.
    int width = 0;
    int height = 0;

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

    // Only Ramp reads this: it wants an ordered string, not a glyph set.
    std::string rampChars = " .:-=+*#%@";
};

struct BackdropOptions
{
    BackdropMode mode = BackdropMode::None;
    RGB color {0, 0, 0};
    float darken = 0.15f;
    float lumaThreshold = 40.f;
};

struct OutputOptions
{
    std::vector<std::string> paths;

    // Terminal unless files were asked for. Writing a file is always explicit.
    bool stdoutEnabled = true;
    bool stdoutExplicit = false;

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
