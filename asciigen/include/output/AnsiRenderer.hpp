#pragma once

#include "bitmap/CellBuffer.hpp"
#include "bitmap/Charset.hpp"
#include <string>

namespace AnsiRenderer {

enum class ColorDepth
{
    TrueColor,
    Ansi16,
    None,
};

struct AnsiRenderOptions
{
    ColorDepth depth = ColorDepth::TrueColor;
    bool screenControls = false;
};

std::string render(const CellBuffer& buffer, const Charset& charset, AnsiRenderOptions opts = {});

};   // namespace AnsiRenderer
