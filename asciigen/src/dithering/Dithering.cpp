#include "dithering/Dithering.hpp"
#include "dithering/Bayer4.hpp"
#include "dithering/BlockContrast.hpp"
#include "dithering/ContrastField.hpp"

namespace Dithering {

void apply(Image& plane, int blockW, int blockH)
{
    if (!options.enabled) return;

    // Computed here rather than per algorithm: where dither helps is a property
    // of the picture, not of the pattern used. An empty field reads back as 1
    // everywhere, so the algorithms need no branch for the non-adaptive case.
    ContrastField gate;
    if (options.adaptive) {
        // Half the bias swing: how far a value can actually be pushed either way,
        // and so how much room a tone needs before dithering it means anything.
        const float headroom = 255.f / (2.f * (float)(options.levels - 1));

        computeBlockContrast(
            plane, blockW, blockH, options.flatContrast, options.edgeContrast, headroom, gate
        );
    }

    switch (options.algorithm) {
    case Algorithm::Bayer4: applyBayer4(plane, options.levels, blockW, blockH, gate); break;
    }
}

}   // namespace Dithering
