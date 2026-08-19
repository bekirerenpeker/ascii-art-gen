#pragma once

#include "core/CellBuffer.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"
#include <filesystem>

namespace ImageManager {

Image loadImage(std::filesystem::path filepath, int desiredChannels = 0);

bool saveImage(std::filesystem::path filepath, const Image& img);
bool savePng(std::filesystem::path filepath, const Image& img);
bool saveJpg(std::filesystem::path filepath, const Image& img);

Image bufferToImage(const CellBuffer& buffer, const GlyphAtlas& atlas);

bool saveBufferAsImage(
    std::filesystem::path filepath, const CellBuffer& buffer, const GlyphAtlas& atlas
);

};   // namespace ImageManager
