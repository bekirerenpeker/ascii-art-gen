#pragma once

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

// Nearest of the 16 ANSI colors, for terminals without truecolor support.
// Default is never returned -- it has no RGB value to match against.
inline GlyphColor RGB::toGlyphColor() const
{
    struct Entry
    {
        uint8_t r, g, b;
        GlyphColor color;
    };

    // xterm's default palette. Terminals let users retheme these freely, so
    // this is a best-effort match rather than exactly what ends up on screen.
    static constexpr Entry palette[] = {
        {  0,   0,   0,         GlyphColor::Black},
        {205,   0,   0,           GlyphColor::Red},
        {  0, 205,   0,         GlyphColor::Green},
        {205, 205,   0,        GlyphColor::Yellow},
        {  0,   0, 238,          GlyphColor::Blue},
        {205,   0, 205,       GlyphColor::Magenta},
        {  0, 205, 205,          GlyphColor::Cyan},
        {229, 229, 229,         GlyphColor::White},
        {127, 127, 127,   GlyphColor::BrightBlack},
        {255,   0,   0,     GlyphColor::BrightRed},
        {  0, 255,   0,   GlyphColor::BrightGreen},
        {255, 255,   0,  GlyphColor::BrightYellow},
        { 92,  92, 255,    GlyphColor::BrightBlue},
        {255,   0, 255, GlyphColor::BrightMagenta},
        {  0, 255, 255,    GlyphColor::BrightCyan},
        {255, 255, 255,   GlyphColor::BrightWhite},
    };

    GlyphColor best = GlyphColor::Black;
    int32_t bestDistance = -1;

    for (const Entry& entry : palette) {
        // "Redmean" weighted distance: markedly closer to human perception
        // than plain RGB Euclidean, while staying integer-only.
        const int32_t rmean = (static_cast<int32_t>(r) + entry.r) / 2;
        const int32_t dr = static_cast<int32_t>(r) - entry.r;
        const int32_t dg = static_cast<int32_t>(g) - entry.g;
        const int32_t db = static_cast<int32_t>(b) - entry.b;

        const int32_t distance =
            (((512 + rmean) * dr * dr) >> 8) + 4 * dg * dg + (((767 - rmean) * db * db) >> 8);

        if (bestDistance < 0 || distance < bestDistance) {
            bestDistance = distance;
            best = entry.color;
        }
    }

    return best;
}
