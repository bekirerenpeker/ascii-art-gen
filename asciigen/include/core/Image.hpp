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
    bool stbAllocated = false;
    int width = 0, height = 0, depth = 0;
    byte* pixels = nullptr;

    Image() {}
    Image(int w, int h, int d) : width(w), height(h), depth(d) { pixels = new byte[w * h * d]; }
    Image(int w, int h, int d, byte*& p) : width(w), height(h), depth(d), pixels(p) { p = nullptr; }

    Image(Image&& other) { *this = std::move(other); }
    Image& operator=(Image&& other)
    {
        if (this == &other) return *this;

        if (stbAllocated) stbi_image_free(pixels);
        else delete[] pixels;

        stbAllocated = other.stbAllocated;
        width = other.width;
        height = other.height;
        depth = other.depth;
        pixels = other.pixels;

        other.pixels = nullptr;
        other.stbAllocated = false;
        other.width = other.height = other.depth = 0;
        return *this;
    }

    ~Image()
    {
        if (stbAllocated) stbi_image_free(pixels);
        else delete[] pixels;
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

    void setAt(int x, int y, PixelColor color)
    {
        if (x < 0 || y < 0 || x >= width || y >= height) return;

        byte* p = pixels + (x + y * width) * depth;

        if (depth < 3) {
            // 1 and 2 channel images are grey; collapse rgb the same way getAt expands it
            p[0] = byte((color.r * 299 + color.g * 587 + color.b * 114) / 1000);
            if (depth == 2) p[1] = color.a;
            return;
        }

        p[0] = color.r;
        p[1] = color.g;
        p[2] = color.b;
        if (depth == 4) p[3] = color.a;
    }
};
