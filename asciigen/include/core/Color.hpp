#pragma once

#include <algorithm>
#include <cstdint>

// The 16 standard ANSI colors. Values are the SGR *foreground* parameters, so
// emitting a foreground needs no arithmetic at all -- just cast.
//
// Backgrounds are always the foreground code + 10, without exception:
//   Black    fg 30 / bg 40      BrightRed fg 91 / bg 101
//   Default  fg 39 / bg 49
// so one stored value serves both roles.
enum class GlyphColor : uint8_t
{
    Black = 30,
    Red = 31,
    Green = 32,
    Yellow = 33,
    Blue = 34,
    Magenta = 35,
    Cyan = 36,
    White = 37,

    // "Use whatever the terminal already has" -- no defined RGB value.
    Default = 39,

    BrightBlack = 90,
    BrightRed = 91,
    BrightGreen = 92,
    BrightYellow = 93,
    BrightBlue = 94,
    BrightMagenta = 95,
    BrightCyan = 96,
    BrightWhite = 97,
};

constexpr uint8_t foregroundCode(GlyphColor color) { return static_cast<uint8_t>(color); }
constexpr uint8_t backgroundCode(GlyphColor color)
{
    return static_cast<uint8_t>(static_cast<uint8_t>(color) + 10);
}

struct RGB
{
    uint8_t r = 0, g = 0, b = 0;

    GlyphColor toGlyphColor() const;
};

// How a cell's two colours are decided once a glyph has been chosen. Shared by
// every selector: which glyph fits is the algorithm's problem, what colour to
// draw it in is not.
struct CellColorOptions
{
    // Solve a paper colour per cell instead of drawing over a fixed backdrop.
    // Better on flat art, washes photos out towards coloured blocks.
    bool allowBackground = false;

    // Exponent on the foreground. Coverage already tracks brightness, so a value
    // below 1 wins back some of that double-darkening; 1 disables it.
    float brightnessGamma = 1.f;
};

// tile is cellPx RGB samples, mask the chosen glyph's coverage, inkWeight the sum
// of that mask over the cell (mask/255 summed, not a pixel count).
void solveCellColor(
    const RGB* tile, const uint8_t* mask, int cellPx, float inkWeight, CellColorOptions opts,
    RGB& outFg, RGB& outBg
);

// Nearest of the 16 ANSI colors, for terminals without truecolor support.
// Default is never returned -- it has no RGB value to match against.
inline GlyphColor RGB::toGlyphColor() const
{
    float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
    float value = std::max({rf, gf, bf});
    float chroma = value - std::min({rf, gf, bf});
    float saturation = (value <= 0.f) ? 0.f : chroma / value;

    // No real hue to work with -- decide by brightness alone.
    if (value < 0.12f) return GlyphColor::Black;
    if (saturation < 0.15f) {
        if (value > 0.85f) return GlyphColor::BrightWhite;
        if (value > 0.55f) return GlyphColor::White;
        if (value > 0.25f) return GlyphColor::BrightBlack;
        return GlyphColor::Black;
    }

    float hue;
    if (value == rf) hue = 60.f * fmod((gf - bf) / chroma, 6.f);
    else if (value == gf) hue = 60.f * ((bf - rf) / chroma + 2.f);
    else hue = 60.f * ((rf - gf) / chroma + 4.f);
    if (hue < 0) hue += 360.f;

    bool bright = value > 0.6f;

    if (hue < 30 || hue >= 330) return bright ? GlyphColor::BrightRed : GlyphColor::Red;
    if (hue < 90) return bright ? GlyphColor::BrightYellow : GlyphColor::Yellow;
    if (hue < 150) return bright ? GlyphColor::BrightGreen : GlyphColor::Green;
    if (hue < 210) return bright ? GlyphColor::BrightCyan : GlyphColor::Cyan;
    if (hue < 270) return bright ? GlyphColor::BrightBlue : GlyphColor::Blue;
    return bright ? GlyphColor::BrightMagenta : GlyphColor::Magenta;
}
