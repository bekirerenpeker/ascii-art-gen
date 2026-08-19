#include "Presets.hpp"

using namespace App;

namespace Presets {

// The tuned chain lives here rather than in the defaults: nothing that edits the
// photo happens unless it was asked for. `photo` is that chain, and everything
// else builds on it.
static void photo(Options& o)
{
    o.algo.name = AlgoName::Structure;
    o.charset.name = CharsetName::Ascii;

    o.source.autoLevels = true;
    o.source.sharpenAmount = 0.6f;
    o.source.sharpenRadius = 1;

    o.grid.gamma = 0.7f;
    o.grid.vibrance = 0.4f;
    o.grid.despeckle = 0.1f;

    o.backdrop.mode = BackdropMode::Auto;
}

bool apply(const std::string& name, Options& out)
{
    if (name == "photo") {
        photo(out);
        return true;
    }

    // Two wallpapers, because the two ways of filling a screen want opposite
    // settings. Both render at 64px glyphs -- large enough that nothing is
    // upscaled later, which is where sharpness is normally lost.
    //
    // Centred: a small grid, so each glyph reads clearly, floated at 60% inside
    // a 16:9 canvas grown around it. Nothing is resampled; the canvas is
    // reshaped instead. The darker backdrop keeps the empty area from competing
    // with the art.
    if (name == "wallpaper-center") {
        photo(out);
        out.font.renderSize = 64;
        out.grid.height = 35;
        out.output.imageAspect = 16.f / 9.f;
        out.output.imageScale = 0.6f;
        out.backdrop.darken = 0.115f;
        return true;
    }

    // Covering: a tall grid at the same glyph size, so the picture comes out
    // enormous and edge to edge. No aspect and no scale -- the source's own
    // shape is the canvas.
    if (name == "wallpaper-cover") {
        photo(out);
        out.font.renderSize = 64;
        out.grid.height = 80;
        return true;
    }

    if (name == "terminal") {
        photo(out);
        out.output.color = ColorMode::TrueColor;
        out.output.stdoutEnabled = true;
        return true;
    }

    if (name == "lineart") {
        out.algo.name = AlgoName::Ramp;
        out.charset.name = CharsetName::Ramp;
        out.edge.name = EdgeName::Scharr;
        out.edge.threshold = 0.2f;
        out.output.color = ColorMode::None;
        return true;
    }

    if (name == "blocks") {
        out.algo.name = AlgoName::Bitmask;
        out.algo.allowBackground = true;
        out.charset.name = CharsetName::Blocks;
        out.backdrop.mode = BackdropMode::Auto;
        return true;
    }

    if (name == "braille") {
        photo(out);
        out.algo.name = AlgoName::Bitmask;
        out.charset.name = CharsetName::Braille;
        out.backdrop.mode = BackdropMode::Auto;
        return true;
    }

    if (name == "plain") {
        out.algo.name = AlgoName::Ramp;
        out.charset.name = CharsetName::Ramp;
        out.output.color = ColorMode::None;
        out.grid.despeckle = 0.f;
        return true;
    }

    if (name == "gruvbox") {
        photo(out);
        out.grid.palette = PaletteName::Gruvbox;
        out.backdrop.mode = BackdropMode::Fixed;
        out.backdrop.color = {40, 40, 40};
        return true;
    }

    if (name == "nord") {
        photo(out);
        out.grid.palette = PaletteName::Nord;
        out.backdrop.mode = BackdropMode::Fixed;
        out.backdrop.color = {46, 52, 64};
        return true;
    }

    return false;
}

std::vector<std::string> names()
{
    return {"photo",   "wallpaper-center", "wallpaper-cover", "terminal", "lineart",
            "blocks",  "braille",          "plain",           "gruvbox",  "nord"};
}

}   // namespace Presets
