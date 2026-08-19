# asciigen — Roadmap

> Companion to `inputs.md`. That file specs the CLI surface; this one specs *what to
> build next and why*, grounded in the actual code that exists right now.

---

## 1. Where things stand

| Piece | Status | Lives in |
|---|---|---|
| Image decode (stb_image) | done | `file_management/ImageManager` |
| `.ans` file save | done | `file_management/OutputManager` |
| `Image`, `PixelColor`, `getAt()` | done | `core/Image.hpp` |
| `CellBuffer`, `Cell` (encapsulated, `setSize()`) | done | `core/CellBuffer.hpp` |
| `Charset` (u32string + ASCII-string ctor) | done | `core/Charset.hpp` |
| `RGB`, `GlyphColor`, hue-based ANSI16 matching | done | `core/Color.hpp` |
| `AnsiRenderer` (truecolor / ansi16 / none) | done | `output/AnsiRenderer` |
| `Terminal` (VT mode, UTF-8, size, isTty) | done | `output/Terminal` |
| `Ramp` algorithm | done | `bitmap/Ramp` |
| Value-boost for color (glyph carries brightness, color carries hue) | discussed, not built | `bitmap/Ramp` (upstream of `toGlyphColor`) |
| Glyph rasterization / font atlas (FreeType) | done | `font/Font`, `font/GlyphAtlas` |
| Resample stage (subcell resolution) | done | `bitmap/Resample` |
| `bitmask` algorithm (correlation + coverage match) | done | `bitmap/Bitmask` |
| Ordered (Bayer) dithering | done | `dithering/Bayer4` |
| Contrast-adaptive dither amplitude | done | `dithering/BlockContrast` |
| Edge detection (Scharr → directional glyph overlay) | done, default off | `edges/` |
| Reconstruction quality metric (SSIM / RMSE / PSNR) | done | `quality/` |
| `feature` / `gradient` / `template` / `dct` / `ssim` selectors | **not started** | see §9 |
| argparser → Options → dispatch | in progress, by you | `app/` |

Everything below builds on this, in the order I'd actually do it.

---

## 2. The one thing that changes shape as you go: does the charset need to be ordered?

**Only for `ramp`.** That's the whole reason it was the right first algorithm — it needed
nothing that didn't already exist. It works on exactly one number per cell (mean luma) and
steps through the charset string as a lookup table, so *position in the string* has to mean
*brightness rank*.

Every other algorithm answers a different question — "which glyph's own shape best matches
this cell's pixels" — and to answer that they need to know what each glyph actually **looks
like**: a rasterized bitmap. Charset order is irrelevant to all of them; a `Charset` for
`bitmask` or `feature` can list glyphs in any order at all.

That's the pivot point for the whole roadmap: **`ramp` is the only algorithm that runs on
`Charset` alone. Everything else needs a font atlas first.** So the next investment isn't
"algorithm #2" directly — it's the rasterizer that unlocks *all* the remaining algorithms at
once.

---

## 3. Foundational infrastructure (build once, reused by every remaining algorithm)

### 3.1 Glyph atlas (`font_loading/`)

Rasterize every codepoint in a `Charset` via `stb_truetype` into a fixed-size bitmap
(8×8 to start — a `uint64_t` mask, one bit per pixel, `popcount` is one instruction).
For each glyph, precompute:

- the coverage bitmap itself (for `template`/`ssim`)
- a binary ink/paper mask (for `bitmask`)
- a subcell feature vector — mean coverage per quadrant or per 2×4 grid (for `feature`/`gradient`)
- ink pixel count (`popcount` of the mask) — used by `bitmask`'s scoring shortcut

One rasterization pass serves every algorithm below. This is the single biggest unlock in
the whole roadmap.

### 3.2 Resample stage

`Ramp` reads pixels directly out of `Image` and box-averages per cell because it only ever
needs *one* number per cell. `bitmask`/`feature`/`template` need to know **where** the light
and dark pixels sit *within* a cell, so they need a real subcell grid: resample the source
image up to `cols × cellW` by `rows × cellH` pixels (matching the atlas's rasterization
size) before selection runs. This is the "so the selector sees subcell detail" step from
the original design notes — skip it and every glyph will score identically, because there's
no spatial structure left to match against.

---

## 4. The remaining glyph-selection algorithms

