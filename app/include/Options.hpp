#pragma once

#include "core/Color.hpp"
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
    float threshold = 0.3f;
    float coherence = 0.55f;
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

    bool helpRequested = false;
    std::string helpTopic;
    bool showVersion = false;
};

};   // namespace App
