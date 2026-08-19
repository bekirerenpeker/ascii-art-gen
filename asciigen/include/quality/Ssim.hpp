#pragma once

#include "core/Image.hpp"

namespace Quality {

double computeSsim(const Image& a, const Image& b, int window = 8, int stride = 4);

};   // namespace Quality