| Algorithm | Picks a glyph by... | Needs | Looks like | Cost |
|---|---|---|---|---|
| `bitmask` | best-matching ink/paper split against the cell's actual pixels, fg/bg solved jointly | atlas + resample | the "real ASCII art" look — chafa-style, high apparent detail, best with color | ★★ |
| `feature` | nearest glyph by subcell mean-coverage vector | atlas + resample | structural, brush-stroke-like, strong general-purpose photo quality | ★★ |
| `gradient` | `feature`, plus dx/dy moments so orientation matches, not just coverage | atlas + resample | sharper than `feature` on diagonal edges and fine lines | ★★ |
| `template` | full per-pixel SSD against every glyph bitmap | atlas + resample | closest to "the font is literally drawing the image" — ground truth, not meant to be fast | ★★★★ |
| `dct` | match low-frequency DCT coefficients | atlas + resample | close to `feature`, noticeably better on texture (fabric, grass, noise) | ★★★ |
| `ssim` | maximize structural similarity per cell | atlas + resample | perceptually the strongest match, slowest | ★★★★ |
| `pixel` | no matching at all — packs subcell samples directly into a block/braille bit pattern | resample only, no atlas | not "art," a small direct image — max fidelity, zero character | ★ |
| `edge` | gradient direction, not brightness | its own Sobel/DoG pass | pure line art, sketch/outline look | ★★ |

Build `bitmask` first — it was the original target, it's the one that needed a font atlas
to even start, and everything it needs (Charset, CellBuffer, RGB, atlas from §3.1) already
exists once §3 is done. `feature`/`gradient` are the natural second pick: same atlas, same
resample step, different scoring — genuinely incremental once `bitmask` proves the atlas
plumbing works. `template`/`dct`/`ssim` are polish, worth doing once you want a quality
ceiling to compare the cheaper ones against, not before. `pixel` is nearly free once
resample exists (it's `bitmask`'s math with the search removed). `edge` can happen any time
after §3.2 — it doesn't depend on any *algorithm*, only on the resample stage existing.

---

## 5. Dithering

Not a new algorithm — a modifier that layers onto whichever algorithm is already running,
by nudging the pixel values *before* they're quantized/matched. This only makes sense once
there's at least one real algorithm to layer it onto, which is why it comes after §4, not
before.

Two families, and they don't cost the same to build:

- **Ordered (Bayer)** — add a fixed threshold-matrix value to each pixel before matching.
  Stateless, trivially parallel, ~30 lines including the matrix table. **Build this one
  first.**
- **Error diffusion (Floyd-Steinberg, etc.)** — quantize, measure the residual, push it onto
  unvisited neighbors. Better quality, but sequential (kills parallelism) and shimmers
  across video frames. Build once ordered dithering is working and you want the extra
  quality for stills.

Both operate purely on the pixel plane and never touch a glyph ID — they don't know or care
which algorithm is running above them.

---

## 6. Edge detection

Also a modifier, also algorithm-agnostic, but it operates in three stages instead of one:

1. **Detect** — Sobel/Scharr (cheap) or DoG/XDoG (stylized) on the resampled luma plane.
   Produces a magnitude + direction field, full resolution.
2. **Reduce to cells** — magnitude: max over the cell's pixels. Direction: a
   magnitude-weighted histogram over 4–8 buckets, **not** a plain average (angles wrap,
   0° and 179° are nearly the same edge, and averaging them gives 90° — perpendicular to
   both).
