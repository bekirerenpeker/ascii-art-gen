# Optimizations

Working list. Anything marked *estimate* is reasoning from a trace, not a measurement —
a reason to try, not a promise. Measured numbers say where they came from.

Traces come from a build with `ASCIIGEN_PROFILING=ON` (the default); `--profile PATH` writes
one. Open it at [ui.perfetto.dev](https://ui.perfetto.dev) or
[speedscope.app](https://speedscope.app).

**Rule for everything below: the picture must not change *unreviewed*.** Every item is either
arithmetically identical or exactly equivalent, unless explicitly marked otherwise and its
quality cost measured before being enabled. As of item 7, two deliberate exceptions exist:
el-3 stays opt-in (`--algo-structure-gradient-stride`, default `1`, no change at that value) —
its 9.6% cell-level change was judged too large to default to. **el-4 (`--algo-structure-fast-atan`)
is on by default as of this update** — its 0.14% cell-level change, next to a ~40% cut on
`buildDescriptor`, was judged small enough to take; `--no-algo-structure-fast-atan` still gives
the exact `atan2`+`fmod` path for anyone who wants it back. **Item 9's flat-tile path
(`--algo-structure-flat-threshold`, default `0.02`) is the third** — a measured ~0.8%
cell-level change, on by default, `-1` (or any negative value) opts back out to the exact scan.

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

**4. S1 + S2 for Structure: packed glyph matrices, scored via a Cauchy-Schwarz shortlist.**
See `output/benchmarks/benchmark.md` — `0-baseline` vs `1-s1-s2-gemm`, same fixed scene
(`--preset photo --grid-height 100 --dither bayer4` on the 1920×840 porsche source).
`Structure::generate` **358.5 → 254.2 ms (1.41×)**; whole run 646.3 → 529.2 ms in-process,
737.2 → 626.4 ms wall. Verified bit-identical output across 20+ configurations (grid sizes
from 8 to 300 wide, every preset that uses Structure, non-default `--algo-structure-*-blocks`
down to the smallest possible shapes, a forced negative weight to exercise the exact-scan
fallback, zero weights, braille and ascii charsets).

Lower than the doc's original 10–20× estimate for S1 alone, and that's expected: this only
sped up *scoring* (`pickGlyph`'s glyph loop), not *building* each cell's own descriptor
(`buildDescriptor`, called once per cell for the tile side) — that's T1 below, the ~157 ms
fixed cost, still untouched. **T1 is now the next highest-value item**, not S1/S2's own
follow-through; there's nothing more to extract from scoring alone at this charset size
without T1 first.

One real bug surfaced during verification, worth recording: the first `residualNorm` used
Parseval's `sqrt(1 - Σcoeffs²)` shortcut, which is only exact for a *perfectly* orthonormal
basis. Power iteration only gets close, and in a small, exactly full-rank subspace
(`--algo-structure-orient-blocks 1x1 --algo-structure-bins 8`, an 8-dimensional space with
`k=8`) the drift was enough to occasionally prune the true winner — caught by the byte-diff
battery, not by inspection. Fixed by computing the residual as a direct reconstruction error
(`‖v − P(b)‖`, measured, not inferred from Σcoeffs²) instead, which is correct for *any*
basis, not just an orthonormal one — see `DescriptorBasis.cpp`.

**5. S3 + S4: row-walk `BlockContrast`, two reused buffers in `unsharpMask`.**
Same fixed scene, `1-s1-s2-gemm` vs `2-s3-s4` in `output/benchmarks/benchmark.md`.
`unsharpMask` 158.2 → 132.3 ms (1.20×), `Dithering::apply` 43.6 → 37.8 ms (1.15×); whole run
529.2 → 487.3 ms in-process, 626.4 → 561.4 ms wall. Both smaller than their *estimate* tags
below said, same pattern as S1/S2 and #3 before them — the fixed/overhead share of these
stages is bigger than the doc assumed. Verified bit-identical across the earlier battery plus
6 more cases aimed at these two specifically: adaptive dither off, several sharpen radii, a
grid exactly filling the plane so every `BlockContrast` block is interior, one deliberately
not (a 6-wide grid, so most blocks clamp), a grey-source sharpen, and a large external source.

`unsharpMask`'s grey-image path also got simpler, not just less allocate-y: the old code
sharpened r, g and b separately even though a grey pixel's three "channels" were always
identical bytes, then recombined them with a luma reweight. Since equal inputs make equal
outputs, that reweight was always reconstructing the same value it started with — one pass
gets the same byte, done directly instead of proven identical after the fact.

**6. T1 + T2 for `buildDescriptor`'s gradient.** Same fixed scene, `2-s3-s4` vs `3-t1-t2`.
Result: **flat, within noise** — `Structure::generate` 250.4 → ~250–257 ms across repeated
runs, no consistent improvement either way. Verified bit-identical output across the full
battery plus atlas-size edge cases (`--font-render-size` 8 through 64, where the interior
region shrinks to a couple of pixels wide or vanishes) and the smallest possible descriptor
shapes.

Correct, and not wasted (T2's allocation hoist is a pure win regardless, and the clamp
removal is real), but the *reasoning* behind T1's "best value in this section" tag was wrong.
`std::clamp` on an already-in-range value compiles to a couple of branchless comparisons —
cheap, not the 24-clamps-per-pixel cost the doc assumed. Rough arithmetic on the untouched
part points at the real cost instead: `buildDescriptor` calls `std::atan2` and `std::fmod`
once per pixel with nonzero gradient, 5.85M times a frame — at even 20 ns each (a
conservative estimate for a transcendental), that alone is in the same ballpark as the whole
~157 ms fixed cost T1 was supposed to be "the larger part of." **The fixed cost was never
mostly clamps — it was mostly `atan2`/`fmod`.** Filed below as a new lead, not yet attempted.

This did not reopen T3 under the rule active when this was written — T3 rejected approximating
`atan2` because the soft bin split needs the true sub-bin position, and a faster-but-approximate
`pos` would still shift `b0`/`b1`/`frac` on real image data. That rule was "the picture must not
change, ever, unconditionally." **It has since been relaxed, deliberately and only as an opt-in,
default-off flag** — see item 7 and el-4 below. T3's own reasoning was never wrong; the
constraint it was reasoning under was loosened after it was written.

**7. Four Structure ideas from a live-trace breakdown of `pickGlyph`.** A finer trace (see
"How this was traced" below) found `pickGlyph` — after S1 *and* S2 — costing more than
`buildDescriptor`: 85.3 ms vs 58.9 ms on the fixed scene, with S2 already pruning 83.4% of
glyph checks (719,844 / 4,341,500 exact evals). The bound-*check* itself, paid on all 4.34M,
turned out to be the new bottleneck, not the exact evaluations it was guarding. Four things
followed from that, done in order, each independently verified and each with its own trace +
rendered image in `output/benchmarks/`:

- **el-1 — a cheaper cascade tier before S2's real bound** (`4-el1-cascade`). A second,
  looser Cauchy-Schwarz bound using only the first 2 of the existing basis components,
  reusing the same projected coefficients (see `DescriptorBasis.hpp`'s note on `residualNorm`'s
  `kUse`) — provably exact, same guarantee as S2 itself, no flag needed. **Measured: a wash.**
  86–89 ms, barely different from (arguably slightly worse than) the 85.3 ms it started from —
  pruned 159 fewer glyphs out of 4.34M, i.e. essentially nothing. Kept in the codebase (it is
  free-riding on el-2 below, see next point) but on its own this did not pay for itself.

- **el-2 — retuned S2 basis size** (`5-el2-basis-4-8`). Swept k from 8/16 (original) down to
  4/8, 2/4 and 1/2. **Measured: real, and the direction was the opposite of what "tighter bound
  = better" would predict.** k=8/16: 719,685 exact evals (16.6%), 85–89 ms. k=4/8: 932,763
  exact evals (21.5%, *more* fall through) yet only 66–68 ms. k=2/4 and k=1/2 landed in the
  same 65–75 ms band, noisier, no further win. Settled on **4/8**: a plateau, not a single
  sharp optimum, and going smaller than that bought nothing more. The lesson: the per-glyph
  bound-*check* cost (paid 4.34M times) dominated the exact-eval cost (paid 720K–1.1M times)
  enough that a cheaper-but-looser bound won outright. `pickGlyph`: 85.3 → ~66 ms.

- **el-3 — `--algo-structure-gradient-stride N`** (`6-el3-stride2`, opt-in, default `1`).
  Skips the gradient (not mass) on non-stride-aligned interior pixels. **Measured** at stride
  2: `buildDescriptor` 67.9 → 41.5 ms. **Visual cost, measured on the fixed scene: 4,367 of
  45,700 cells (9.6%) picked a different glyph** — the largest change of the four, and one to
  actually look at before trusting, not assume is unnoticeable.

- **el-4 — `--algo-structure-fast-atan`** (`7-el4-fastatan`). Swaps `buildDescriptor`'s
  `std::atan2`+`std::fmod` for a minimax polynomial approximation (see `Descriptor.cpp`'s
  `fastFoldedAngle`), accurate to a fraction of a degree against bins that are 45 degrees wide.
  **Measured: `buildDescriptor` 67.9 → 38.5 ms (~43% off), and only 64 of 45,700 cells (0.14%)
  changed glyph.** Best value-for-quality-cost of the four by a wide margin — biggest
  single-stage cut, smallest visible footprint. **Promoted to the default** after this was
  written — see the callout at the top of this document. `--no-algo-structure-fast-atan` opts
  back out.

el-1 through el-4 verified bit-identical to the pre-el-1 baseline **at el-4's original default**
(`stride=1`, `fast-atan` off, i.e. today's `--no-algo-structure-fast-atan`) across the full
battery. el-3 and el-4 were also checked for what they *do* change: rendered to PNG
(`output/benchmarks/images/`) and diffed character-by-character against the same scene's
plain-text output, which is where the 9.6%/0.14% figures above come from.

**How this was traced.** `buildDescriptor`, `pickGlyph`, `tile gather` and `solveCellColor` each
got their own `ASCIIGEN_PROFILE` scope (call-granularity — once per cell, ~45,700 times a frame
— never per-pixel; a per-pixel scope's own mutex-locked trace write would dwarf the ~20 ns cost
of the thing being measured). `pickGlyph` also gained a free counter, reported once via
`Profiler::describe` rather than per-call, for how many glyphs its exact score actually got
computed for versus how many S2's bound pruned. This is as fine as the trace can safely go —
for a true per-primitive breakdown (`atan2` alone vs `fmod` alone vs the comparison), the right
tool is a sampling profiler (Visual Studio's CPU Usage tool, Windows Performance Analyzer)
against a build with debug info, not more scopes here.

**8. `applyBayer4`: raw pointer + hoisted per-block bias.** Same fixed scene, `7-el4-fastatan`
vs `8-dither-blockskip`. `Dithering::apply` 41.9 → 27.7 ms (**1.51×**), found the same way as
item 7's leads: reading `Bayer4.cpp` directly rather than trusting the trace alone. It had never
gotten the `getAt`/`setAt` → raw-pointer treatment #3 gave everything else, running over the
full plane (5.85M pixels) every frame.

The bigger find while reading it: `kBayer[blockY & 3][blockX & 3]` is indexed by *block*
coordinates, not pixel ones — the bias was already constant across an entire block, the old
per-pixel loop was just recomputing an identical value up to `blockW * blockH` times (128, at
the default cell size). Restructured blocks-outer, pixels-inner: `gate.at()` and `bias` computed
once per block, and a gated-off block (`amplitude <= 0`, common on flat/saturated regions --
see `BlockContrast.cpp`) now skips its entire run of pixels in one branch instead of one
`continue` per pixel. Exact — every pixel is written independently, nothing here accumulates
across pixels, so reordering the traversal from row-major to block-major changes nothing about
the result. Verified bit-identical across the full battery plus 10 dither-focused cases: adaptive
on and off, `--algo ramp`'s 1x1 blocks, 2 and 8 quantisation levels, tiny and wide grids, a large
external source, and Bitmask.

Also answers a question from the same investigation: adaptive dithering measured *faster* than
`--dither-adaptive false`, not slower, despite doing strictly more work (`computeBlockContrast`
on top of `applyBayer4`) — the flatness/room gate's `amplitude <= 0` early-out was already
skipping the expensive per-pixel path on top of being a quality feature. `adaptive` already
defaults to `true` at the engine level, so this needed no code change, only confirming the
default was already the right one.

**9. `pickGlyph`'s flat-tile path: binary search by ink weight + an off-centre tie-break.**
On a tile whose `|orientW|` and `|massW|` both fall at or under `--algo-structure-flat-threshold`
(default `0.02`), the shape terms are bounded to matter so little that tone alone effectively
picks the winner — and tone is a smooth, single-peaked function of ink weight, so the winner is
findable by binary search instead of a 95-glyph scan. Within the resulting candidate window, a
new term breaks ties: `-0.5 * offCenter[g]`, where `offCenter` is each glyph's own ink centroid
distance from the cell's geometric centre, precomputed once per glyph in `buildGlyphModel`
(same pass as `inkWeight`, no extra cost). Deliberately *not* used outside this path — Structure
already splits the mass term into a fine per-pixel grid (128 blocks at the default cell size),
which is a far richer positional match than one non-directional scalar could add; on a textured
tile this would be redundant at best.

The candidate window is **derived, not guessed**: every dot product involved is between vectors
of norm ≤ 1, so by Cauchy-Schwarz the shape terms' contribution to any glyph's score is bounded
by `|orientW| + |massW|`. Any glyph whose tone term alone is more than twice that below the
*nearest* candidate's own tone term provably cannot reach the tie threshold — see
`pickGlyphFlat`'s own comment for the full derivation. `--algo-structure-flat-threshold -1`
(or any negative value) disables the path entirely, since `std::fabs(...) <= negative` is never
true — verified bit-identical to the pre-item-9 baseline at that setting, across the full
battery.

**Measured, and the result is a genuine mixed bag, reported as such:**

- **Adoption is much higher than expected**: 46.7% of cells on the fixed scene (21,334 / 45,700)
  take the flat path — this photo has a lot of smooth/flat area (sky, panels, out-of-focus
  background).
- **Visual effect is small and targeted, as intended**: 374 of 45,700 cells (0.82%) changed
  glyph relative to the path disabled — the off-centre tie-break doing its job on a modest
  slice of tiles, not a wholesale change.
- **`pickGlyph`'s total time did not move**: 64–68 ms before and after, within the same noise
  band as `5-el2-basis-4-8`/`8-dither-blockskip`. The reason: the *provably safe* window ends up
  about as wide as the number of candidates S1/S2's bound was already narrowing down to on these
  same tiles (tens of glyphs, not the full 95, but not a small handful either), and every
  candidate in the window is scored with the *full* exact dot products, not a cheap bound-check
  first. Net effect: comparable total work, redirected through a different code path, not less
  work.

**Follow-up, tried**: composed the same el-1/S2 cascade bound *inside* the window instead of
scoring every window candidate exactly — bound-check each one first, only pay for the real
160-wide dot product on what passes. Verified to still pick the exact same glyph as the
unbounded version, on the same battery: bit-identical output, same winners, cheaper route.
**Also came back flat** — `pickGlyph` still 65–68 ms — but this time for a reason that actually
explains *both* results, not just a second "didn't help":

The cascade bound prunes on *shape* similarity. Inside the flat-path window, `orientW` and
`massW` are both small **by definition** — that is the entire premise of being in this path at
all. So the bound's own value, `orientW * ubOri + massW * ubMass + toneTerm`, ends up almost
entirely determined by `toneTerm` regardless of a candidate's actual shape — the shape terms are
scaled down to near-nothing by the same tiny weights that put the tile here in the first place.
A shape-based bound has very little left to reject once shape itself barely matters; it is not
an independent axis of pruning on these tiles, it is approximately re-deriving the same
tone-based "close enough" answer the window already computed, at extra cost for the privilege.

**The actual conclusion**: on a genuinely flat tile there is only one axis anything can
discriminate on — tone — and the analytically-derived window already sits close to the
tightest that axis allows without risking correctness. `pickGlyph`'s flat-tile cost is not
being held up by a missing optimization; it is close to the honest floor for "search by tone,
safely." The only remaining lever is the threshold itself: a smaller `flatThreshold` shrinks
`budget` and therefore the window (real, direct, but fewer tiles qualify at all) — a tuning
knob, not another restructuring.

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

**S1. Reformulate scoring as a matrix multiply.** *(done for Structure — item 4)*
Both selectors reduce to the same kernel: a dot product of a 128- or 160-dimensional tile
vector against a glyph matrix that never changes. Bitmask still inherits this unchanged
(see "Bitmask inherits S1, S2, S3, S4" below) — B2 is what makes it apply there too.

- Structure: `similarity()` (`Descriptor.cpp:118`) is a plain dot product of vectors that
  `centreAndNormalize` already made zero-mean and unit-length. The weights `orientW`/`massW`
  are per-*tile* constants, so they factor out of the glyph loop entirely.
- Bitmask: the inner `lw += tileLuma[i] * (mask[i]/255)` is the same thing against the mask
  matrix, and the `allowBackground` branch needs the identical product (`paperL = sumL − inkL`).

Currently: 45,700 separate dot products with a branch in the loop, ~3.3 GMAC/s. As a blocked
GEMM with FMA, 10–20× on the same arithmetic. *Estimate: Structure scoring 208 → ~25 ms;
Bitmask similar.*

**S2. Exact shortlist via a projection bound.** *(done for Structure — item 4)*
Composes with S1. Project glyph descriptors onto their top-k principal components once. For
a unit-length tile vector `a` and glyph `b = P(b) + r(b)`, Cauchy–Schwarz gives
`|a·r(b)| ≤ ‖r(b)‖`, so `a·P(b) + ‖r(b)‖` is a true upper bound on the real score. Evaluate
exactly only the glyphs whose bound beats the best exact score so far.

Implemented via power iteration for the top-k subspace (`DescriptorBasis.hpp/.cpp`), not a
real eigensolver — deliberately: any orthonormal-ish basis gives a valid bound, a worse one
only prunes less, so there was nothing to gain from an exact one. `k=8` for the 32-wide
orientation term, `k=16` for the 128-wide mass term.

Provably the same winner. Typically skips 80–90% of candidates, and the bigger the charset
the more it skips — braille (256 glyphs) benefits most.

**S3. `Dithering::apply` still uses `getAt`.** *(done — item 5)* Missed in #3.
`BlockContrast.cpp` reads its contrast window through `plane.getAt(x, y)` with two clamps per
sample — 5.85M of them per frame, which is most of the 44.6 ms. The window is a fixed
rectangle inside a known-size plane, so it can be a clipped row walk. Straightforward and
exact. *Estimate: 44.6 → ~15 ms.*

**S4. Fix `unsharpMask`'s allocations.** *(done — item 5)* It allocates six `n`-float
vectors — **140 MB** at this plane size — and is memory-bound rather than compute-bound. Two
reused buffers, or fusing the gather with the first blur pass. *Estimate: 165 → ~55 ms.*

**Already shared and already done:** `Resample::toGrid`, the tile gather in both selectors,
and `ImageRenderer::render`/`compose` all got the pointer treatment in #3, so both selectors
already benefit. `solveCellColor` is shared and is called once per cell, not per glyph — it
is not hot.

---

## Structure only

**T1. Kill the clamps in the gradient.** *(done — item 6)* Turned out not to be the best
value in this section after all — see item 6's writeup. `buildDescriptor` calls `at()` **12
times per pixel**, and every `at()` does two `std::clamp`s — 24 clamps and 12 bounds-checked
loads per pixel, over 5.85M pixels. The interior of an 8×16 cell is 6×14, so **74% of pixels
need no clamping at all**. Split interior from border; the redundant 4-of-12 re-fetch between
`gx` and `gy` is also gone. Exactly output-preserving. *Estimate: the larger part of the
157 ms fixed cost — measured: no consistent change.*

**T2. Hoist the per-cell allocation out of `buildDescriptor`.** *(done — item 6)*
`std::vector<int> counts(mx*my)` is constructed **inside** the function, so it allocates once
per cell — 45,700 mallocs per frame. Passed in as a caller-owned scratch buffer instead, reused
across every glyph and every cell.

**T3. Leave `atan2` alone.** *(supersede — see item 7 / el-4)* Previously listed as a win; it
is not, *if quality must hold unconditionally*. The soft binning needs the true angle, not just
the bin index, so any fast approximation shifts the histogram — that much is still correct.
What changed is the constraint: once approximation was allowed as an explicit, reviewed opt-in
rather than banned outright, exactly the approximation this entry warned against became
`--algo-structure-fast-atan` (el-4) — the best-value item of the whole set, since promoted to
the default (`--no-algo-structure-fast-atan` opts back out). T3 was right about the
unconditional case; it just stopped being the only case that matters.

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

## Filters — ideas, not yet attempted

`unsharpMask` is not an engine default (`SourceOptions::sharpenAmount` is `0.f`) — it is baked
into `Presets::photo()` (`sharpenAmount = 0.6f`), and nearly every preset (`nord`, `gruvbox`,
`terminal`, both `wallpaper-*`, `braille`) calls `photo()` as its shared base, which is why it
runs on almost everything in practice. What it does: blur a copy, then add back
`amount × (original − blurred)`. Blurring destroys fine detail; the difference *is* that
detail, so adding it back sharpens. It exists because resampling the source down to plane
resolution is itself a blur, and Structure/Bitmask need clean edges to read orientation from —
sharpening beforehand counteracts the loss the resample is about to cause.

**F1. Finish "the plane is upsampled 3.6×" (above), specifically for this filter.** Still
pending. `unsharpMask` runs on the plane (5.85MPix) when the source (1.6MPix) is smaller — the
biggest single lever available for this filter specifically, proportional to the pixel-count
cut. Same caveat as before: filter-radius semantics shift depending on which side they land on.

**F2. Hoist `blur()`'s own internal `tmp` buffer.** Same category as T2 — `unsharpMask`'s 3
calls into `ImageFilters::blur(float*, float*, w, h, radius)` each still allocate their own
scratch internally, even after S4 fixed `unsharpMask`'s own 6→2 buffers. Exact, low risk, low
reward (source filters run once a frame, not per-cell — this matters far more for video).

**F3. Sliding-window box sum instead of re-summing `2r+1` samples per pixel.** Only matters if
radius is commonly larger than the default 1 — at radius 1 there are only 3 samples to sum
already, so this is low priority until someone actually runs a larger radius.

**F4. A fixed 3×3 unsharp kernel instead of the two-pass separable blur, as an opt-in.**
*Changes the result* — a different kernel shape gives different sharpened output, and
different resulting glyph choices in some cells. At radius≈1 and ascii-art output granularity
this is probably a small visible difference (the coarse glyph selection already discards a lot
of fine sharpening nuance), but that is a claim to verify, not assume — same discipline as
el-3/el-4 above. Potentially the biggest win in this list: collapses ~4 full passes over the
plane into 1.

---

## General per-pixel/per-cell hygiene

Things worth checking across every hot loop, not specific to one algorithm:

- **Iteration order vs. storage layout.** Row-major, x innermost, confirmed for the plane,
  `Descriptor.cpp` and `BlockContrast.cpp` this session. `CellBuffer`'s own storage/iteration
  order has **not** been audited — worth a quick check before assuming it is fine too.
- **Redundant luma across stages.** `BlockContrast.cpp`'s dither-contrast luma and
  `Structure.cpp`'s tile-gather luma are computed independently over the same plane, at
  different sampling footprints (window-around-block-center vs. per-cell). Not obviously
  shareable without restructuring, but worth a look — if they ever land on computing luma over
  the *same* samples for different purposes, caching once is a free, exact win.
- **Manual prefetching.** Hardware prefetchers already handle the sequential/strided patterns
  in these loops well; expect a few percent at best for real added complexity. Low priority.
- **False sharing between adjacent `Cell`s.** Not relevant yet (single-threaded), but worth
  remembering once multithreading (deliberately deferred, see below) actually happens.
- **Finer-than-call-granularity profiling needs a different tool.** el-1 through el-4's own
  trace scopes (see item 7) are as fine as this codebase's mutex-locked instrumentation can
  safely go — a true per-primitive-function breakdown (`atan2` alone vs `fmod` alone) needs a
  sampling profiler (Visual Studio's CPU Usage tool, Windows Performance Analyzer) against a
  build with debug info, not more `ASCIIGEN_PROFILE` scopes.

---

## Video: stop reallocating

Nothing in the per-frame path should allocate. Today, most of it does. Every item is
per-*frame* unless noted:

| what | where | size |
|---|---|---|
| `std::vector<int> counts` | `buildDescriptor` | small × **45,700 per frame** |
| `Image plane` | `Structure::generate`, `Bitmask::generate` | 17.5 MB |
| 2 × `n` floats | `unsharpMask` (was 6×, S4) | 47 MB |
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
