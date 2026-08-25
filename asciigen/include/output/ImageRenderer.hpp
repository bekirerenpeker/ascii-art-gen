#pragma once

#include "core/CellBuffer.hpp"
#include "core/Color.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"
#include <filesystem>

namespace ImageRenderer {

// How the rendered grid is sized into the target box. None ignores the target
// entirely, Stretch ignores aspect, the rest preserve it.
enum class Fit
{
    None,
    Width,
    Height,
    Contain,
    Cover,
    Stretch,
};

enum class Align
{
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight,
};

struct ImageRenderOptions
{
    // Target canvas in pixels. Either left at 0 means "however big the grid comes
    // out", and fit/align then have nothing to do.
    int width = 0;
    int height = 0;

    Fit fit = Fit::Contain;
    Align align = Align::Center;

    // The art is fitted into a box inside the canvas, not into the canvas
    // itself. Shrinking that box is what leaves deliberate empty space -- and
    // what gives align something to actually align within, since a full-bleed
    // fit leaves no slack to position anything in.
    //
    // margin insets the canvas by a fixed number of pixels per side; scale then
    // takes a fraction of what is left. Use margin for "keep 200px clear", scale
    // for "make it 60% the size".
    int margin = 0;
    float scale = 1.f;

    // Width divided by height, e.g. 16.0/9.0. Only ever GROWS the canvas -- the
    // short side is extended with backdrop, nothing is cropped and the art is
    // never resampled to suit. That is the point: it reshapes the final picture
    // without touching the render, so a square source can become a 16:9
    // wallpaper at full glyph resolution. 0 leaves the shape alone, and giving
    // both width and height ignores it, since that already fixes the shape.
    float aspect = 0.f;

    // Painted behind everything. Set this to whatever the cells' own background
    // was filled with, or the padding will not match the art it surrounds.
    RGB backgroundColor {0, 0, 0};
};

// The three steps, separately usable. render() is the natural size the grid and
// atlas imply; scale() resamples that; compose() places it on a canvas.
//
// render() takes its result as an out-parameter, resized only if its current
// size doesn't already match -- a still image calls it once, but a video calls
// it every frame, and the common case (no --image-width/height/scale/margin/
// aspect) is the ENTIRE render: reusing `out`'s buffer there means zero
// allocation after the first frame. scale() and compose() stay by-value: they
// only run at all on the rarer sized/margined/aspect-reshaped path, where the
// source and destination can be genuinely different sizes from call to call.
void render(const CellBuffer& buffer, const GlyphAtlas& atlas, Image& out);

Image scale(const Image& src, int width, int height);

Image compose(
    const Image& src, int canvasW, int canvasH, Align align, RGB background, int margin = 0
);

// render -> scale (per fit) -> compose (per align). What save() runs. `out` is
// reused the same way render()'s own is; on the sized/margined/aspect path it
// ends up holding whatever compose() produced, which is generally a different
// size than the frame before -- render() re-checks its own size next call
// regardless, so this never goes stale, it just doesn't stay warm across a
// switch between the two paths.
void renderToSize(const CellBuffer& buffer, const GlyphAtlas& atlas, ImageRenderOptions opts, Image& out);

// Given a grid that many rows tall and a target pixel height, the atlas cell
// height that lands closest without being upscaled into blur or rendered huge
// and thrown away. Clamped, and kept even so the width stays a whole number.
int suggestedGlyphHeight(int gridRows, int targetHeight, int minimum = 16, int maximum = 64);

bool save(
    const std::filesystem::path& filepath, const CellBuffer& buffer, const GlyphAtlas& atlas,
    ImageRenderOptions opts = {}
);

};   // namespace ImageRenderer
