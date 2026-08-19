#pragma once

#include "core/CellBuffer.hpp"
#include "core/Color.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"
#include <filesystem>

namespace ImageRenderer {

struct ImageRenderOptions
{
    // Output size in pixels. Left at 0, the image comes out exactly as large as
    // the cell grid makes it -- which is what the quality metric compares
    // against. Anything else centres that grid and pads, or crops, to fit.
    int width = 0;
    int height = 0;

    // Painted behind everything. Set this to whatever the cells' own background
    // was filled with, or the padding will not match the art it surrounds.
    RGB backgroundColor {0, 0, 0};
};

Image render(const CellBuffer& buffer, const GlyphAtlas& atlas, ImageRenderOptions opts = {});

bool save(
    const std::filesystem::path& filepath, const CellBuffer& buffer, const GlyphAtlas& atlas,
    ImageRenderOptions opts = {}
);

};   // namespace ImageRenderer
