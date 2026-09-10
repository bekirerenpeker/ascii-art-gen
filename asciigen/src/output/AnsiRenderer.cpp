#include "core/Profiler.hpp"
#include "output/AnsiRenderer.hpp"

namespace AnsiRenderer {

#define ESC "\x1b"

static inline void appendU8(std::string& out, uint8_t v)
{
    if (v < 10) {
        out += char('0' + v);
        return;
    }
    if (v < 100) {
        out += char('0' + v / 10);
        out += char('0' + v % 10);
        return;
    }
    out += char('0' + v / 100);
    out += char('0' + (v / 10) % 10);
    out += char('0' + v % 10);
}

static inline void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

std::string render(const CellBuffer& buffer, const Charset& charset, AnsiRenderOptions opts)
{
    ASCIIGEN_PROFILE("AnsiRenderer::render", "output");

    std::string out;
    out.reserve(buffer.width() * buffer.height() * 42 + buffer.height() * 8);

    // Cursor-to-home, not a full clear -- every frame repaints every cell at
    // the same fixed grid size (one CellBuffer size for the whole run), so
    // overwriting in place from the top-left can never leave a previous
    // frame's glyphs peeking out around the edges the way a partial redraw
    // could. Used for video playback (see Pipeline.cpp's runVideo/
    // playTextVideo): embedding this in the saved frame means playing one
    // back is just printing each string in order, no redraw bookkeeping
    // needed at the read side at all.
    if (opts.screenControls) out += ESC "[H";

    // Buffers are bottom-up; terminals draw top-down. Reversing here is this
    // function's problem, not the pipeline's.
    for (int y = buffer.height() - 1; y >= 0; y--) {
        for (int x = 0; x < buffer.width(); x++) {
            const Cell& cell = buffer.getAt(x, y);

            switch (opts.depth) {
            case ColorDepth::TrueColor:
                out += ESC "[38;2;";
                appendU8(out, cell.fg.r), out += ';';
                appendU8(out, cell.fg.g), out += ';';
                appendU8(out, cell.fg.b);
                if (!opts.transparentBackground) {
                    out += ";48;2;";
                    appendU8(out, cell.bg.r), out += ';';
                    appendU8(out, cell.bg.g), out += ';';
                    appendU8(out, cell.bg.b);
                }
                out += 'm';
                break;
            case ColorDepth::Ansi16:
                out += ESC "[";
                appendU8(out, (int)cell.fg.toGlyphColor());
                if (!opts.transparentBackground) {
                    out += ';';
                    appendU8(out, (int)cell.bg.toGlyphColor() + 10);
                }
                out += 'm';
                break;
            case ColorDepth::None: break;
            }

            appendUtf8(out, charset.codepointAt(cell.glyphIndex));
        }

        // Nothing was set, so there is nothing to reset -- and emitting one
        // anyway leaves escape bytes in output that is meant to be plain text.
        if (opts.depth != ColorDepth::None) out += ESC "[0m";

        out += '\n';
    }

    return out;
}

}   // namespace AnsiRenderer
