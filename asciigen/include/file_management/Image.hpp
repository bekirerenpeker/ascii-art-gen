#pragma once

#include "stb/stb_image.h"
#include <utility>

typedef unsigned char byte;

struct Image
{
    int width = 0, height = 0, depth = 0;
    byte* pixels = nullptr;

    Image() {}
    Image(int w, int h, int d) : width(w), height(h), depth(d) {}
    Image(int w, int h, int d, byte*& p) : width(w), height(h), depth(d), pixels(p) { p = nullptr; }

    Image(Image&& other) { *this = std::move(other); }
    Image& operator=(Image&& other)
    {
        width = other.width;
        height = other.height;
        depth = other.depth;
        pixels = other.pixels;
        other.pixels = nullptr;
        return *this;
    }

    ~Image()
    {
        // since stb allocates with malloc its safer to use
        stbi_image_free(pixels);
    }
};
