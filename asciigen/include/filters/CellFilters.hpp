#pragma once

#include "core/CellBuffer.hpp"
#include "font/GlyphAtlas.hpp"
#include <vector>

// Post-filters. These run on the cell colours AFTER selection, so the glyphs are
// already settled and only the drawing changes. This is where output that looks
// duller than the source gets corrected: coverage carries brightness and then
// the colour carries it again, so the picture lands near luma squared, and no
// amount of pre-filtering recovers it.
//
// All of these touch fg and bg alike. A black backdrop is a fixed point for gain
// and gamma, so the common case of a fixed dark background is left alone anyway,
// while a two-colour split still gets both of its colours treated the same.
namespace CellFilters {

// Saturation that boosts the dullest colours most and leaves vivid ones nearly
// alone. Flat saturation drives already-strong colours to clip and go cartoonish,
// while it is the washed-out midtones that actually read as lifeless.
void vibrance(CellBuffer& buffer, float amount = 0.5f);

// gain scales linearly and clips; gamma below 1 lifts without clipping, which is
// usually what you want for the coverage-darkening described above.
void brightness(CellBuffer& buffer, float gain = 1.f, float gamma = 1.f);

// Snap colours to a fixed palette, matched by perceived distance. strength
// blends, so a partial value tints towards the palette instead of quantising.
void paletteMap(CellBuffer& buffer, const std::vector<RGB>& palette, float strength = 1.f);

// Blank cells that are both faint and close to their own background, which is
// what the scattered dark specks left over from selection are. strength is the
// geometric mean of ink and contrast below which a cell is not worth drawing.
void despeckle(CellBuffer& buffer, const GlyphAtlas& atlas, float strength);

};   // namespace CellFilters
