#pragma once

#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include <string>

// The reverse of AnsiRenderer::render: recovers a CellBuffer (glyph + colours
// per cell) from text that render() produced, colour escapes or not. Exists
// so a saved .txt/.ans -- a still image's, or one video frame's worth, see
// Pipeline.cpp's runVideo/playTextVideo -- can be handed back to the exact
// same ImageRenderer/VideoWriter machinery a live render uses, rather than
// that path needing its own separate way to turn cells into pixels.
namespace AnsiParser {

// One frame's worth of saved text -> a CellBuffer the same shape it was
// before AnsiRenderer::render ever ran. `charset` grows via Charset::append
// as new glyphs are seen -- an existing entry's index is reused, so calling
// this once per frame of a video with the SAME charset keeps every frame's
// glyph indices meaning the same thing throughout, which is what lets one
// GlyphAtlas built from it stay valid across the whole video. Cursor-control
// escapes (\x1b[H, from AnsiRenderer's own screenControls) are recognised and
// skipped, not treated as cell content.
void parse(const std::string& text, Charset& charset, CellBuffer& buffer);

// First pass over a frame's text: just grows `charset` with whichever
// codepoints it uses, without building a CellBuffer at all. For collecting
// every glyph a whole video will ever need before the one GlyphAtlas it
// renders with gets built -- an existing codepoint costs nothing extra to
// see again, so calling this once per frame ahead of a real parse() pass is
// cheap.
void collectGlyphs(const std::string& text, Charset& charset);

}   // namespace AnsiParser
