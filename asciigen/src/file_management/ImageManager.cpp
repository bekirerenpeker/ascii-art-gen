#include "file_management/ImageManager.hpp"
#include "core/CellBuffer.hpp"
#include "core/Image.hpp"
#include "font/GlyphAtlas.hpp"
#include "stb/stb_image_write.h"
#include "stb/stb_image.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>

namespace ImageManager {

Image loadImage(std::filesystem::path filepath, int desiredChannels)
{
    Image img;
    int channelsInFile;

    img.stbAllocated = true;

    stbi_set_flip_vertically_on_load(1);
    img.pixels = stbi_load(
        filepath.string().c_str(), &img.width, &img.height, &channelsInFile, desiredChannels
    );

    if (!img.pixels) {
        img.width = img.height = img.depth = 0;
        std::cout << "couldnt load image from " << filepath << "\n";
        return img;
    }

    // depth describes the buffer, not the file: stb honours desiredChannels when
    // non-zero, and falls back to the file's own channel count otherwise.
    img.depth = desiredChannels ? desiredChannels : channelsInFile;

    return img;
}

bool saveImage(std::filesystem::path filepath, const Image& img)
{
    if (!img.pixels || !img.width || !img.height || !img.depth) return false;

    std::string ext = filepath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == ".png") return savePng(filepath, img);
    if (ext == ".jpg" || ext == ".jpeg") return saveJpg(filepath, img);

    std::cout << "unsupported extension for images: " << ext << "\n";
    return false;
}

bool savePng(std::filesystem::path filepath, const Image& img)
{
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(
               filepath.string().c_str(), img.width, img.height, img.depth, img.pixels, 0
           ) != 0;
}

bool saveJpg(std::filesystem::path filepath, const Image& img)
{
    stbi_flip_vertically_on_write(1);

    // A 4th channel is ignored by the JPEG writer; alpha is simply dropped.
    return stbi_write_jpg(
               filepath.string().c_str(), img.width, img.height, img.depth, img.pixels, 0
           ) != 0;
}

}   // namespace ImageManager
