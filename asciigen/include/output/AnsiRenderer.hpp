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

    // Drops the background half of every cell's escape, leaving only the
    // foreground set. Nothing ever writes to the background SGR field then, so
    // the terminal's own background shows through the whole picture, exactly
    // as it does behind ordinary unstyled text. No effect under ColorDepth::None,
    // which already emits no escapes at all.
    bool transparentBackground = false;

    // Reserved for clear-screen / cursor-home framing around the output, for
    // redrawing in place during video playback. Not read by render() yet.
    bool screenControls = false;
};

std::string render(const CellBuffer& buffer, const Charset& charset, AnsiRenderOptions opts = {});

};   // namespace AnsiRenderer
