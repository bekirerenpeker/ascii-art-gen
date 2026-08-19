#include "edges/Edges.hpp"
#include "edges/EdgeField.hpp"
#include "edges/Scharr.hpp"
#include <cstdint>

namespace Edges {

bool apply(const Image& image, CellBuffer& buffer, Charset& charset)
{
    if (!options.enabled) return false;
    if (!image.pixels || buffer.width() <= 0 || buffer.height() <= 0) return false;

    // Bucket order matches the angle sweep in the detector: 0, 45, 90, 135 degrees.
    static const char32_t kLineGlyphs[4] = {U'-', U'/', U'|', U'\\'};

    // Appended rather than merely looked up. A ramp or blocks charset has some of
    // these or none, and looking them up meant one direction got stamped while
    // the others silently did not. Appending puts them past every index already
    // in use, so nothing already chosen shifts -- and doing it here, after
    // selection, is what stops the selector treating them as ordinary glyphs.
    const uint16_t sizeBefore = charset.size();

    int glyphIndex[4];
    for (int b = 0; b < 4; b++) glyphIndex[b] = charset.append(kLineGlyphs[b]);

    const bool charsetGrew = charset.size() != sizeBefore;

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

            buffer.getAt(x, y).glyphIndex = (uint16_t)glyphIndex[field.bucket[i]];
        }
    }

    return charsetGrew;
}

}   // namespace Edges
