#include "bitmap/Bitmask.hpp"
#include "bitmap/Structure.hpp"
#include "bitmap/Ramp.hpp"
#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include "dithering/Dithering.hpp"
#include "edges/Edges.hpp"
#include "file_management/ImageManager.hpp"
#include "filters/Despeckle.hpp"
#include "file_management/OutputManager.hpp"
#include "font/Font.hpp"
#include "font/GlyphAtlas.hpp"
#include "output/AnsiRenderer.hpp"
#include "output/Terminal.hpp"
#include "quality/Quality.hpp"
#include <iomanip>
#include <iostream>

int pixelsPerChar = 30;

int main(int argc, char* argv[])
{
    Image img = ImageManager::loadImage(SOURCE_DIR "/assets/images/porsche-911-blue.png");

    Dithering::options = {
        .enabled = true,
        .algorithm = Dithering::Algorithm::Bayer4,
        .levels = 4,
        .adaptive = true,
        .flatContrast = 10.f,
        .edgeContrast = 45.f
    };
    Edges::options = {
        .enabled = false,
        .algorithm = Edges::Algorithm::Scharr,
        .subsamples = 4,
        .threshold = 0.6f,
        .coherence = 0.55
    };

    Charset charset = Charset::ascii();
    CellBuffer buffer;
    buffer.setSize(2 * img.width / pixelsPerChar, img.height / pixelsPerChar);

    Font font("C:/Windows/Fonts/CascadiaMono.ttf");
    GlyphAtlas matchAtlas(font, charset, 8, 16);
    GlyphAtlas renderAtlas(font, charset, 16, 32);

    Structure::generate(
        img, buffer, matchAtlas,
        {
            .shape = {.blocksX = 2, .blocksY = 4, .bins = 4},
            .orientationWeight = 1.f,
            .massWeight = 1.f,
            .toneWeight = 4.f,
            .allowBackground = false,
    }
    );
    /*
    Bitmask::generate(img, buffer, matchAtlas);
    */

    Edges::apply(img, buffer, charset);

    Despeckle::apply(buffer, matchAtlas, 0.1f);

    const std::string rendered =
        AnsiRenderer::render(buffer, charset, {.depth = AnsiRenderer::ColorDepth::TrueColor});
    Terminal::enableAnsi();   // required on Windows or the escapes print as literal text
    Terminal::enableUtf8();   // no-op for pure ASCII, but this is going through real UTF-8 encoding
    std::cout << rendered;

    ImageManager::saveBufferAsImage(SOURCE_DIR "/output/test_render.png", buffer, renderAtlas);

    matchAtlas.debugDumpToImage(SOURCE_DIR "/output/matchAtlas.png");
    renderAtlas.debugDumpToImage(SOURCE_DIR "/output/renderAtlas.png");
    OutputManager::saveAns(SOURCE_DIR "/output/test_render.ans", rendered);

    const Quality::Report report = Quality::compare(img, buffer, matchAtlas);
    std::cout << std::fixed << std::setprecision(4) << "\nssim " << report.ssim
              << std::setprecision(2) << "   luma rmse " << report.lumaRmse << "   colour rmse "
              << report.colorRmse << "   psnr " << report.psnr << " dB\n";

    return 0;
}
