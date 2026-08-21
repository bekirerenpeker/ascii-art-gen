#include "edges/EdgeCleanup.hpp"

namespace Edges {

void suppressNonMaxima(EdgeField& field)
{
    const int w = field.width, h = field.height;
    if (w <= 0 || h <= 0) return;

    const std::vector<float> src = field.magnitude;

    auto at = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0.f;
        return src[(size_t)x + (size_t)y * w];
    };

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const size_t i = (size_t)x + (size_t)y * w;
            if (src[i] <= 0.f) continue;

            // The bucket names the LINE's direction; the gradient runs
            // perpendicular to it, which is the axis a ridge has to be
            // checked along.
            int dx1, dy1, dx2, dy2;
            switch (field.bucket[i]) {
            case 0: dx1 = 0, dy1 = -1, dx2 = 0, dy2 = 1; break;    // "-" -> vertical
            case 1: dx1 = -1, dy1 = 1, dx2 = 1, dy2 = -1; break;   // "/" -> "\" axis
            case 2: dx1 = -1, dy1 = 0, dx2 = 1, dy2 = 0; break;    // "|" -> horizontal
            default: dx1 = -1, dy1 = -1, dx2 = 1, dy2 = 1; break;  // "\" -> "/" axis
            }

            if (src[i] < at(x + dx1, y + dy1) || src[i] < at(x + dx2, y + dy2))
                field.magnitude[i] = 0.f;
        }
    }
}

std::vector<uint8_t> hysteresisAccept(const EdgeField& field, float high, float low)
{
    const int w = field.width, h = field.height;
    std::vector<uint8_t> accepted((size_t)w * (size_t)h, 0);
    if (w <= 0 || h <= 0) return accepted;

    std::vector<int> stack;
    stack.reserve(64);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const size_t i = (size_t)x + (size_t)y * w;
            if (field.magnitude[i] >= high) {
                accepted[i] = 1;
                stack.push_back((int)i);
            }
        }
    }

    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % w, y = i / w;

        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                if (ox == 0 && oy == 0) continue;

                const int nx = x + ox, ny = y + oy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;

                const size_t ni = (size_t)nx + (size_t)ny * w;
                if (accepted[ni] || field.magnitude[ni] < low) continue;

                accepted[ni] = 1;
                stack.push_back((int)ni);
            }
        }
    }

    return accepted;
}

};   // namespace Edges
