#pragma once

#include "freetype/freetype.h"
#include <cstdint>
#include <filesystem>

class Font
{
  private:
    static bool s_libInitialized;
    static FT_Library s_library;

    FT_Face m_face;

  public:
    Font(std::filesystem::path filepath);

    void rasterize(char32_t c, uint8_t* outBuffer, int width, int height) const;
};
