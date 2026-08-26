#include "filters/ImageFilters.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ImageFilters {

namespace {

float hue2rgb(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 1.f / 2.f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

void rgbToHsl(float r, float g, float b, float& h, float& s, float& l)
{
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    l = (mx + mn) / 2.f;

    if (mx == mn) {
        h = s = 0.f;
        return;
    }

    const float d = mx - mn;
    s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);

    if (mx == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h /= 6.f;
}

void hslToRgb(float h, float s, float l, float& r, float& g, float& b)
{
    if (s == 0.f) {
        r = g = b = l;
        return;
    }

    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    r = hue2rgb(p, q, h + 1.f / 3.f);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 1.f / 3.f);
}

uint8_t toByte(float v) { return (uint8_t)std::clamp(std::lround(v * 255.f), 0L, 255L); }

// Shared by invertBrightness/invertSaturation: decode to HSL, let the caller
// mutate whichever one component it owns, re-encode. `edit` takes (h, s, l)
// by reference so it can flip exactly one of them.
template <typename Edit>
void withHsl(Image& image, Edit edit)
{
    if (!image.pixels || image.depth < 3) return;

    const int d = image.depth;
    const size_t n = (size_t)image.width * (size_t)image.height;
    byte* p = image.pixels;

    for (size_t i = 0; i < n; i++, p += d) {
        float h, s, l;
        rgbToHsl(p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, h, s, l);
        edit(h, s, l);

        float r, g, b;
        hslToRgb(h, s, l, r, g, b);
        p[0] = toByte(r), p[1] = toByte(g), p[2] = toByte(b);
    }
}

}   // namespace

void invert(Image& image)
{
    if (!image.pixels) return;

    const int d = image.depth;
    const size_t n = (size_t)image.width * (size_t)image.height;
    byte* p = image.pixels;

    // Alpha (p[3] at depth 4, p[1] at depth 2) is left alone either way --
    // only the colour/luma channel(s) invert.
    const int colorChannels = d >= 3 ? 3 : 1;
    for (size_t i = 0; i < n; i++, p += d)
        for (int c = 0; c < colorChannels; c++) p[c] = (byte)(255 - p[c]);
}

void invertBrightness(Image& image)
{
    // Grey source: luma IS brightness, so this is the same as invert() --
    // no separate hue/saturation to preserve. Handled here rather than
    // silently doing nothing, so a depth<3 caller still gets what it asked
    // for.
    if (image.pixels && image.depth < 3) {
        invert(image);
        return;
    }

    withHsl(image, [](float&, float&, float& l) { l = 1.f - l; });
}

void invertSaturation(Image& image)
{
    // Grey source has no saturation to invert -- nothing to do.
    if (image.pixels && image.depth < 3) return;

    withHsl(image, [](float&, float& s, float&) { s = 1.f - s; });
}

}   // namespace ImageFilters
