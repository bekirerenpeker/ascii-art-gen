#include "ArgParser.hpp"
#include "Presets.hpp"
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace App;

namespace ArgParser {

namespace {

struct Token
{
    std::string name;    // empty for a positional
    std::string value;
    bool hasValue = false;
};

// "--flag=value" is split here so the main loop only ever sees a name plus an
// optional attached value; "--flag value" pulls the next token instead.
std::vector<Token> tokenize(int argc, char* argv[])
{
    std::vector<Token> out;

    for (int i = 1; i < argc; i++) {
        std::string raw = argv[i];
        Token t;

        if (raw.rfind("--", 0) == 0) {
            const size_t eq = raw.find('=');
            if (eq != std::string::npos) {
                t.name = raw.substr(2, eq - 2);
                t.value = raw.substr(eq + 1);
                t.hasValue = true;
            } else {
                t.name = raw.substr(2);
            }
        } else {
            t.value = raw;
            t.hasValue = true;
        }

        out.push_back(t);
    }

    return out;
}

class Reader
{
  public:
    explicit Reader(std::vector<Token>& tokens) : m_tokens(tokens) {}

    bool failed() const { return m_failed; }
    size_t index() const { return m_index; }
    void advance() { m_index++; }
    bool done() const { return m_index >= m_tokens.size(); }
    const Token& current() const { return m_tokens[m_index]; }

    // Attached value first, otherwise the next token -- but never a token that
    // is itself a flag, so a missing value is reported instead of silently
    // swallowing the option that followed it.
    std::string value(const std::string& flag)
    {
        const Token& t = m_tokens[m_index];
        if (t.hasValue) return t.value;

        if (m_index + 1 < m_tokens.size() && m_tokens[m_index + 1].name.empty()) {
            m_index++;
            return m_tokens[m_index].value;
        }

        fail("--" + flag + " needs a value");
        return {};
    }

    // For flags whose value is optional. The next token is only taken when it
    // is a positional that looks numeric -- otherwise "--font-bold photo.jpg"
    // would eat the input path, and "--font-bold 2" would leave the 2 behind as
    // a second input. Anything ambiguous can still be written with "=".
    bool optionalValue(std::string& out)
    {
        const Token& t = m_tokens[m_index];
        if (t.hasValue) {
            out = t.value;
            return true;
        }

        if (m_index + 1 >= m_tokens.size()) return false;
        if (!m_tokens[m_index + 1].name.empty()) return false;

        const std::string& next = m_tokens[m_index + 1].value;
        if (next.empty()) return false;
        if (!std::isdigit((unsigned char)next[0]) && next[0] != '.' && next[0] != '-'
            && next[0] != '+')
            return false;

        m_index++;
        out = next;
        return true;
    }

    float floatValue(const std::string& flag)
    {
        const std::string v = value(flag);
        if (m_failed) return 0.f;

        try {
            return std::stof(v);
        } catch (...) {
            fail("--" + flag + " expects a number, got \"" + v + "\"");
            return 0.f;
        }
    }

    int intValue(const std::string& flag)
    {
        const std::string v = value(flag);
        if (m_failed) return 0;

        try {
            return std::stoi(v);
        } catch (...) {
            fail("--" + flag + " expects a whole number, got \"" + v + "\"");
            return 0;
        }
    }

    void fail(const std::string& message)
    {
        if (!m_failed) std::cerr << "asciigen: " << message << "\n";
        m_failed = true;
    }

