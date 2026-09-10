#pragma once

#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include <functional>
#include <set>
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
//
// `isSupported`, when given, is checked before a codepoint is added to
// `charset` -- returning false substitutes a plain space instead of adding
// the real one, so a font that can't render something this file used never
// produces a tofu/notdef box. An already-unsupported codepoint gets
// re-checked (and re-substituted) every time it recurs rather than being
// remembered -- Font::hasGlyph is a cheap lookup, so this costs nothing
// worth avoiding, and it means charset never ends up containing an
// unsupported codepoint under any path.
// Substituting up front like this means `charset` never has to be rebuilt
// and reindexed after the fact the way Pipeline.cpp's own
// filterUnsupportedGlyphs does for a live render's charset: every index
// parse() ever hands out stays valid for the whole file, which is what lets
// Pipeline.cpp's renderTextToMedia parse a video one frame at a time instead
// of scanning it all first just to know which glyphs are safe. Dropped
// codepoints are recorded into `dropped` (if given) so the caller can warn
// once, after the fact, instead of needing to know them all up front.
void parse(
    const std::string& text, Charset& charset, CellBuffer& buffer,
    const std::function<bool(char32_t)>& isSupported = {}, std::set<char32_t>* dropped = nullptr
);

}   // namespace AnsiParser
