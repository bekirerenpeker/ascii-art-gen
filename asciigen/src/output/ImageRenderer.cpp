#include "output/ImageRenderer.hpp"
#include "file_management/ImageManager.hpp"
#include "stb/stb_image_resize2.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ImageRenderer {

Image render(const CellBuffer& buffer, const GlyphAtlas& atlas)
{
    const int cellW = atlas.cellWidth();
    const int cellH = atlas.cellHeight();
    if (buffer.width() <= 0 || buffer.height() <= 0 || cellW <= 0 || cellH <= 0) return Image();

    Image img(buffer.width() * cellW, buffer.height() * cellH, 3);

    for (int cy = 0; cy < buffer.height(); cy++) {
        for (int cx = 0; cx < buffer.width(); cx++) {
            const Cell& cell = buffer.getAt(cx, cy);

            // An atlas older than the buffer -- Edges::apply having grown the
            // charset after this atlas was built -- would otherwise be read past
            // its end. Draw the background rather than whatever follows in memory.
            const uint8_t* glyph = (int)cell.glyphIndex < atlas.glyphCount()
                                       ? atlas.getGlyphBegin(cell.glyphIndex)
                                       : nullptr;

            for (int y = 0; y < cellH; y++) {
                for (int x = 0; x < cellW; x++) {
                    // Coverage blends background toward foreground, the same as a
                    // terminal drawing the cell.
                    const int a = glyph ? glyph[x + y * cellW] : 0;
                    const PixelColor c {
                        (byte)(cell.bg.r + (cell.fg.r - cell.bg.r) * a / 255),
                        (byte)(cell.bg.g + (cell.fg.g - cell.bg.g) * a / 255),
                        (byte)(cell.bg.b + (cell.fg.b - cell.bg.b) * a / 255),
                        255
                    };
                    img.setAt(cx * cellW + x, cy * cellH + y, c);
                }
            }
        }
    }

    return img;
}

Image scale(const Image& src, int width, int height)
{
    if (!src.pixels || width <= 0 || height <= 0) return Image();

    Image out(width, height, src.depth);

    if (width == src.width && height == src.height) {
        std::copy(src.pixels, src.pixels + (size_t)width * height * src.depth, out.pixels);
        return out;
    }

    stbir_pixel_layout layout = STBIR_1CHANNEL;
    if (src.depth == 2) layout = STBIR_2CHANNEL;
    else if (src.depth == 3) layout = STBIR_RGB;
    else if (src.depth == 4) layout = STBIR_RGBA;

    stbir_resize_uint8_linear(
        src.pixels, src.width, src.height, 0, out.pixels, width, height, 0, layout
    );

    return out;
}

Image compose(
    const Image& src, int canvasW, int canvasH, Align align, RGB background, int margin
)
{
    if (canvasW <= 0 || canvasH <= 0) return Image();

    Image out(canvasW, canvasH, 3);
    for (int y = 0; y < canvasH; y++)
        for (int x = 0; x < canvasW; x++)
            out.setAt(x, y, {background.r, background.g, background.b, 255});

    if (!src.pixels) return out;

    // Alignment happens inside the margin, so "top-left" means a margin in from
    // the corner rather than jammed against it.
    const int m = std::max(0, margin);
    const int slackX = canvasW - 2 * m - src.width;
    const int slackY = canvasH - 2 * m - src.height;

    int offX = m + slackX / 2;
    int offY = m + slackY / 2;

    switch (align) {
    case Align::TopLeft: offX = m, offY = m + slackY; break;
    case Align::Top: offY = m + slackY; break;
    case Align::TopRight: offX = m + slackX, offY = m + slackY; break;
    case Align::Left: offX = m; break;
    case Align::Center: break;
    case Align::Right: offX = m + slackX; break;
    case Align::BottomLeft: offX = m, offY = m; break;
    case Align::Bottom: offY = m; break;
    case Align::BottomRight: offX = m + slackX, offY = m; break;
    }

    // Negative offsets are how a picture larger than the canvas gets cropped:
    // setAt drops anything landing outside. Note y counts up from the bottom, so
    // "Top" is the far end of the slack, not zero.
    for (int y = 0; y < src.height; y++)
        for (int x = 0; x < src.width; x++) out.setAt(offX + x, offY + y, src.getAt(x, y));

    return out;
}