3. **Apply** — where magnitude clears a threshold, replace the cell's glyph with a
   directional one (`|`, `/`, `-`, `\`); the gradient is *perpendicular* to the edge, so the
   direction-to-glyph mapping needs a 90° rotation, and image-space y grows downward so `/`
   and `\` end up swapped from what you'd expect on paper — verify against an actual
   diagonal test image rather than reasoning it out.

Can be built any time after §3.2. Doesn't need `bitmask` or any particular algorithm to
exist first — it only needs a resampled luma plane and a `CellBuffer` to write glyph
overrides into.

---

## 7. The app layer

This is the biggest remaining chunk of *code*, even though it's conceptually the least new
*idea* — `inputs.md` already specs every flag and `inputs.md §24` already has the `Options`
struct field tables. What's left is wiring, in this order:

1. **Finish the argparser** — you're already on this. Output is a fully-populated `Options`.
2. **Resolve, not just parse** — `--color auto`, terminal size when `--width` is unset,
   `--format auto` from the output extension. This is a separate step from parsing; keep it
   that way, it's what `inputs.md §3` calls out as "resolution" happening after flags are
   read.
3. **Dispatch table** — `Options.algo.name` (an enum) → construct the right selector.
   With one algorithm this is a single `if`; **don't build a name→factory registry until
   there are at least 2–3 real cases** — it buys nothing before that and is easy to bolt on
   later without touching call sites.
4. **Replace `main.cpp`'s hardcoded pipeline** with the dispatch-driven version: decode →
   (filters — not built yet, skip) → resample → dither (if requested) → select → edge
   overlay (if requested) → colorize → sink.
5. **Validation** — `inputs.md §20`'s combination table (warn/error/ignore) becomes real
   code here, once there's enough surface area (charset × algorithm × color × dither) for
   any of those combinations to actually be reachable.

Presets, `--list`, `--help <topic>` generation, and multiple sinks (HTML/SVG/PNG) all sit on
top of this and aren't blocking anything — they're straightforward once the dispatch table
exists, and premature before it.

---

## 8. Suggested order, as a checklist

1. Font atlas (`font_loading/`) — rasterize a `Charset` into per-glyph bitmaps + masks
2. Resample stage — `Image` → subcell-resolution grid
3. `bitmask` algorithm
4. Ordered (Bayer) dithering
5. Edge detection (Sobel/Scharr direction → glyph overlay)
6. `feature` / `gradient` algorithms
7. App layer: dispatch table, pipeline wiring, replace hardcoded `main.cpp`
8. Error-diffusion dithering, `template`/`dct`/`ssim`, more charsets (braille/blocks),
   more sinks (HTML/SVG/PNG), video decode

Steps 1–6 are all inside `asciigen` and testable the same way `Ramp` has been — by hand, from
`main.cpp`, no CLI required. Step 7 is what turns the project into an actual command-line
tool instead of a library you drive by editing `main.cpp`.

---

## 9. Next up: a better shape matcher

Reference for all of this: **Xu, Zhang & Wong, "Structure-based ASCII Art" (SIGGRAPH Asia
2010)**. Its finding is that humans match glyph to tile *tolerantly* — small misalignment
doesn't break the match for a reader, but it does break every per-pixel metric. That's the
gap between what `bitmask` scores and what your eye scores.

Three known deficiencies in the current `Bitmask` scoring, in order of how much they cost:

1. **No misalignment tolerance.** `corr` is a per-pixel dot product. A diagonal sitting 2px
   off from where `/`'s stroke sits scores as a miss, even though `/` is plainly the right
   glyph. Glyphs land on a rigid grid; image features don't.
2. **Uniform pixel weighting.** Side bearings and leading make cell borders systematically
   emptier than centers, but every pixel is treated as equally informative.
3. **Single scale.** Full-resolution correlation is dominated by fine detail, with no notion
   of "roughly the right shape, roughly the right place" to reward first.

### TODO 1 — Reconstruction quality metric ✅ done

`quality/`. Composites chosen glyph masks back to pixels (`Reconstruct`) and scores against
the undithered resampled source: SSIM, luma/colour RMSE, PSNR. Prerequisite for everything
below — "which selector is best" is not answerable by eye across five of them.

**Known blind spot: it cannot score dithering.** Dither trades local per-cell accuracy for
tonal range perceived at a distance, so a local metric marks it down every time (measured:
SSIM 0.46 → 0.43). Use it to compare *selectors*, never to tune dither. Fixing that needs a
second pass at a blurred/downscaled resolution — the "squint test" — which isn't built.

### TODO 2 — Misalignment tolerance in `bitmask` ⚠️ built, measured, does not help

`BitmaskOptions::softness` (0–1) mixes a blurred correlation into the sharp one;
`blurRadius` sets how far a stroke may drift. Blurred glyph twins are precomputed once per
call, the tile is blurred to match, and `Blur::box` (`bitmap/Blur`) is the separable kernel.
`softness = 0` is bit-identical to the original scoring — verified, same metric to 4dp.

**Measured on the Porsche, dither off, so only the selector moves:**

| softness | ssim (r=1) | ssim (r=2) | ssim (blocks, r=1) |
|---|---|---|---|
| 0.0 | **0.4598** | **0.4598** | **0.5550** |
| 0.3 | 0.4587 | 0.4569 | 0.5549 |
| 0.5 | 0.4579 | 0.4554 | 0.5543 |
| 1.0 | 0.4571 | 0.4530 | 0.5545 |

Monotonically worse, on both charsets, at both radii — and worse faster at the larger
radius, which confirms the knob is live rather than inert. The ASCII-vocabulary hypothesis
is disproven too: blocks has a denser vocabulary and shows the same sign, just flatter.

Caveat worth keeping: the metric compares a *sharp* reconstruction against a *sharp* source,
so it is structurally somewhat unkind to a matcher optimising for blurred agreement. But the
effect is consistent and negative, and there is no positive evidence on the other side. Left
in at `softness = 0` — free to keep, available to eyeball, not on by default.

The lesson for TODO 3: pixel-space blurring is not what "misalignment tolerance" means in
the Xu paper. They deform the *structure*, not the intensities. Getting the tolerance from
descriptor binning instead is a genuinely different mechanism, not a stronger version of
this one — so this result does not predict that one.

### TODO 3 — `feature`: descriptor matching

The real answer, and a genuinely different matcher rather than a tweak. Per tile and per
glyph build a HOG-style descriptor — 2×4 subcell blocks × 4 orientation bins,
magnitude-weighted — and compare by cosine similarity. Misalignment tolerance is structural
here: binning discards exact position by construction, and orientation is matched explicitly
instead of emerging as a side effect of intensity correlation. This merges what §4 lists
separately as `feature` and `gradient`.

`ssim` from §4 is the ceiling to measure these against once they exist, not a shipping
selector — it is far too slow for video.

**Caveat on "best":** with ASCII the glyph vocabulary is often the bottleneck, not the
metric — no scoring function can produce a shape the charset doesn't contain. These upgrades
should show up much more strongly on blocks and braille, where the vocabulary covers the
space densely. Measure on both, not on ASCII alone.
