#pragma once

#include "core/Image.hpp"
#include <filesystem>

namespace ImageManager {

Image loadImage(std::filesystem::path filepath, int desiredChannels = 0);

bool saveImage(std::filesystem::path filepath, const Image& img);
// 0 fastest and largest, 9 slowest and smallest. stb defaults to 8; at typical
// output sizes that makes encoding cost more than everything else put together,
// and dropping to 2 is roughly four times faster for about a quarter more bytes.
void setPngCompression(int level);

bool savePng(std::filesystem::path filepath, const Image& img);
bool saveJpg(std::filesystem::path filepath, const Image& img);

};   // namespace ImageManager
