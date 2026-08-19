#include "bitmap/Bitmask.hpp"
#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include "dithering/Dithering.hpp"
#include "edges/Edges.hpp"
#include "file_management/ImageManager.hpp"
#include "file_management/OutputManager.hpp"
#include "font/Font.hpp"
#include "font/GlyphAtlas.hpp"
#include "output/AnsiRenderer.hpp"
#include "output/Terminal.hpp"
#include <iostream>

int pixelsPerChar = 30;

int main(int argc, char* argv[])
{

    Image img = ImageManager::loadImage(SOURCE_DIR "/assets/images/porsche-911-blue.png");

    Dithering::options = {.enabled = true, .algorithm = Dithering::Algorithm::Bayer4, .levels = 4};
    Edges::options = {
        .enabled = false,
        .algorithm = Edges::Algorithm::Scharr,
        .subsamples = 4,
        .threshold = 0.3f,
        .coherence = 0.55f
    };

    Charset charset = Charset::ascii();
    CellBuffer buffer;
    buffer.setSize(2 * img.width / pixelsPerChar, img.height / pixelsPerChar);

    // Space Mono has none of U+2580-U+259F; Cascadia Mono has all 32.
    Font font("C:/Windows/Fonts/CascadiaMono.ttf");
    GlyphAtlas matchAtlas(font, charset, 8, 16);

    Bitmask::generate(img, buffer, matchAtlas, {.allowBackground = false});

    // Post-pass, not a step inside the selector: it only reads the image and
    // overwrites glyph indices, so it works the same after any algorithm.
    Edges::apply(img, buffer, charset);

    const std::string rendered =
        AnsiRenderer::render(buffer, charset, {.depth = AnsiRenderer::ColorDepth::TrueColor});

    Terminal::enableAnsi();   // required on Windows or the escapes print as literal text
    Terminal::enableUtf8();   // no-op for pure ASCII, but this is going through real UTF-8 encoding
    std::cout << rendered;

    matchAtlas.debugDumpToImage(SOURCE_DIR "/output/matchAtlas.png");
    OutputManager::saveAns(SOURCE_DIR "/output/test_render.ans", rendered);

    return 0;
}
