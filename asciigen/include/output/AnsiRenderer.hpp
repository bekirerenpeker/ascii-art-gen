#pragma once

#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
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
    // Colour escapes emitted per cell. None writes bare glyphs, which is also what
    // you want when piping to a file that should stay readable as plain text.
    ColorDepth depth = ColorDepth::TrueColor;

    // Reserved for clear-screen / cursor-home framing around the output, for
    // redrawing in place during video playback. Not read by render() yet.
    bool screenControls = false;
};

std::string render(const CellBuffer& buffer, const Charset& charset, AnsiRenderOptions opts = {});

};   // namespace AnsiRenderer
