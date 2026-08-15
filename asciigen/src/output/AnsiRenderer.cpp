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
    std::string out;
    out.reserve(buffer.width() * buffer.height() * 42 + buffer.height() * 8);

    for (int y = buffer.height() - 1; y >= 0; y--) {
        for (int x = 0; x < buffer.width(); x++) {
            const Cell& cell = buffer.getAt(x, y);

            switch (opts.depth) {
            case ColorDepth::TrueColor:
                out += ESC "[38;2;";
                appendU8(out, cell.fg.r), out += ";";
                appendU8(out, cell.fg.g), out += ";";
                appendU8(out, cell.fg.b);
                out += ";48;2;";
                appendU8(out, cell.bg.r), out += ";";
                appendU8(out, cell.bg.g), out += ";";
                appendU8(out, cell.bg.b);
                out += "m";
                break;
            case ColorDepth::Ansi16:
                out += ESC "[";
                appendU8(out, (int)cell.fg.toGlyphColor());
                out += ";";
                appendU8(out, (int)cell.bg.toGlyphColor());
                out += "m";
                break;
            case ColorDepth::None: break;
            }

            appendUtf8(out, charset.codepointAt(cell.glyphIndex));
        }

        out += ESC "[0m";
        out += '\n';
    }

    return out;
}

}   // namespace AnsiRenderer
