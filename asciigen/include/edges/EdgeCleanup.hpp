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
void suppressNonMaxima(EdgeField& field);

// Two-threshold connectivity, the other half of the same fix: a cell is
// accepted if it clears `high` outright, or if it clears `low` and is
// 8-connected -- through other `low`-clearing cells -- to one that does.
// Isolated noise above `low` alone has nothing to connect to and is dropped;
// a real edge's fainter far end keeps its line instead of breaking up.
std::vector<uint8_t> hysteresisAccept(const EdgeField& field, float high, float low);

};   // namespace Edges