// boxW/boxH is the space the art may occupy, already inset and scaled by the
// caller. `scale` is passed separately only for Fit::None, which has no box to
// fit into and so has to apply it to the natural size directly.
static void fitSize(
    int natW, int natH, int boxW, int boxH, Fit fit, float scale, int& outW, int& outH
)
{
    outW = natW, outH = natH;
    if (natW <= 0 || natH <= 0 || boxW <= 0 || boxH <= 0) return;

    const float sx = (float)boxW / natW;
    const float sy = (float)boxH / natH;
    float s = 1.f;

    switch (fit) {
    case Fit::None: s = scale; break;
    case Fit::Stretch: outW = boxW, outH = boxH; return;
    case Fit::Width: s = sx; break;
    case Fit::Height: s = sy; break;
    case Fit::Contain: s = std::min(sx, sy); break;
    case Fit::Cover: s = std::max(sx, sy); break;
    }

    outW = std::max(1, (int)std::lround(natW * s));
    outH = std::max(1, (int)std::lround(natH * s));
}

int suggestedGlyphHeight(int gridRows, int targetHeight, int minimum, int maximum)
{
    if (gridRows <= 0 || targetHeight <= 0) return minimum;

    // The floor matters more than the ceiling. Rendering bigger than needed and
    // scaling DOWN costs nothing -- that is just supersampling. Rendering small
    // and scaling UP is what turns glyphs to mush, so the floor is set where
    // letterforms still hold together rather than as low as it could go.
    //
    // Round to even so the 1:2 cell aspect stays a whole number of pixels.
    int h = (int)std::lround((double)targetHeight / gridRows);
    h -= h & 1;

    return std::clamp(h, minimum, maximum);
}

Image renderToSize(const CellBuffer& buffer, const GlyphAtlas& atlas, ImageRenderOptions opts)
{
    Image natural = render(buffer, atlas);
    if (!natural.pixels) return natural;

    // A missing width or height means "whatever the grid came out as", not "skip
    // all of this" -- scale and margin still have to work against that default
    // canvas, or asking for half size with no explicit picture size does nothing.
    const bool resizing = opts.width > 0 || opts.height > 0 || opts.margin > 0
                       || opts.scale != 1.f || opts.aspect > 0.f;
    if (!resizing) return natural;

    const int canvasW = opts.width > 0 ? opts.width : natural.width;
    const int canvasH = opts.height > 0 ? opts.height : natural.height;

    // Inset first, then take a fraction. What is left is the box the art fits
    // into; everything outside it stays backdrop, and is the slack that makes
    // alignment mean something.
    const int margin = std::max(0, opts.margin);
    const float boxScale = opts.scale > 0.f ? opts.scale : 1.f;

    const int boxW = std::max(1, (int)std::lround((canvasW - 2 * margin) * boxScale));
    const int boxH = std::max(1, (int)std::lround((canvasH - 2 * margin) * boxScale));

    int fittedW = 0, fittedH = 0;
    fitSize(natural.width, natural.height, boxW, boxH, opts.fit, boxScale, fittedW, fittedH);

    if (fittedW != natural.width || fittedH != natural.height)
        natural = scale(natural, fittedW, fittedH);

    // Grown only now, after the art has been sized against the ungrown canvas.
    // Do it any earlier and the bigger box would make `contain` scale the art
    // straight back up to fill it, which is the opposite of what aspect is for.
    // Explicit width AND height already pin the shape, so aspect stays out.
    int finalW = canvasW, finalH = canvasH;
    if (opts.aspect > 0.f && !(opts.width > 0 && opts.height > 0)) {
        const float current = (float)finalW / (float)finalH;

        if (current < opts.aspect) finalW = (int)std::lround(finalH * opts.aspect);
        else if (current > opts.aspect) finalH = (int)std::lround(finalW / opts.aspect);
    }

    return compose(natural, finalW, finalH, opts.align, opts.backgroundColor, margin);
}

bool save(
    const std::filesystem::path& filepath, const CellBuffer& buffer, const GlyphAtlas& atlas,
    ImageRenderOptions opts
)
{
    const Image img = renderToSize(buffer, atlas, opts);
    if (!img.pixels) return false;

    return ImageManager::saveImage(filepath, img);
}

}   // namespace ImageRenderer
