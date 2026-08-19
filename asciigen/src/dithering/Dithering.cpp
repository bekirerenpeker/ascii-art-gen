#include "dithering/Dithering.hpp"
#include "dithering/Bayer4.hpp"

namespace Dithering {

void apply(Image& plane, int blockW, int blockH)
{
    if (!options.enabled) return;

    switch (options.algorithm) {
    case Algorithm::Bayer4: applyBayer4(plane, options.levels, blockW, blockH); break;
    }
}

}   // namespace Dithering
