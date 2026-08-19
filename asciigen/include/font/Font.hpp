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

    void rasterize(char32_t c, uint8_t* outBuffer, int cellW, int cellH) const;
};
