#include "filters/CellFilters.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace CellFilters {

static float luma(const RGB& c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

void despeckle(CellBuffer& buffer, const GlyphAtlas& atlas, float strength)
{
    if (strength <= 0.f) return;

    const int cellPx = atlas.glpyhSize();
    const int glyphCount = atlas.glyphCount();
    if (cellPx <= 0 || glyphCount <= 0 || buffer.width() <= 0 || buffer.height() <= 0) return;

    // Mean ink per glyph, and the emptiest glyph in the set -- that is what a
    // cleared cell becomes. Picked by coverage rather than by looking up a space
    // codepoint, so this still works on a charset that has no literal space.
    std::vector<float> coverage(glyphCount, 0.f);
    float maxCoverage = 0.f;
    int blankGlyph = 0;

    for (int g = 0; g < glyphCount; g++) {
        const uint8_t* mask = atlas.getGlyphBegin(g);

        float sum = 0.f;
        for (int i = 0; i < cellPx; i++) sum += mask[i] / 255.f;

        coverage[g] = sum / cellPx;
        maxCoverage = std::max(maxCoverage, coverage[g]);
        if (coverage[g] < coverage[blankGlyph]) blankGlyph = g;
    }

    if (maxCoverage <= 0.f) return;

    for (int y = 0; y < buffer.height(); y++) {
        for (int x = 0; x < buffer.width(); x++) {
            Cell& cell = buffer.getAt(x, y);
            if (cell.glyphIndex >= glyphCount) continue;

            const float ink = coverage[cell.glyphIndex] / maxCoverage;

            // Against the cell's own background, so a custom backdrop and the
            // two-colour split's per-cell paper both work without special casing.
            const float contrast = std::fabs(luma(cell.fg) - luma(cell.bg)) / 255.f;

            // Geometric mean, so a cell has to be both faint AND low-contrast to
            // go: heavy ink survives a dim colour, a bright colour survives a thin
            // glyph. The sqrt spreads out the low end, where every real case sits.
            if (std::sqrt(ink * contrast) < strength) cell.glyphIndex = (uint16_t)blankGlyph;
        }
    }
}

}   // namespace CellFilters
