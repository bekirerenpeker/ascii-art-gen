#pragma once

#include "edges/EdgeField.hpp"
#include <cstdint>
#include <vector>

namespace Edges {

// Zeroes the magnitude of any cell that is not a local maximum along its own
// bucket's gradient axis. A real edge blurred across several cells by
// resampling or depth of field is a ridge, not a spike -- this collapses it
// down to the one cell that is genuinely the peak, before threshold ever
// sees the rest.
//
// `snapshot` is caller-owned scratch: NMS reads the pre-suppression magnitude
// while writing suppressed values into `field` in place, so it needs a copy to
// read from -- reused across calls instead of a fresh one made here each time.
void suppressNonMaxima(EdgeField& field, std::vector<float>& snapshot);

// Two-threshold connectivity, the other half of the same fix: a cell is
// accepted if it clears `high` outright, or if it clears `low` and is
// 8-connected -- through other `low`-clearing cells -- to one that does.
// Isolated noise above `low` alone has nothing to connect to and is dropped;
// a real edge's fainter far end keeps its line instead of breaking up.
//
// `accepted`/`stack` are caller-owned scratch, reused across calls; `accepted`
// is resized (not just cleared) since a different-sized field can follow one
// call to the next.
void hysteresisAccept(
    const EdgeField& field, float high, float low, std::vector<uint8_t>& accepted,
    std::vector<int>& stack
);

};   // namespace Edges