  private:
    std::vector<Token>& m_tokens;
    size_t m_index = 0;
    bool m_failed = false;
};

bool parseSize(const std::string& text, int& w, int& h)
{
    const size_t x = text.find_first_of("xX");
    if (x == std::string::npos) return false;

    try {
        w = std::stoi(text.substr(0, x));
        h = std::stoi(text.substr(x + 1));
    } catch (...) {
        return false;
    }

    return true;
}

bool parseColor(const std::string& text, RGB& out)
{
    std::string hex = text;
    if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
    if (hex.size() != 6) return false;

    try {
        out.r = (uint8_t)std::stoi(hex.substr(0, 2), nullptr, 16);
        out.g = (uint8_t)std::stoi(hex.substr(2, 2), nullptr, 16);
        out.b = (uint8_t)std::stoi(hex.substr(4, 2), nullptr, 16);
    } catch (...) {
        return false;
    }

    return true;
}

// "U+2580-U+259F", or plain hex on either side.
bool parseRange(const std::string& text, std::pair<char32_t, char32_t>& out)
{
    std::string t = text;
    for (size_t i = 0; i + 1 < t.size();) {
        if ((t[i] == 'U' || t[i] == 'u') && t[i + 1] == '+') t.erase(i, 2);
        else i++;
    }

    const size_t dash = t.find('-');
    if (dash == std::string::npos) return false;

    try {
        out.first = (char32_t)std::stoul(t.substr(0, dash), nullptr, 16);
        out.second = (char32_t)std::stoul(t.substr(dash + 1), nullptr, 16);
    } catch (...) {
        return false;
    }

    return out.first <= out.second;
}

// "16:9", "16/9" or a plain ratio like 1.778.
bool parseAspect(const std::string& text, float& out)
{
    const size_t sep = text.find_first_of(":/");

    try {
        if (sep == std::string::npos) out = std::stof(text);
        else {
            const float w = std::stof(text.substr(0, sep));
            const float h = std::stof(text.substr(sep + 1));
            if (h <= 0.f) return false;
            out = w / h;
        }
    } catch (...) {
        return false;
    }

    return out > 0.f;
}

bool boolValue(const std::string& v) { return v != "off" && v != "false" && v != "0"; }

// Plain seconds ("12.5"), "mm:ss(.ms)" ("4:52"), or "hh:mm:ss(.ms)"
// ("1:04:52.3") -- the same shapes ffmpeg's own -ss/-to accept, so a duration
// copied from there works here unchanged.
bool parseTime(const std::string& text, double& outSeconds)
{
    std::vector<std::string> parts;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, ':')) parts.push_back(part);
    if (parts.empty() || parts.size() > 3) return false;

    try {
        double h = 0, m = 0, s = 0;
        if (parts.size() == 1) s = std::stod(parts[0]);
        else if (parts.size() == 2) {
            m = std::stod(parts[0]);
            s = std::stod(parts[1]);
        } else {
            h = std::stod(parts[0]);
            m = std::stod(parts[1]);
            s = std::stod(parts[2]);
        }
        outSeconds = h * 3600.0 + m * 60.0 + s;
    } catch (...) {
        return false;
    }

    return outSeconds >= 0.0;
}

}   // namespace

