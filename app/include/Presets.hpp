#pragma once

#include "Options.hpp"
#include <string>
#include <vector>

namespace Presets {

bool apply(const std::string& name, App::Options& out);

std::vector<std::string> names();

};   // namespace Presets
