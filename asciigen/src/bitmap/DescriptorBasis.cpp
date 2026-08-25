#include "bitmap/DescriptorBasis.hpp"
#include <algorithm>
#include <cmath>

namespace Structure {

namespace {

float dot(const float* a, const float* b, int n)
{
    float s = 0.f;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

// G = M^T M, dim x dim, symmetric PSD -- the matrix whose top eigenvectors
// are the vectors the rows vary along the most.
std::vector<float> gram(const float* rows, int rowCount, int dim)
{
    std::vector<float> g((size_t)dim * dim, 0.f);
    for (int r = 0; r < rowCount; r++) {
        const float* row = rows + (size_t)r * dim;
        for (int i = 0; i < dim; i++) {
            const float vi = row[i];
            if (vi == 0.f) continue;
            float* gi = g.data() + (size_t)i * dim;
            for (int j = 0; j < dim; j++) gi[j] += vi * row[j];
        }
    }
    return g;
}

void orthogonalizeAgainstPrior(std::vector<float>& v, const DescriptorBasis& basis, int countSoFar)
{
    for (int p = 0; p < countSoFar; p++) {
        const float* prior = basis.row(p);
        const float d = dot(prior, v.data(), basis.dim);
        for (int i = 0; i < basis.dim; i++) v[i] -= d * prior[i];
    }
}

bool normalize(std::vector<float>& v)
{
    const float len = std::sqrt(dot(v.data(), v.data(), (int)v.size()));
    if (len <= 1e-12f) return false;
    for (float& f : v) f /= len;
    return true;
}

}   // namespace

DescriptorBasis buildBasis(const float* rows, int rowCount, int dim, int kWanted)
{
    DescriptorBasis out;
    out.dim = dim;
    out.k = std::max(0, std::min(kWanted, dim));
    out.vectors.assign((size_t)out.k * dim, 0.f);

    if (out.k == 0 || dim <= 0 || rowCount <= 0) return out;

    const std::vector<float> g = gram(rows, rowCount, dim);
    std::vector<float> v(dim), Gv(dim);

    // Subspace (orthogonal) iteration: each component is power-iterated
    // against G and re-orthogonalized against every earlier component on
    // every step, rather than found once and deflated out of G. That keeps
    // the components numerically orthonormal even after many of them, which
    // is what residualNorm's bound depends on.
    for (int c = 0; c < out.k; c++) {
        for (int i = 0; i < dim; i++) v[i] = std::sin((float)(i + 1) * (float)(c + 3) * 0.63f) + 1.7f;
        orthogonalizeAgainstPrior(v, out, c);
        if (!normalize(v)) continue;

        for (int iter = 0; iter < 40; iter++) {
            for (int i = 0; i < dim; i++) Gv[i] = dot(g.data() + (size_t)i * dim, v.data(), dim);
            orthogonalizeAgainstPrior(Gv, out, c);
            if (!normalize(Gv)) break;
            v = Gv;
        }

        float* row = out.vectors.data() + (size_t)c * dim;
        for (int i = 0; i < dim; i++) row[i] = v[i];
    }

    return out;
}

void project(const DescriptorBasis& basis, const float* v, float* outCoeffs)
{
    for (int c = 0; c < basis.k; c++) outCoeffs[c] = dot(basis.row(c), v, basis.dim);
}

float residualNorm(const DescriptorBasis& basis, const float* v, const float* coeffs, int kUse)
{
    kUse = std::clamp(kUse, 0, basis.k);

    // Direct reconstruction error, not the sqrt(1 - sum(coeffs^2)) shortcut --
    // that shortcut is only exact for a perfectly orthonormal basis, and nothing
    // here guarantees the power-iterated one is. This measures what is actually
    // left over after subtracting the reconstruction back out, so the bound
    // holds no matter how good or bad the basis turned out to be.
    float sumSq = 0.f;
    for (int i = 0; i < basis.dim; i++) {
        float p = 0.f;
        for (int c = 0; c < kUse; c++) p += coeffs[c] * basis.row(c)[i];
        const float r = v[i] - p;
        sumSq += r * r;
    }

    // Pure float rounding in the reconstruction above -- much smaller than the
    // old orthonormality-assuming margin needed, since there is no assumption
    // left to protect against.
    constexpr float kSlack = 1e-4f;
    return std::sqrt(sumSq) + kSlack;
}

}   // namespace Structure
