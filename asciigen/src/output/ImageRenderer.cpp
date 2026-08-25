#include "core/Profiler.hpp"
#include "output/ImageRenderer.hpp"
#include "file_management/ImageManager.hpp"
#include "stb/stb_image_resize2.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace ImageRenderer {

void render(const CellBuffer& buffer, const GlyphAtlas& atlas, Image& out)
{
    const int cellW = atlas.cellWidth();
    const int cellH = atlas.cellHeight();
    if (buffer.width() <= 0 || buffer.height() <= 0 || cellW <= 0 || cellH <= 0) {
        out = Image();
        return;
    }
    ASCIIGEN_PROFILE("ImageRenderer::render", "output");


    const int w = buffer.width() * cellW, h = buffer.height() * cellH;
    if (out.width != w || out.height != h || out.depth != 3) out = Image(w, h, 3);
    Image& img = out;

    // Written through a row pointer rather than setAt. The output is the largest
    // buffer the program touches, and setAt would bounds-check, re-branch on
    // depth and recompute the address for every one of its pixels -- and because
    // a byte store may alias the Image struct itself, reload width/height/depth
    // as well. All of it is loop-invariant here by construction.
    byte* const px = img.pixels;
    const size_t rowStride = (size_t)img.width * 3;

    for (int cy = 0; cy < buffer.height(); cy++) {
        for (int cx = 0; cx < buffer.width(); cx++) {
            const Cell& cell = buffer.getAt(cx, cy);

            // An atlas older than the buffer -- Edges::apply having grown the
            // charset after this atlas was built -- would otherwise be read past
            // its end. Draw the background rather than whatever follows in memory.
            const uint8_t* glyph = (int)cell.glyphIndex < atlas.glyphCount()
                                       ? atlas.getGlyphBegin(cell.glyphIndex)
                                       : nullptr;

            const int br = cell.bg.r, bg = cell.bg.g, bb = cell.bg.b;
            const int dr = cell.fg.r - br, dg = cell.fg.g - bg, db = cell.fg.b - bb;

            for (int y = 0; y < cellH; y++) {
                byte* dst = px + (size_t)(cy * cellH + y) * rowStride + (size_t)cx * cellW * 3;
                const uint8_t* row = glyph ? glyph + (size_t)y * cellW : nullptr;

                for (int x = 0; x < cellW; x++, dst += 3) {
                    // Coverage blends background toward foreground, the same as a
                    // terminal drawing the cell.
                    const int a = row ? row[x] : 0;
                    dst[0] = (byte)(br + dr * a / 255);
                    dst[1] = (byte)(bg + dg * a / 255);
                    dst[2] = (byte)(bb + db * a / 255);
                }
            }
        }
    }
}

Image scale(const Image& src, int width, int height)
{
    if (!src.pixels || width <= 0 || height <= 0) return Image();
    ASCIIGEN_PROFILE("ImageRenderer::scale", "output");


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
    ASCIIGEN_PROFILE("ImageRenderer::compose", "output");


    Image out(canvasW, canvasH, 3);

    // One row is filled a pixel at a time and the rest are copied from it, so the
    // per-pixel work happens once per canvas instead of once per row. A grey
    // backdrop -- which the default black is -- degenerates to a single memset.
    byte* const dst = out.pixels;
    const size_t rowBytes = (size_t)canvasW * 3;

    if (background.r == background.g && background.g == background.b) {
        std::memset(dst, background.r, rowBytes * canvasH);
    }
    else {
        for (int x = 0; x < canvasW; x++) {
            dst[x * 3 + 0] = background.r;
            dst[x * 3 + 1] = background.g;
            dst[x * 3 + 2] = background.b;
        }
        for (int y = 1; y < canvasH; y++) std::memcpy(dst + (size_t)y * rowBytes, dst, rowBytes);
    }

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

    // Negative offsets are how a picture larger than the canvas gets cropped, so
    // the visible span is clipped up front and each row copied whole. Note y
    // counts up from the bottom, so "Top" is the far end of the slack, not zero.
    const int x0 = std::max(0, offX), x1 = std::min(canvasW, offX + src.width);
    const int y0 = std::max(0, offY), y1 = std::min(canvasH, offY + src.height);
    if (x1 <= x0 || y1 <= y0) return out;

    if (src.depth == 3) {
        const size_t span = (size_t)(x1 - x0) * 3;
        for (int y = y0; y < y1; y++) {
            const byte* s = src.pixels + ((size_t)(y - offY) * src.width + (x0 - offX)) * 3;
            std::memcpy(dst + (size_t)y * rowBytes + (size_t)x0 * 3, s, span);
        }
    }
    else {
        // Any other depth still has to go through getAt for its grey and alpha
        // expansion; render() only ever produces 3, so this is the rare path.
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                out.setAt(x, y, src.getAt(x - offX, y - offY));
    }

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

void renderToSize(const CellBuffer& buffer, const GlyphAtlas& atlas, ImageRenderOptions opts, Image& out)
{
    render(buffer, atlas, out);
    if (!out.pixels) return;

    // A missing width or height means "whatever the grid came out as", not "skip
    // all of this" -- scale and margin still have to work against that default
    // canvas, or asking for half size with no explicit picture size does nothing.
    const bool resizing = opts.width > 0 || opts.height > 0 || opts.margin > 0
                       || opts.scale != 1.f || opts.aspect > 0.f;
    if (!resizing) return;   // `out` already holds the answer, reused in place

    // Rarer path (an explicit --image-width/height/scale/margin/aspect): still
    // allocates fresh buffers through the by-value scale()/compose() calls below,
    // a known, deliberately unaddressed gap -- see FrameStorage.hpp. Moving
    // `out`'s buffer into `natural` rather than copying it means this path costs
    // nothing extra beyond what scale()/compose() were always going to allocate.
    Image natural = std::move(out);

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

    out = compose(natural, finalW, finalH, opts.align, opts.backgroundColor, margin);
}

bool save(
    const std::filesystem::path& filepath, const CellBuffer& buffer, const GlyphAtlas& atlas,
    ImageRenderOptions opts
)
{
    Image img;
    renderToSize(buffer, atlas, opts, img);
    if (!img.pixels) return false;

    return ImageManager::saveImage(filepath, img);
}

}   // namespace ImageRenderer
