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
Image render(const CellBuffer& buffer, const GlyphAtlas& atlas);

Image scale(const Image& src, int width, int height);

Image compose(
    const Image& src, int canvasW, int canvasH, Align align, RGB background, int margin = 0
);

// render -> scale (per fit) -> compose (per align). What save() runs.
Image renderToSize(const CellBuffer& buffer, const GlyphAtlas& atlas, ImageRenderOptions opts);

// Given a grid that many rows tall and a target pixel height, the atlas cell
// height that lands closest without being upscaled into blur or rendered huge
// and thrown away. Clamped, and kept even so the width stays a whole number.
int suggestedGlyphHeight(int gridRows, int targetHeight, int minimum = 16, int maximum = 64);

bool save(
    const std::filesystem::path& filepath, const CellBuffer& buffer, const GlyphAtlas& atlas,
    ImageRenderOptions opts = {}
);

};   // namespace ImageRenderer