Result parse(int argc, char* argv[], Options& out)
{
    std::vector<Token> tokens = tokenize(argc, argv);

    if (tokens.empty()) {
        out.helpRequested = true;
        return Result::ExitSuccess;
    }

    // Pass one: presets only. Applying them up front is what makes an explicit
    // flag win regardless of which side of the --preset it was written on.
    {
        Reader r(tokens);
        for (; !r.done(); r.advance()) {
            if (r.current().name != "preset") continue;

            const std::string name = r.value("preset");
            if (r.failed()) return Result::ExitFailure;

            if (!Presets::apply(name, out)) {
                std::cerr << "asciigen: unknown preset \"" << name << "\"\n";
                std::cerr << "try: asciigen --help presets\n";
                return Result::ExitFailure;
            }

            out.presets.push_back(name);
        }
    }

    // Second half of the same pre-pass: render detail lands after presets so it
    // can override the render size a preset chose, and before the main loop so
    // an explicit --font-render-size or --grid-width still wins over both.
    {
        Reader r(tokens);
        for (; !r.done(); r.advance()) {
            if (r.current().name != "render-detail") continue;

            const std::string v = r.value("render-detail");
            if (r.failed()) return Result::ExitFailure;

            // Grid size is deliberately NOT touched. How many cells there are
            // decides which glyphs get chosen and what the picture looks like --
            // that is a styling decision, not a detail level. Changing it here
            // would mean a preview showed different art from the final render,
            // which defeats the point of previewing.
            if (v == "low") {
                out.font.renderSize = 16;
                out.output.pngCompression = 1;
            } else if (v == "mid") {
                out.font.renderSize = 32;
                out.output.pngCompression = 4;
            } else if (v == "high") {
                out.font.renderSize = 64;
                out.output.pngCompression = 9;
            } else {
                std::cerr << "asciigen: unknown --render-detail \"" << v
                          << "\" (low, mid, high)\n";
                return Result::ExitFailure;
            }
        }
    }

    Reader r(tokens);

    for (; !r.done(); r.advance()) {
        const Token& t = r.current();
        const std::string& n = t.name;

        if (n.empty()) {
            if (!out.input.path.empty()) {
                r.fail("more than one input given (\"" + out.input.path + "\" and \"" + t.value + "\")");
                break;
            }
            out.input.path = t.value;
            continue;
        }

        if (n == "preset") {
            r.value("preset");   // handled above; just consume the value
            continue;
        }

        if (n == "help" || n == "h") {
            out.helpRequested = true;
            if (t.hasValue) {
                out.helpTopic = t.value;
            } else if (r.index() + 1 < tokens.size() && tokens[r.index() + 1].name.empty()) {
                r.advance();
                out.helpTopic = tokens[r.index()].value;
            }
            continue;
        }
        if (n == "profile") { out.profilePath = r.value(n); continue; }
        if (n == "version") {
            out.showVersion = true;
            continue;
        }

        // --- input ---
        if (n == "input") { out.input.path = r.value(n); continue; }
        if (n == "preview") {
            out.input.previewFrame = 0;

            std::string v;
            if (r.optionalValue(v)) {
                try {
                    out.input.previewFrame = std::stoi(v);
                } catch (...) {
                    r.fail("--preview expects a frame number, got \"" + v + "\"");
                }
            }
            continue;
        }
        if (n == "play-position") {
            const std::string v = r.value(n);
            if (v == "top-left" || v == "top") out.input.playPosition = PlaybackPosition::TopLeft;
            else if (v == "inline" || v == "here") out.input.playPosition = PlaybackPosition::Inline;
            else r.fail("unknown --play-position \"" + v + "\" (top-left, inline)");
            continue;
        }

        // --- video ---
        if (n == "fps") { out.video.fps = r.floatValue(n); continue; }
        if (n == "start-time") {
            const std::string v = r.value(n);
            if (!parseTime(v, out.video.startTime))
                r.fail("bad --start-time \"" + v + "\" (want seconds, mm:ss, or hh:mm:ss)");
            continue;
        }
        if (n == "end-time") {
            const std::string v = r.value(n);
            if (!parseTime(v, out.video.endTime))
                r.fail("bad --end-time \"" + v + "\" (want seconds, mm:ss, or hh:mm:ss)");
            continue;
        }
        if (n == "start-frame") { out.video.startFrame = r.intValue(n); continue; }
        if (n == "end-frame") { out.video.endFrame = r.intValue(n); continue; }

        // --- source ---
        if (n == "source-auto-levels") {
            out.source.autoLevels = true;

            std::string v;
            if (r.optionalValue(v)) {
                std::istringstream ss(v);
                char comma = 0;
                ss >> out.source.autoLevelsLow >> comma >> out.source.autoLevelsHigh;
            }
            continue;
        }
        if (n == "no-source-auto-levels") { out.source.autoLevels = false; continue; }
        if (n == "source-levels") {
            std::istringstream ss(r.value(n));
            char c = 0;
            out.source.levels = true;
            ss >> out.source.levelsBlack >> c >> out.source.levelsWhite;
            if (ss >> c) ss >> out.source.levelsGamma;
            continue;
        }
        if (n == "source-contrast") { out.source.contrast = r.floatValue(n); continue; }
        if (n == "source-sharpen") {
            std::istringstream ss(r.value(n));
            char c = 0;
            ss >> out.source.sharpenAmount;
            if (ss >> c) ss >> out.source.sharpenRadius;
            continue;
        }
        if (n == "source-blur") { out.source.blurRadius = r.intValue(n); continue; }
        if (n == "source-invert") { out.source.invert = true; continue; }
        if (n == "no-source-invert") { out.source.invert = false; continue; }
        if (n == "source-invert-brightness") { out.source.invertBrightness = true; continue; }
        if (n == "no-source-invert-brightness") { out.source.invertBrightness = false; continue; }
        if (n == "source-invert-saturation") { out.source.invertSaturation = true; continue; }
        if (n == "no-source-invert-saturation") { out.source.invertSaturation = false; continue; }

        // --- font ---
        if (n == "font-path") { out.font.path = r.value(n); continue; }
        if (n == "font-match-size") { out.font.matchSize = r.intValue(n); continue; }
        if (n == "font-render-size") { out.font.renderSize = r.intValue(n); continue; }
        if (n == "font-bold") {
            std::string v;
            out.font.bold = 1.f;
            if (r.optionalValue(v)) {
                try {
                    out.font.bold = std::stof(v);
                } catch (...) {
                    r.fail("--font-bold expects a number, got \"" + v + "\"");
                }
            }
            continue;
        }

        // --- charset ---
        if (n == "charset") {
            const std::string v = r.value(n);
            if (v == "ascii") out.charset.name = CharsetName::Ascii;
            else if (v == "blocks") out.charset.name = CharsetName::Blocks;
            else if (v == "braille") out.charset.name = CharsetName::Braille;
            else if (v == "ramp") out.charset.name = CharsetName::Ramp;
            else r.fail("unknown charset \"" + v + "\" (ascii, blocks, braille, ramp)");
            continue;
        }
        if (n == "charset-chars") {
            out.charset.chars = r.value(n);
            out.charset.name = CharsetName::Custom;
            continue;
        }
        if (n == "charset-range") {
            std::pair<char32_t, char32_t> range;
            const std::string v = r.value(n);
            if (!parseRange(v, range)) r.fail("bad --charset-range \"" + v + "\" (want U+2580-U+259F)");
            else out.charset.ranges.push_back(range);
            continue;
        }

        // --- grid ---
        if (n == "grid-width") { out.grid.width = r.intValue(n); continue; }
        if (n == "grid-height") { out.grid.height = r.intValue(n); continue; }
        if (n == "grid-fit") {
            const std::string v = r.value(n);
            if (v == "auto") out.grid.fitAxis = GridFitAxis::Auto;
            else if (v == "width") out.grid.fitAxis = GridFitAxis::Width;
            else if (v == "height") out.grid.fitAxis = GridFitAxis::Height;
            else r.fail("unknown --grid-fit \"" + v + "\" (auto, width, height)");
            continue;
        }
        if (n == "grid-brightness") { out.grid.brightness = r.floatValue(n); continue; }
        if (n == "grid-gamma") { out.grid.gamma = r.floatValue(n); continue; }
        if (n == "grid-vibrance") { out.grid.vibrance = r.floatValue(n); continue; }
        if (n == "grid-despeckle") { out.grid.despeckle = r.floatValue(n); continue; }
        if (n == "no-grid-despeckle") { out.grid.despeckle = 0.f; continue; }
        if (n == "grid-palette") {
            const std::string v = r.value(n);
            if (v == "none") out.grid.palette = PaletteName::None;
            else if (v == "gruvbox") out.grid.palette = PaletteName::Gruvbox;
            else if (v == "nord") out.grid.palette = PaletteName::Nord;
            else r.fail("unknown palette \"" + v + "\" (none, gruvbox, nord)");
            continue;
        }
        if (n == "grid-palette-strength") { out.grid.paletteStrength = r.floatValue(n); continue; }

        // --- dither ---
        if (n == "dither") {
            const std::string v = r.value(n);
            if (v == "none") out.dither.name = DitherName::None;
            else if (v == "bayer4") out.dither.name = DitherName::Bayer4;
            else r.fail("unknown dither \"" + v + "\" (none, bayer4)");
            continue;
        }
        if (n == "dither-levels") { out.dither.levels = r.intValue(n); continue; }
        if (n == "dither-adaptive") { out.dither.adaptive = boolValue(r.value(n)); continue; }
        if (n == "dither-flat-contrast") { out.dither.flatContrast = r.floatValue(n); continue; }
        if (n == "dither-edge-contrast") { out.dither.edgeContrast = r.floatValue(n); continue; }

        // --- edges ---
        if (n == "edge") {
            const std::string v = r.value(n);
            if (v == "none") out.edge.name = EdgeName::None;
            else if (v == "scharr") out.edge.name = EdgeName::Scharr;
            else r.fail("unknown edge detector \"" + v + "\" (none, scharr)");
            continue;
        }
        if (n == "edge-threshold") { out.edge.threshold = r.floatValue(n); continue; }
        if (n == "edge-coherence") { out.edge.coherence = r.floatValue(n); continue; }
        if (n == "edge-subsamples") { out.edge.subsamples = r.intValue(n); continue; }
        if (n == "edge-hysteresis") { out.edge.hysteresis = r.floatValue(n); continue; }
        if (n == "edge-nms") { out.edge.nms = true; continue; }
        if (n == "no-edge-nms") { out.edge.nms = false; continue; }
        if (n == "edge-alpha") { out.edge.alphaOutline = true; continue; }
        if (n == "no-edge-alpha") { out.edge.alphaOutline = false; continue; }
        if (n == "edge-alpha-threshold") { out.edge.alphaThreshold = r.floatValue(n); continue; }
        if (n == "edge-alpha-coherence") { out.edge.alphaCoherence = r.floatValue(n); continue; }
        if (n == "edge-color") {
            const std::string v = r.value(n);
            if (!parseColor(v, out.edge.color)) r.fail("bad --edge-color \"" + v + "\" (#RRGGBB)");
            out.edge.colorSet = true;
            continue;
        }
        if (n == "no-edge-color") { out.edge.colorSet = false; continue; }
        if (n == "edge-brightness") { out.edge.brightness = r.floatValue(n); continue; }
        if (n == "edge-alpha-color") {
            const std::string v = r.value(n);
            if (!parseColor(v, out.edge.alphaColor))
                r.fail("bad --edge-alpha-color \"" + v + "\" (#RRGGBB)");
            out.edge.alphaColorSet = true;
            continue;
        }
        if (n == "no-edge-alpha-color") { out.edge.alphaColorSet = false; continue; }
        if (n == "edge-alpha-brightness") { out.edge.alphaBrightness = r.floatValue(n); continue; }
        if (n == "edge-color-both") {
            const std::string v = r.value(n);
            if (!parseColor(v, out.edge.color))
                r.fail("bad --edge-color-both \"" + v + "\" (#RRGGBB)");
            out.edge.colorSet = true;
            out.edge.alphaColor = out.edge.color;
            out.edge.alphaColorSet = true;
            continue;
        }
        if (n == "edge-brightness-both") {
            out.edge.brightness = out.edge.alphaBrightness = r.floatValue(n);
            continue;
        }

        // --- algorithm ---
        if (n == "algo") {
            const std::string v = r.value(n);
            if (v == "ramp") out.algo.name = AlgoName::Ramp;
            else if (v == "bitmask") out.algo.name = AlgoName::Bitmask;
            else if (v == "structure") out.algo.name = AlgoName::Structure;
            else r.fail("unknown algorithm \"" + v + "\" (ramp, bitmask, structure)");
            continue;
        }
        if (n == "algo-allow-background") {
            out.algo.allowBackground = true;

            std::string v;
            if (r.optionalValue(v)) out.algo.allowBackground = boolValue(v);
            continue;
        }
        if (n == "no-algo-allow-background") { out.algo.allowBackground = false; continue; }
        if (n == "algo-brightness-gamma") { out.algo.brightnessGamma = r.floatValue(n); continue; }
        if (n == "algo-ramp-chars") { out.algo.rampChars = r.value(n); continue; }
        if (n == "algo-bitmask-softness") { out.algo.bitmaskSoftness = r.floatValue(n); continue; }
        if (n == "algo-bitmask-blur-radius") { out.algo.bitmaskBlurRadius = r.intValue(n); continue; }
        if (n == "algo-structure-orientation-weight") {
            out.algo.structureOrientationWeight = r.floatValue(n);
            continue;
        }
        if (n == "algo-structure-mass-weight") { out.algo.structureMassWeight = r.floatValue(n); continue; }
        if (n == "algo-structure-tone-weight") { out.algo.structureToneWeight = r.floatValue(n); continue; }
        if (n == "algo-structure-bins") { out.algo.structureBins = r.intValue(n); continue; }
        if (n == "algo-structure-orient-blocks") {
            const std::string v = r.value(n);
            if (!parseSize(v, out.algo.structureOrientBlocksX, out.algo.structureOrientBlocksY))
                r.fail("bad --algo-structure-orient-blocks \"" + v + "\" (want WxH)");
            continue;
        }
        if (n == "algo-structure-mass-blocks") {
            const std::string v = r.value(n);
            if (!parseSize(v, out.algo.structureMassBlocksX, out.algo.structureMassBlocksY))
                r.fail("bad --algo-structure-mass-blocks \"" + v + "\" (want WxH)");
            continue;
        }
        if (n == "algo-structure-gradient-stride") {
            out.algo.structureGradientStride = r.intValue(n);
            continue;
        }
        if (n == "algo-structure-fast-atan") { out.algo.structureFastAtan = true; continue; }
        if (n == "no-algo-structure-fast-atan") { out.algo.structureFastAtan = false; continue; }
        if (n == "algo-structure-flat-threshold") {
            out.algo.structureFlatThreshold = r.floatValue(n);
            continue;
        }
        if (n == "resample-filter") {
            const std::string v = r.value(n);
            if (v == "auto") out.algo.resampleFilter = ResampleFilterName::Auto;
            else if (v == "box") out.algo.resampleFilter = ResampleFilterName::Box;
            else if (v == "triangle") out.algo.resampleFilter = ResampleFilterName::Triangle;
            else r.fail("unknown --resample-filter \"" + v + "\" (auto, box, triangle)");
            continue;
        }

        // --- backdrop ---
        if (n == "backdrop") {
            const std::string v = r.value(n);
            if (v == "none") out.backdrop.mode = BackdropMode::None;
            else if (v == "auto") out.backdrop.mode = BackdropMode::Auto;
            else if (v == "transparent") out.backdrop.mode = BackdropMode::Transparent;
            else if (parseColor(v, out.backdrop.color)) out.backdrop.mode = BackdropMode::Fixed;
            else r.fail("bad --backdrop \"" + v + "\" (none, auto, transparent, or #RRGGBB)");
            continue;
        }
        if (n == "backdrop-darken") { out.backdrop.darken = r.floatValue(n); continue; }
        if (n == "backdrop-luma-threshold") { out.backdrop.lumaThreshold = r.floatValue(n); continue; }

        // --- output ---
        if (n == "out") { out.output.paths.push_back(r.value(n)); continue; }
        if (n == "format") { out.output.format = r.value(n); continue; }
        if (n == "stdout") {
            out.output.stdoutEnabled = true;
            out.output.stdoutExplicit = true;
            continue;
        }
        if (n == "no-stdout") {
            out.output.stdoutEnabled = false;
            out.output.stdoutExplicit = true;
            continue;
        }
        if (n == "overwrite") { out.output.overwrite = true; continue; }
        if (n == "color") {
            const std::string v = r.value(n);
            if (v == "truecolor") out.output.color = ColorMode::TrueColor;
            else if (v == "ansi16") out.output.color = ColorMode::Ansi16;
            else if (v == "none") out.output.color = ColorMode::None;
            else r.fail("unknown colour mode \"" + v + "\" (truecolor, ansi16, none)");
            continue;
        }
        if (n == "image-width") { out.output.imageWidth = r.intValue(n); continue; }
        if (n == "image-height") { out.output.imageHeight = r.intValue(n); continue; }
        if (n == "image-aspect") {
            const std::string v = r.value(n);
            if (!parseAspect(v, out.output.imageAspect))
                r.fail("bad --image-aspect \"" + v + "\" (want 16:9 or 1.778)");
            continue;
        }
        if (n == "png-compression") { out.output.pngCompression = r.intValue(n); continue; }
        if (n == "render-detail") {
            const std::string v = r.value(n);
            if (v == "low") out.renderDetail = RenderDetail::Low;
            else if (v == "mid") out.renderDetail = RenderDetail::Mid;
            else if (v == "high") out.renderDetail = RenderDetail::High;
            else r.fail("unknown --render-detail \"" + v + "\" (low, mid, high)");
            continue;
        }
        if (n == "image-margin") { out.output.imageMargin = r.intValue(n); continue; }
        if (n == "image-scale") { out.output.imageScale = r.floatValue(n); continue; }
        if (n == "image-fit") {
            const std::string v = r.value(n);
            if (v == "none") out.output.fit = ImageFit::None;
            else if (v == "width") out.output.fit = ImageFit::Width;
            else if (v == "height") out.output.fit = ImageFit::Height;
            else if (v == "contain") out.output.fit = ImageFit::Contain;
            else if (v == "cover") out.output.fit = ImageFit::Cover;
            else if (v == "stretch") out.output.fit = ImageFit::Stretch;
            else r.fail("unknown --image-fit \"" + v + "\" (none, width, height, contain, cover, stretch)");
            continue;
        }
        if (n == "image-align") {
            const std::string v = r.value(n);
            if (v == "top-left") out.output.align = ImageAlign::TopLeft;
            else if (v == "top") out.output.align = ImageAlign::Top;
            else if (v == "top-right") out.output.align = ImageAlign::TopRight;
            else if (v == "left") out.output.align = ImageAlign::Left;
            else if (v == "center") out.output.align = ImageAlign::Center;
            else if (v == "right") out.output.align = ImageAlign::Right;
            else if (v == "bottom-left") out.output.align = ImageAlign::BottomLeft;
            else if (v == "bottom") out.output.align = ImageAlign::Bottom;
            else if (v == "bottom-right") out.output.align = ImageAlign::BottomRight;
            else r.fail("unknown --image-align \"" + v + "\"");
            continue;
        }

        r.fail("unknown option \"--" + n + "\" (try: asciigen --help)");
        break;
    }

    if (r.failed()) return Result::ExitFailure;
    if (out.helpRequested || out.showVersion) return Result::ExitSuccess;

    // Writing a file is explicit; printing is what happens when nothing else was
    // asked for. Both only when --stdout says so.
    if (!out.output.paths.empty() && !out.output.stdoutExplicit) out.output.stdoutEnabled = false;

    if (out.input.path.empty()) {
        std::cerr << "asciigen: no input given (try: asciigen photo.jpg)\n";
        return Result::ExitFailure;
    }

    return Result::Ok;
}

}   // namespace ArgParser
