#include "core/Profiler.hpp"
#include "edges/AlphaOutline.hpp"
#include "edges/EdgeCleanup.hpp"
#include "edges/EdgeField.hpp"
#include "edges/Edges.hpp"
#include "edges/Scharr.hpp"
#include <cstdint>

namespace Edges {

// Bucket order matches the angle sweep in every detector: 0, 45, 90, 135 degrees.
static const char32_t kLineGlyphs[4] = {U'-', U'/', U'|', U'\\'};

static void stamp(
    const EdgeField& field, const std::vector<uint8_t>& accepted, float coherence,
    const int (&glyphIndex)[4], CellBuffer& buffer
)
{
    for (int y = 0; y < buffer.height(); y++) {
        for (int x = 0; x < buffer.width(); x++) {
            const size_t i = (size_t)x + (size_t)y * (size_t)buffer.width();

            if (!accepted[i]) continue;
            if (field.coherence[i] < coherence) continue;

            buffer.getAt(x, y).glyphIndex = (uint16_t)glyphIndex[field.bucket[i]];
        }
    }
}

bool apply(const Image& image, CellBuffer& buffer, Charset& charset, const Image& alpha)
{
    const bool wantScharr = options.enabled && image.pixels;
    const bool wantAlpha = alphaOptions.enabled && alpha.pixels;
    if (!wantScharr && !wantAlpha) return false;
    if (buffer.width() <= 0 || buffer.height() <= 0) return false;
    ASCIIGEN_PROFILE("Edges::apply", "edges");


    // Appended rather than merely looked up. A ramp or blocks charset has some of
    // these or none, and looking them up meant one direction got stamped while
    // the others silently did not. Appending puts them past every index already
    // in use, so nothing already chosen shifts -- and doing it here, after
    // selection, is what stops the selector treating them as ordinary glyphs.
    // Both passes stamp the same four characters, so one append covers either.
    const uint16_t sizeBefore = charset.size();

    int glyphIndex[4];
    for (int b = 0; b < 4; b++) glyphIndex[b] = charset.append(kLineGlyphs[b]);

    const bool charsetGrew = charset.size() != sizeBefore;

    if (wantScharr) {
        EdgeField field;
        switch (options.algorithm) {
        case Algorithm::Scharr:
            detectScharr(image, buffer.width(), buffer.height(), options.subsamples, field);
            break;
        }

        if (options.nms) suppressNonMaxima(field);

        const auto accepted =
            hysteresisAccept(field, options.threshold, options.threshold * options.hysteresis);
        stamp(field, accepted, options.coherence, glyphIndex, buffer);
    }

    if (wantAlpha) {
        EdgeField field;
        detectAlphaOutline(alpha, buffer.width(), buffer.height(), options.subsamples, field);

        if (options.nms) suppressNonMaxima(field);

        const auto accepted = hysteresisAccept(
            field, alphaOptions.threshold, alphaOptions.threshold * options.hysteresis
        );
        stamp(field, accepted, alphaOptions.coherence, glyphIndex, buffer);
    }

    return charsetGrew;
}

}   // namespace Edges
