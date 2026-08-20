# Optimizations

Working list. Anything marked *estimate* is reasoning from a trace, not a measurement —
a reason to try, not a promise. Measured numbers say where they came from.

Traces come from a build with `ASCIIGEN_PROFILING=ON` (the default); `--profile PATH` writes
one. Open it at [ui.perfetto.dev](https://ui.perfetto.dev) or
[speedscope.app](https://speedscope.app).

**Rule for everything below: the picture must not change.** Every item here is either
arithmetically identical or exactly equivalent. Anything that trades quality for speed is
called out as such and is not on the list.

---

## Done

**1. Run source filters after the resample, not before.** Filters now touch a few hundred
thousand pixels instead of thirty million. Measured ~25× on large sources.
*(But see "the plane is upsampled" below — this is the right rule only when the source is
bigger than the plane, which is not always.)*

**2. Lower the default PNG compression, and skip stb's row-filter search below level 5.**
`--png-compression` exposes it. Measured ~32% off encode at level 1 for ~9% more bytes.

**3. Raw pointer loops instead of `getAt` / `setAt`.** The accessors were already inlined;
the cost was the per-pixel bounds check, the `depth` branch, the address multiply, and — the
real one — that a `byte` store may alias the struct's own fields, forcing
`width`/`height`/`depth`/`pixels` to be reloaded after every write. Nothing about `inline`
could have helped.

Measured on a 31MP source: `Resample::toGrid` 1.63×, `autoLevels` 1.54×,
`ImageRenderer::render` 1.13×, whole run 1.05×. Output bit-identical across 12
configurations. The estimate (2–4×) was too high and the stages are a small share of a run.

---

## What the numbers actually say

From `output/trace-mid.json` — a 1920×840 source, `--grid-height 100` (a 457×100 grid,
45,700 cells), `--preset photo`, dither on:

| scope | ms | in a video? |
|---|---|---|
| `savePng` | 495.0 | **no** — frames go to ffmpeg raw |
| `Structure::generate` | 360.5 | yes |
| `source filters` | 198.1 | yes (`unsharpMask` 165.2) |
| `ImageRenderer::render` | 65.6 | yes |
| `Resample::toGrid` | 59.9 | yes |
| `Dithering::apply` | 44.6 | yes |
| `autoLevels` | 32.9 | yes |
| `load` | 16.0 | **no** — ffmpeg decodes |

**For stills** the bottleneck is stb: `savePng` + `load` is 60–75% of wall time. Lowering
the default `--png-compression` from 8 beats everything else on this page, and replacing
stb's PNG encoder beats that.

**For video** both of those disappear. Real per-frame work is **~700 ms → 1.4 fps**, and
`Structure::generate` is over half of it. Everything below is about that number.

`trace-high` vs `trace-mid` also settles a question: `Structure::generate` is 356.7 vs 360.5,
i.e. identical. **`--render-detail` does not change selection at all** — high's extra 2.3 s
is entirely `savePng` and a larger `render`. For a final still, mid and high produce the same
art at different resolutions.

### Where Structure's 360 ms goes

Measured by holding the grid fixed and varying charset size:

| glyphs | `Structure::generate` |
|---|---|
| 4 | 165.2 ms |
| 10 | 176.0 ms |
| 26 | 210.5 ms |
| 95 | 370.2 ms |
| 256 | 715.8 ms |

Straight line: **157 ms fixed + 2.19 ms per glyph** (predicts 364 for ascii, measured 370).

So at ascii, scoring is 208 ms and **157 ms is fixed per-cell work that no matching
cleverness touches**. Both halves need their own attack. This is the single most useful
number on this page — it caps what any glyph-matching optimisation can be worth.

### The plane is upsampled 3.6×

Source 1920×840 = 1.6 MPix. Plane at `--font-match-size 16` is 3656×1600 = **5.85 MPix**.
Every per-pixel stage — resample, filters, dither, tile gather — runs on 3.6× more pixels
than the source contains. We invent pixels and then pay to filter them.

The matcher genuinely needs 8×16 per cell to compare against glyph bitmaps, so the plane
size is forced. The *source filters* are not: the right rule is **filter at whichever is
smaller, source or plane** — after the resample when the source is huge (why #1 was done),
before it when the source is smaller. Worth ~140 ms here.

Caveat: filter radii would then mean different things depending on which side they land,
which contradicts what `--help source` now promises. Weigh that before doing it.

---

## Shared — helps both selectors

**S1. Reformulate scoring as a matrix multiply.** *The big one.*
Both selectors reduce to the same kernel: a dot product of a 128- or 160-dimensional tile
vector against a glyph matrix that never changes.

- Structure: `similarity()` (`Descriptor.cpp:118`) is a plain dot product of vectors that
  `centreAndNormalize` already made zero-mean and unit-length. The weights `orientW`/`massW`
  are per-*tile* constants, so they factor out of the glyph loop entirely.
- Bitmask: the inner `lw += tileLuma[i] * (mask[i]/255)` is the same thing against the mask
  matrix, and the `allowBackground` branch needs the identical product (`paperL = sumL − inkL`).

Currently: 45,700 separate dot products with a branch in the loop, ~3.3 GMAC/s. As a blocked
GEMM with FMA, 10–20× on the same arithmetic. *Estimate: Structure scoring 208 → ~25 ms;
Bitmask similar.*

**S2. Exact shortlist via a projection bound.** Composes with S1.
Project glyph descriptors onto their top-k principal components once. For a unit-length tile
vector `a` and glyph `b = P(b) + r(b)`, Cauchy–Schwarz gives `|a·r(b)| ≤ ‖r(b)‖`, so
`a·P(b) + ‖r(b)‖` is a true upper bound on the real score. Evaluate exactly only the glyphs
whose bound beats the best exact score so far.

Provably the same winner. Typically skips 80–90% of candidates, and the bigger the charset
the more it skips — braille (256 glyphs) benefits most.

**S3. `Dithering::apply` still uses `getAt`.** Missed in #3.
`BlockContrast.cpp` reads its contrast window through `plane.getAt(x, y)` with two clamps per
sample — 5.85M of them per frame, which is most of the 44.6 ms. The window is a fixed
rectangle inside a known-size plane, so it can be a clipped row walk. Straightforward and
exact. *Estimate: 44.6 → ~15 ms.*

**S4. Fix `unsharpMask`'s allocations.** It allocates six `n`-float vectors — **140 MB** at
this plane size — and is memory-bound rather than compute-bound. Two reused buffers, or
fusing the gather with the first blur pass. *Estimate: 165 → ~55 ms.*

**Already shared and already done:** `Resample::toGrid`, the tile gather in both selectors,
and `ImageRenderer::render`/`compose` all got the pointer treatment in #3, so both selectors
already benefit. `solveCellColor` is shared and is called once per cell, not per glyph — it
is not hot.

---

## Structure only

**T1. Kill the clamps in the gradient.** *Best value in this section.*
`buildDescriptor` calls `at()` **12 times per pixel**, and every `at()` does two
`std::clamp`s — 24 clamps and 12 bounds-checked loads per pixel, over 5.85M pixels. The
interior of an 8×16 cell is 6×14, so **74% of pixels need no clamping at all**. Split
interior from border and slide the window instead of re-loading the same neighbours for each
`x`. Exactly output-preserving. *Estimate: the larger part of the 157 ms fixed cost.*

**T2. Hoist the per-cell allocation out of `buildDescriptor`.**
`std::vector<int> counts(mx*my)` is constructed **inside** the function, so it allocates once
per cell — 45,700 mallocs per frame. Pass a scratch buffer in, or make it a fixed-size array.
See the video section; this is the worst offender.

**T3. Leave `atan2` alone.** Previously listed as a win; it is not, if quality must hold.
The soft binning needs the true angle, not just the bin index, so any fast approximation
shifts the histogram. Removing the clamps (T1) is worth more anyway and costs nothing in
exactness.

---

## Bitmask only

**B1. Hoist `wMean` and `wStd` out of the cell loop.** *Free, and embarrassing.*

```cpp
const float wMean = w / cellPx;
const float wStd  = std::sqrt(std::max(0.f, inkWeightSq[g] - cellPx * wMean * wMean));
```

Both depend only on `g` — yet they sit inside the glyph loop inside the cell loop, so they
are recomputed for every cell. That is **4.3 million redundant `sqrt` calls per frame** at
ascii. Precompute two arrays alongside `inkWeight`. Bit-identical.

**B2. Precompute the masks as floats.** The inner loop does `mask[i] / 255.f` — a byte load,
a convert and a multiply — **per pixel, per glyph, per cell**: 556M of them per frame at
ascii. A float copy of the mask array is `glyphCount × cellPx × 4` = 48 KB for ascii, which
sits in L2. Converts the loop into a clean float dot product and is the precondition for S1
anyway. Bit-identical if the same `×(1/255)` is folded in at build time.

**B3. `softness` runs a full blur per cell.** When `--algo-bitmask-softness > 0`,
`ImageFilters::blur` is called once per cell on the tile. It is off by default and measured
unhelpful, so this is only worth touching if the dial is ever revived — but if so, the blur
is separable over a fixed 8×16 tile and can be a precomputed small convolution.

**Bitmask inherits S1, S2, S3, S4 unchanged.** B2 is the step that makes S1 applicable.

---

## Video: stop reallocating

Nothing in the per-frame path should allocate. Today, most of it does. Every item is
per-*frame* unless noted:

| what | where | size |
|---|---|---|
| `std::vector<int> counts` | `buildDescriptor` | small × **45,700 per frame** |
| `Image plane` | `Structure::generate`, `Bitmask::generate` | 17.5 MB |
| 6 × `n` floats | `unsharpMask` | 140 MB |
| 2 × `n` floats | `blur(Image&)` | 47 MB |
| `ContrastField gate` | `Dithering::apply` | ~180 KB |
| output `Image` | `ImageRenderer::render` | large |
| `Image out` | `scale`, `compose` | large |
| `inkWeight`, `inkWeightSq`, `blurMask*` | `Bitmask::generate` | small, but see below |
| `GlyphModel` | `Structure::generate` via `buildGlyphModel` | small, but see below |

You were right that the still path mostly gets away with it — one allocation per run is
nothing. At 30 fps it is a different picture, and `buildDescriptor`'s per-cell vector is
1.4 million allocations per second.

The fix is the same everywhere: **a `Scratch` struct owned by the caller**, holding every
buffer, passed into the pipeline and reused. Sizes depend only on grid and plane dimensions,
which are fixed for the whole video, so it is allocated once and never grows.

---

## Video: what can be computed once and reused for every frame

You asked whether some work could be cached across frames. Yes — more than I expected, and
it is nearly free to do. Two clean tiers:

**Depends only on the atlas/charset — constant for the entire video:**

- `buildGlyphModel` — glyph descriptors, `inkWeight`, `maxCoverage` (Structure)
- `inkWeight`, `inkWeightSq`, `maxCoverage`, `blurMask`, `blurMaskMean`, `blurMaskStd` (Bitmask)
- the float mask array from **B2**
- the hoisted `wMean` / `wStd` from **B1**
- the PCA basis and residual norms `‖r(b)‖` from **S2**
- **the packed glyph matrix for S1** — normally GEMM packing is an overhead you have to
  amortise; here you pack once and reuse it for every frame of the video, so the fast path
  gets faster than it would in a one-shot run
- `GlyphAtlas` itself, both match and render atlases

**Depends only on grid/plane size — also constant for the entire video:**

- the plane `Image`, every filter scratch buffer, the `CellBuffer`, the render `Image`
- the `ContrastField` *buffer* (its contents are per-frame, its storage is not)
- the Bayer matrix, which is a compile-time constant already

What genuinely cannot be cached: anything derived from pixels. The resample, the filters, the
contrast field's values, the tile descriptors, and the scoring itself.

This tier split is worth building the video path around from the start — a `Session` holding
tier-1 and tier-2, and a `frame()` call that allocates nothing.

---

## Then multithreading

Deliberately last. It is a flat ~6× on 8 cores whatever it is given, so it multiplies
whatever the algorithmic work leaves behind — and parallelising code that S1 is about to
rewrite is wasted effort, since GEMM changes what the parallel unit even is.

Frame-parallel (or chunk-parallel, if temporal coherence lands) beats intra-frame: no
barriers, one thread owns a frame's buffers end to end. If you go that way, **do not also
parallelise the cell loop** — they contend.

Rough projection, per frame at this 457×100 grid:

| | per frame | fps |
|---|---|---|
| now | ~742 ms | 1.35 |
| threads only | ~124 ms | 8 |
| this page only, 1 thread | ~356 ms | 2.8 |
| **both** | **~59 ms** | **~17** |

At `--grid-height 60` — 16,400 cells instead of 45,700 — the same work lands comfortably past
30 fps.

### One caveat on exactness

S1 and S2 preserve the winner mathematically, but vectorised accumulation reorders float
adds, and both selectors break ties within `1e-4`:

```cpp
score > bestScore + 1e-4f || (score > bestScore - 1e-4f && ink < bestInk)
```

A near-tie could flip. For bit-identical output, keep the *final* comparison among
shortlisted candidates in the current scalar order — the shortlist itself can be as fast as
you like, and it is small by construction.
