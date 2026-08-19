#pragma once

#include "Options.hpp"

namespace ArgParser {

enum class Result
{
    Ok,
    ExitSuccess,
    ExitFailure,
};

// Accepts both "--flag value" and "--flag=value". A bare word is the input path.
// Presets are applied in a first pass so that an explicit flag always wins, no
// matter which side of the --preset it was written on.
Result parse(int argc, char* argv[], App::Options& out);

};   // namespace ArgParser
