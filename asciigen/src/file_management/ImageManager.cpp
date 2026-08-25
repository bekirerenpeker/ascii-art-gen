#include "core/Profiler.hpp"
#include "file_management/ImageManager.hpp"
#include "core/Image.hpp"
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

void setPngCompression(int level)
{
    stbi_write_png_compression_level = std::clamp(level, 0, 9);

    // The compression level barely moves the needle for this content -- stb's
    // cost is dominated by trying all five PNG row filters and keeping the best,
    // not by deflate. Forcing one filter skips that search entirely. Filter 1
    // (Sub) suits ASCII art: long runs of identical backdrop, and neighbouring
    // pixels within a glyph that differ mostly horizontally.
    //
    // Only at the fast end -- above level 4 the caller has asked for small files
    // and should get the real search back.
    stbi_write_force_png_filter = level <= 4 ? 1 : -1;
}

bool savePng(std::filesystem::path filepath, const Image& img)
{
    ASCIIGEN_PROFILE("savePng", "io");

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
