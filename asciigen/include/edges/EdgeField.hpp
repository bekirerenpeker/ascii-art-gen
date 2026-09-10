#pragma once

#include "core/Image.hpp"
#include <cstdint>
#include <vector>

namespace Edges {

struct EdgeField
{
    int width = 0, height = 0;
    std::vector<float> magnitude;
    std::vector<float> coherence;
    std::vector<int> bucket;
};

// Every buffer a call to apply() needs that would otherwise be a fresh local --
// reused across frames instead of allocated per call. One shared `accepted`/`stack`
// pair serves both the Scharr and alpha passes since they run sequentially, never
// concurrently, within one apply() call.
struct Scratch
{
    EdgeField scharrField;
    EdgeField alphaField;
    std::vector<float> nmsSnapshot;    // suppressNonMaxima's pre-suppression copy
    std::vector<uint8_t> accepted;     // hysteresisAccept's output
    std::vector<int> stack;            // hysteresisAccept's flood-fill stack
    Image scharrPlane;                 // detectScharr's resampled luma plane
    Image alphaPlane;                  // detectAlphaOutline's resampled alpha plane
};

};   // namespace Edges
