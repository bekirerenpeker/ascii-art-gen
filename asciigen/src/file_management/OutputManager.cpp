#include "file_management/OutputManager.hpp"

#include <fstream>

namespace OutputManager
{

bool saveAns(std::filesystem::path filepath, const std::string& contents)
{
    // Binary, not text mode: the file must carry exactly the rendered bytes,
    // \n included as a bare line feed. Text mode on Windows silently rewrites
    // every \n to \r\n, and .ans is meant to be portable, exact-byte output --
    // that's the whole reason it needs no player, just `cat`/`type`.
    std::ofstream file(filepath, std::ios::binary);
    if (!file) return false;

    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return file.good();
}

};   // namespace OutputManager
