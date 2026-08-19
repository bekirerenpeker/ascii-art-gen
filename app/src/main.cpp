#include "bitmap/Bitmask.hpp"
#include "bitmap/Structure.hpp"
#include "bitmap/Ramp.hpp"
#include "core/CellBuffer.hpp"
#include "core/Charset.hpp"
#include "dithering/Dithering.hpp"
#include "edges/Edges.hpp"
#include "file_management/ImageManager.hpp"
#include "filters/CellFilters.hpp"
#include "filters/ImageFilters.hpp"
#include "filters/Palettes.hpp"
#include "file_management/OutputManager.hpp"
#include "font/Font.hpp"
#include "font/GlyphAtlas.hpp"
#include "output/AnsiRenderer.hpp"
#include "output/ImageRenderer.hpp"
#include "output/Terminal.hpp"
#include "quality/Quality.hpp"
#include <iomanip>
#include <iostream>

int pixelsPerChar = 30;

int main(int argc, char* argv[])
{
    Image img = ImageManager::loadImage(SOURCE_DIR "/assets/images/porsche-911-blue.png");

    Dithering::options = {
        .enabled = false,
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

    // Before selection: give the selector a cleaner signal to work from.
    ImageFilters::autoLevels(img);
    ImageFilters::unsharpMask(img, 0.6f, 1);

    Charset charset = Charset::ascii();
    CellBuffer buffer;
    buffer.setSize(2 * img.width / pixelsPerChar, img.height / pixelsPerChar);

    Font font("C:/Windows/Fonts/CascadiaMono.ttf");
    GlyphAtlas matchAtlas(font, charset, 8, 16);
    GlyphAtlas renderAtlas(font, charset, 8, 16);

    Structure::generate(
        img, buffer, matchAtlas,
        {
            .shape =
                {.orientBlocksX = 2,
                        .orientBlocksY = 4,
                        .bins = 4,
                        .massBlocksX = 8,
                        .massBlocksY = 16},
            .orientationWeight = 0.25f,
            .massWeight = 1.f,
            .toneWeight = 4.f,
            .allowBackground = false,
    }
    );

    Edges::apply(img, buffer, charset);

    CellFilters::despeckle(buffer, matchAtlas, 0.1f);

    // After selection: undo the coverage-plus-colour darkening. Gamma rather than
    // gain so highlights lift without clipping.
    CellFilters::brightness(buffer, 1.2f, 0.7f);
    CellFilters::vibrance(buffer, 0.4f);

    // Last, so the colour filters above cannot shift it. Cells despeckle blanked
    // still carry a background, and leaving those black speckles the backdrop.
    const RGB backdrop = buffer.suggestedBackground();
    buffer.fillBackground(backdrop);

    const std::string rendered =
        AnsiRenderer::render(buffer, charset, {.depth = AnsiRenderer::ColorDepth::TrueColor});
    Terminal::enableAnsi();   // required on Windows or the escapes print as literal text
    Terminal::enableUtf8();   // no-op for pure ASCII, but this is going through real UTF-8 encoding
    std::cout << rendered;

    ImageRenderer::save(
        SOURCE_DIR "/output/test_render.png", buffer, renderAtlas,
        {.width = 1900, .height = 1200, .backgroundColor = backdrop}
    );

    matchAtlas.debugDumpToImage(SOURCE_DIR "/output/matchAtlas.png");
    renderAtlas.debugDumpToImage(SOURCE_DIR "/output/renderAtlas.png");
    OutputManager::saveAns(SOURCE_DIR "/output/test_render.ans", rendered);

    const Quality::Report report = Quality::compare(img, buffer, matchAtlas);
    std::cout << std::fixed << std::setprecision(4) << "\nssim " << report.ssim
              << std::setprecision(2) << "   luma rmse " << report.lumaRmse << "   colour rmse "
              << report.colorRmse << "   psnr " << report.psnr << " dB\n";

    return 0;
}
