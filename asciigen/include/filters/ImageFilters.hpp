#pragma once

#include "core/Image.hpp"
#include <vector>

// Pre-filters. These run on the source image BEFORE resampling and selection,
// so they change which glyph gets chosen. Note that fixing exposure here is not
// the same as fixing it afterwards: brighten the source and the selector simply
// answers with denser glyphs, landing back where it started. Use these to give
// the selector a cleaner signal, not to correct the look of the output.
namespace ImageFilters {

// Stretch the tonal range so the darkest and brightest parts reach the ends of
// the scale. Percentiles rather than the true min and max, because one blown
// highlight or one black pixel would otherwise pin the range and do nothing.
void autoLevels(Image& image, float lowPercent = 0.5f, float highPercent = 99.5f);

// The manual version, for when auto guesses wrong. gamma above 1 lifts midtones.
void levels(Image& image, float blackPoint = 0.f, float whitePoint = 255.f, float gamma = 1.f);

// Pivots around mid grey. 1 leaves the image alone, below 1 flattens it.
void contrast(Image& image, float amount = 1.f);

// Adds back the detail a blur would remove, which sharpens edges. Worth having
// before Structure runs: a harder edge gives its orientation term more to go on.
void unsharpMask(Image& image, float amount = 0.6f, int radius = 1);

// Softens the source. Useful before selection on noisy or over-detailed photos,
// where the noise otherwise competes with real structure for the glyph choice.
void blur(Image& image, int radius);

// The same kernel over a bare float plane, for mid-pipeline callers that do not
// have an Image: unsharpMask above, and Bitmask blurring its glyph masks.
// scratch is caller-owned and resized in place, not allocated fresh each call
// -- pass the same vector back in across repeated calls of the same w*h (once
// per channel, once per glyph, ...) and it never reallocates past the first.
void blur(const float* src, float* dst, int w, int h, int radius, std::vector<float>& scratch);

};   // namespace ImageFilters
