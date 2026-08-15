#pragma once

#include "file_management/Image.hpp"
#include <filesystem>

namespace ImageManager {

Image loadImage(std::filesystem::path filepath, int desiredChannels = 0);

bool saveImage(std::filesystem::path filepath, const Image& img);
bool savePng(std::filesystem::path filepath, const Image& img);
bool saveJpg(std::filesystem::path filepath, const Image& img);

};   // namespace ImageManager
