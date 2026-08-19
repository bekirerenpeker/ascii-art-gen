#include "Help.hpp"
#include "Assets.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace Help {

bool print(const std::string& topic)
{
    const std::string page = topic.empty() ? "general" : topic;

    // Help is content, not code: it lives in assets and is only ever read and
    // echoed, so adding a topic means adding a text file and nothing else.
    const std::filesystem::path path = Assets::help(page);
    if (path.empty()) return false;

    std::ifstream file(path);
    if (!file) return false;

    std::cout << file.rdbuf();
    return true;
}

}   // namespace Help
