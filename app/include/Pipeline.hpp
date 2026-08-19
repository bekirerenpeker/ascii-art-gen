#pragma once

#include "Options.hpp"

namespace Pipeline {

// Runs a fully resolved Options end to end. Returns a process exit code.
int run(const App::Options& opts);

};   // namespace Pipeline
