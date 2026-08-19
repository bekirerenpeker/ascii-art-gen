#include "core/Charset.hpp"

int Charset::indexOf(char32_t codepoint) const
{
    const size_t pos = m_glyphs.find(codepoint);
    return pos == std::u32string::npos ? -1 : static_cast<int>(pos);
}

const Charset& Charset::blocks()
{
    // Space plus the whole Block Elements range (U+2580-U+259F): shading
    // levels, halves, eighths along both axes, and every quadrant combination.
    static const Charset instance = [] {
        std::u32string glyphs;
        glyphs.reserve(0x21);
        glyphs += U' ';
        for (char32_t cp = 0x2580; cp <= 0x259F; ++cp) glyphs += cp;
        return Charset(std::move(glyphs));
    }();

    return instance;
}

const Charset& Charset::ascii()
{
    // Printable ASCII, space through tilde (32-126 both included)
    static const Charset instance = [] {
        std::u32string glyphs;
        glyphs.reserve(0x7F - 0x20);
        for (char32_t cp = 0x20; cp < 0x7F; ++cp) glyphs += cp;
        return Charset(std::move(glyphs));
    }();

    return instance;
}
