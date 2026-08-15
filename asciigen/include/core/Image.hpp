#pragma once

#include "stb/stb_image.h"
#include <utility>

typedef unsigned char byte;

struct PixelColor
{
    byte r = 255, g = 255, b = 255, a = 255;
};

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

    PixelColor getAt(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= width || y >= height) return {0, 0, 0, 0};

        PixelColor out;
        out.r = pixels[(x + y * width) * depth + 0];
        if (depth == 1) return {out.r, out.r, out.r, 255};

        if (depth > 1) out.g = pixels[(x + y * width) * depth + 1];
        if (depth == 2) return {out.r, out.r, out.r, out.g};

        if (depth > 2) out.b = pixels[(x + y * width) * depth + 2];
        if (depth > 3) out.a = pixels[(x + y * width) * depth + 3];
        return out;
    }
};
