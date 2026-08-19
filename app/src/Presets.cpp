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

    if (name == "wallpaper") {
        photo(out);
        out.output.imageWidth = 1920;
        out.output.imageHeight = 1080;
        out.output.fit = ImageFit::Contain;
        out.output.align = ImageAlign::Center;
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
    return {"photo", "wallpaper", "terminal", "lineart", "blocks", "plain", "gruvbox", "nord"};
}

}   // namespace Presets
