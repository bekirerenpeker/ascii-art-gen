#pragma once

#include <filesystem>
#include <string>

namespace OutputManager
{

bool saveAns(std::filesystem::path filepath, const std::string& contents);

};   // namespace OutputManager
