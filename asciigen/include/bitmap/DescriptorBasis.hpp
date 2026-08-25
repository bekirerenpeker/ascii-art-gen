#pragma once

#include <vector>

namespace Structure {

// A small orthonormal subspace of R^dim, used to bound a dot product before
// paying for it -- see pickGlyph in Structure.cpp. Any orthonormal basis
// gives a valid Cauchy-Schwarz bound; a worse one only prunes less, it can
// never make the bound wrong. That is what lets this use plain power
// iteration instead of an exact eigensolver.
struct DescriptorBasis
{
    int dim = 0;
    int k = 0;
    std::vector<float> vectors;   // k rows of dim floats, row-major, orthonormal

    const float* row(int i) const { return vectors.data() + (size_t)i * dim; }
};

// rows: rowCount vectors of `dim` floats each, packed row-major. k is clamped
// to [0, dim]. Deterministic -- no RNG -- so the basis, and therefore how
// much a run prunes, never varies between runs of the same build.
DescriptorBasis buildBasis(const float* rows, int rowCount, int dim, int k);

// Projects `v` (dim floats, unit length) onto the basis, writing `basis.k`
// coefficients to outCoeffs.
void project(const DescriptorBasis& basis, const float* v, float* outCoeffs);

// The Cauchy-Schwarz residual bound `‖r(b)‖` for vector `v` (dim floats) whose
// projection coefficients (from project(), above) are `coeffs`. Reconstructs
// P(b) = sum_c coeffs[c] * basis.row(c) and measures ‖v - P(b)‖ directly,
// rather than assuming the basis is exactly orthonormal and taking a
// Parseval shortcut -- power iteration only gets close to orthonormal, and
// the bound has to hold regardless of how close. Comes back padded with a
// small safety margin for the reconstruction's own float rounding.
//
// kUse lets a cheaper cascade tier reuse the same coefficients computed for
// the full bound: coeffs[c] only ever depended on basis.row(c), never on how
// many other components exist, so the first kUse of an already-projected
// k-component `coeffs` are exactly the coefficients a kUse-component
// projection would have produced. A smaller kUse gives a looser (still
// valid, just less tight) bound for less work.
float residualNorm(const DescriptorBasis& basis, const float* v, const float* coeffs, int kUse);

}   // namespace Structure
