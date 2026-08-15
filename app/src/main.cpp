#include "bitmap/Ramp.hpp"
#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include "file_management/ImageManager.hpp"
#include "file_management/OutputManager.hpp"
#include "font/Font.hpp"
#include "font/GlyphAtlas.hpp"
#include "output/AnsiRenderer.hpp"
#include "output/Terminal.hpp"
#include <iostream>

// also make it so that non existing files are logged out correctly not just silent errors.
// next up add saving to image

int pixelsPerChar = 150;

int main(int argc, char* argv[])
{
    Font font(SOURCE_DIR "/assets/fonts/Montserrat-Regular.ttf");
    GlyphAtlas matchAtlas(font, Charset::ascii(), 8, 8);
    GlyphAtlas renderAtlas(font, Charset::ascii(), 16, 32);

    Image img = ImageManager::loadImage(SOURCE_DIR "/assets/images/apple.png");

    Charset charset;
    CellBuffer buffer;
    buffer.setSize(2 * img.width / pixelsPerChar, img.height / pixelsPerChar);

    Ramp::generate(img, buffer, charset);

    const std::string rendered =
        AnsiRenderer::render(buffer, charset, {.depth = AnsiRenderer::ColorDepth::TrueColor});

    Terminal::enableAnsi();   // required on Windows or the escapes print as literal text
    Terminal::enableUtf8();   // no-op for pure ASCII, but this is going through real UTF-8 encoding
    // std::cout << rendered;

    if (!OutputManager::saveAns(SOURCE_DIR "/test_render.ans", rendered))
        std::cerr << "failed to save test_render.ans\n";

    return 0;
}
