#include "bitmap/CellBuffer.hpp"
#include "bitmap/Charset.hpp"
#include "file_management/OutputManager.hpp"
#include "output/AnsiRenderer.hpp"
#include "output/Terminal.hpp"

#include <iostream>

int main(int argc, char* argv[])
{
    Terminal::enableAnsi();   // required on Windows or the escapes print as literal text
    Terminal::
        enableUtf8();   // no-op for pure ASCII, but this is now going through real UTF-8 encoding

    const Charset& charset = Charset::ascii();

    CellBuffer buffer;
    buffer.width = 32;
    buffer.height = 16;
    buffer.cells.resize(buffer.width * buffer.height);

    for (int y = 0; y < buffer.height; y++) {
        for (int x = 0; x < buffer.width; x++) {
            Cell& cell = buffer.getAt(x, y);
            cell.fg = RGB {
                static_cast<uint8_t>(x * 255 / (buffer.width - 1)),
                static_cast<uint8_t>(y * 255 / (buffer.height - 1)),
                128,
            };
            cell.bg = RGB {0, 0, 0};
            // Sweep through the whole charset diagonally so every glyph in it
            // gets exercised at least once, instead of always index 0 (space).
            cell.glyphIndex = static_cast<uint16_t>((x + y) % charset.size());
        }
    }

    const std::string rendered = AnsiRenderer::render(buffer, charset);

    std::cout << rendered;

    if (!OutputManager::saveAns(SOURCE_DIR "/test_render.ans", rendered))
        std::cerr << "failed to save test_render.ans\n";

    return 0;
}
