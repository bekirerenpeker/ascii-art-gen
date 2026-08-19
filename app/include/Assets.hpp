#pragma once

#include <filesystem>
#include <string>

// Where the help pages and the bundled font actually live, which differs between
// running out of the build tree and running an installed copy. Resolved once, by
// looking, rather than baked in -- a path baked at configure time stops being
// true the moment the binary is installed somewhere else.
namespace Assets {

// The directory containing help/ and fonts/. Empty if nothing was found.
const std::filesystem::path& dir();

std::filesystem::path help(const std::string& page);
std::filesystem::path font(const std::string& name);

};   // namespace Assets
