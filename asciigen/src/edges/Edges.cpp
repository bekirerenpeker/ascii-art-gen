#include "edges/Edges.hpp"
#include "edges/EdgeField.hpp"
#include "edges/Scharr.hpp"
#include <cstdint>

namespace Edges {

void apply(const Image& image, CellBuffer& buffer, const Charset& charset)
{
    if (!options.enabled) return;
    if (!image.pixels || buffer.width() <= 0 || buffer.height() <= 0) return;

    // Bucket order matches the angle sweep in the detector: 0, 45, 90, 135 degrees.
    static const char32_t kLineGlyphs[4] = {U'-', U'/', U'|', U'\\'};

    int glyphIndex[4];
    bool anyAvailable = false;
    for (int b = 0; b < 4; b++) {
        glyphIndex[b] = charset.indexOf(kLineGlyphs[b]);
        anyAvailable |= glyphIndex[b] >= 0;
    }

    // Blocks and braille carry none of these; stamping the nearest thing they do
    // have would be worse than leaving the base selection untouched.
    if (!anyAvailable) return;

    EdgeField field;
    switch (options.algorithm) {
    case Algorithm::Scharr:
        detectScharr(image, buffer.width(), buffer.height(), options.subsamples, field);
        break;
    }

    for (int y = 0; y < buffer.height(); y++) {
        for (int x = 0; x < buffer.width(); x++) {
            const size_t i = (size_t)x + (size_t)y * (size_t)buffer.width();

            // Two independent gates: threshold picks how strong an edge has to be
            // (outlines only, versus every texture ripple), coherence rejects cells
            // where the sub-samples disagree on a direction and no line glyph fits.
            if (field.magnitude[i] < options.threshold) continue;
            if (field.coherence[i] < options.coherence) continue;

            const int g = glyphIndex[field.bucket[i]];
            if (g < 0) continue;

            buffer.getAt(x, y).glyphIndex = (uint16_t)g;
        }
    }
}

}   // namespace Edges
