#pragma once

#include "freetype/freetype.h"
#include <cstdint>
#include <filesystem>

class Font
{
  private:
    // Glyphs are rendered this many times larger than the target cell so the
    // downscale has detail to work with.
    static constexpr int kSupersample = 4;

    static bool s_libInitialized;
    static FT_Library s_library;

    FT_Face m_face;

  public:
    Font(std::filesystem::path filepath);

    // boldness thickens the outline before it is rasterised, so a regular face
    // can render bold without a second font file. 0 is the face as drawn; 1 is
    // about a normal bold weight.
    void rasterize(char32_t c, uint8_t* outBuffer, int cellW, int cellH, float boldness = 0.f) const;

    // True only if this codepoint has a real mapping in the font's own
    // charmap. Matters because FT_Load_Char (what rasterize calls) does NOT
    // fail for a codepoint the font doesn't have -- by TrueType/OpenType
    // convention it silently substitutes glyph index 0, the face's own
    // ".notdef" glyph, and reports success regardless. A charset built from a
    // fixed Unicode range (Charset::blocks/braille) has no way to know ahead
    // of time whether every one of those codepoints is actually in whichever
    // font gets picked -- this is what lets a caller filter the ones that
    // aren't out before the algorithm can ever select one and render
    // whatever .notdef happens to look like (often a hollow box, sometimes a
    // boxed "?") in the middle of otherwise-correct output.
    bool hasGlyph(char32_t c) const;
};
