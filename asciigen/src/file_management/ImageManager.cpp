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

Image bufferToImage(const CellBuffer& buffer, const GlyphAtlas& atlas)
{
    const int cellW = atlas.cellWidth();
    const int cellH = atlas.cellHeight();
    if (buffer.width() <= 0 || buffer.height() <= 0 || cellW <= 0 || cellH <= 0) return Image();

    Image img(buffer.width() * cellW, buffer.height() * cellH, 3);

    for (int cy = 0; cy < buffer.height(); cy++) {
        for (int cx = 0; cx < buffer.width(); cx++) {
            const Cell& cell = buffer.getAt(cx, cy);
            const uint8_t* glyph = atlas.getGlyphBegin(cell.glyphIndex);

            for (int y = 0; y < cellH; y++) {
                for (int x = 0; x < cellW; x++) {
                    // Coverage blends background toward foreground, the same as a
                    // terminal drawing the cell.
                    const int a = glyph[x + y * cellW];
                    const PixelColor c {
                        (byte)(cell.bg.r + (cell.fg.r - cell.bg.r) * a / 255),
                        (byte)(cell.bg.g + (cell.fg.g - cell.bg.g) * a / 255),
                        (byte)(cell.bg.b + (cell.fg.b - cell.bg.b) * a / 255),
                        255
                    };
                    img.setAt(cx * cellW + x, cy * cellH + y, c);
                }
            }
        }
    }

    return img;
}

bool saveBufferAsImage(
    std::filesystem::path filepath, const CellBuffer& buffer, const GlyphAtlas& atlas
)
{
    const Image img = bufferToImage(buffer, atlas);
    if (!img.pixels) return false;

    return saveImage(filepath, img);
}

}   // namespace ImageManager
