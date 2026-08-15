# `asciigen` — Input & Option Specification

> Status: **design document**. Nothing here is implemented yet. This file defines the
> complete surface area of the CLI so that the implementation has a fixed target and
> so that new algorithms/charsets/sinks can be added without redesigning the interface.
>
> Binary name `asciigen` is a placeholder.

---

## 1. Design principles

Five rules that everything below follows. If a future option violates one of these,
the option is wrong, not the rule.

1. **Orthogonal axes, not modes.** There is no `--braille-mode` or `--color-ascii-mode`.
   There is a *charset* axis, an *algorithm* axis, a *color* axis, and a *sink* axis, and
   they compose freely. Named `--preset`s exist purely as sugar over combinations.

2. **One base selector, many modifiers.** `--algo` picks exactly one glyph-selection
   strategy. `--dither`, `--edge`, and `--filter` are *modifiers* that layer on top of it.
   This is what stops the option set from exploding combinatorially.

3. **Charset and algorithm are independent.** This is the key structural insight:
   half-block rendering, braille art, quadrant mosaics and chafa-style ASCII are all
   *the same algorithm* (`bitmask`) run over *different charsets*. There is no separate
   "block renderer" in the codebase.

4. **Everything is a registry.** Algorithms, charsets, dithers, edge detectors, filters
   and sinks each register a name plus a parameter schema at startup. `--list`,
   `--help <topic>` and the generic `--set ns.key=value` escape hatch are all *generated*
   from those registries. Adding a new algorithm requires **zero** changes to the CLI layer.

5. **Sensible defaults, zero required flags.** `asciigen photo.jpg` must produce good
   output with no other arguments.

### 1.1 Pipeline model

Every flag group below maps to exactly one stage:

```
  SOURCE ──▶ DECODE ──▶ PREPROCESS ──▶ RESAMPLE ──▶ SELECT ──▶ COLORIZE ──▶ RENDER
    §4        §4           §6            §5          §8,§9,§10    §11        §12,§13
   input    decoder     filter chain   geometry    algo+charset   color       sink
                                                   +dither+edge
```

The stages meet at two data types only:

- `Frame` — RGBA8 raster, produced by DECODE, consumed by RESAMPLE.
- `CellBuffer` — grid of `{ glyph_id: u16, fg: RGBA, bg: RGBA, attrs: u8 }`, produced by
  COLORIZE, consumed by every sink.

Because all sinks consume `CellBuffer`, image→HTML and video→terminal are literally the
same pipeline with different ends.

---

## 2. Synopsis

```
asciigen [OPTIONS] <INPUT>...
asciigen [OPTIONS] -i <INPUT> -o <OUTPUT>
asciigen --list <TOPIC>
asciigen --help [TOPIC]
```

```
  <INPUT>    path to image / video / directory / glob,
             "-" for stdin, "cam:N" for capture device N
```

Minimal invocations:

```sh
asciigen photo.jpg                          # auto-size to terminal, color, default algo
asciigen clip.mp4                           # decode + play in terminal
asciigen photo.jpg -o out.html              # format inferred from extension
asciigen clip.mp4 -o out.cast               # asciicast v2 recording
cat photo.png | asciigen -                  # stdin
```

---

## 3. Configuration resolution

Later sources win. Every layer is optional.

| Rank | Source | Notes |
|---:|---|---|
| 1 | Built-in defaults | Documented in the `Default` column of every table below |
| 2 | System config | `/etc/asciigen/config.toml` (POSIX only) |
| 3 | User config | `$XDG_CONFIG_HOME/asciigen/config.toml`, `%APPDATA%\asciigen\config.toml` |
| 4 | Project config | nearest `.asciigen.toml` walking up from CWD |
| 5 | `--config PATH` | explicit file |
| 6 | Environment variables | see §16 |
| 7 | `--preset NAME` | expands to a flag set at this priority |
| 8 | Command-line flags | always win |
| 9 | `--set ns.key=value` | wins over the equivalent named flag |

`--no-config` skips ranks 2–5. `--explain` prints the fully resolved configuration with
the origin of each value and exits.

### 3.1 Global flags

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-h, --help` | `[TOPIC]` | — | Help. `TOPIC` ∈ `algos`, `charsets`, `dither`, `edge`, `color`, `output`, `video`, `filters`, or any registered item name |
| `-V, --version` | — | — | Version, build flags, detected SIMD, linked codec support |
| `--list` | `TOPIC` | — | Machine-readable listing of a registry (see §17) |
| `-v, --verbose` | — | off | Repeatable: `-v` info, `-vv` debug, `-vvv` trace |
| `-q, --quiet` | — | off | Errors only. Mutually exclusive with `-v` |
| `--config` | `PATH` | — | Explicit config file |
| `--no-config` | — | off | Ignore all config files |
| `--preset` | `NAME` | — | Apply a named preset (§14). Repeatable; later presets override earlier |
| `--save-preset` | `NAME` | — | Write the resolved options to the user config as a preset, then exit |
| `--set` | `NS.KEY=VAL` | — | Repeatable generic parameter escape hatch (§17.2) |
| `--explain` | — | off | Print resolved config + pipeline graph, then exit |
| `--dry-run` | — | off | Run everything except writing output |
| `--json-errors` | — | off | Emit diagnostics as JSON on stderr |

---

## 4. Input & decoding

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-i, --input` | `PATH` | — | Explicit input. Repeatable → treated as an ordered frame sequence |
| `--input-format` | `FMT` | `auto` | Override detection: `png` `jpg` `bmp` `tga` `gif` `psd` `hdr` `mp4` `mkv` `webm` `y4m` `rawvideo` `rawimage` |
| `--raw-size` | `WxH` | — | **Required** for `rawvideo`/`rawimage` input |
| `--raw-pix-fmt` | `FMT` | `rgb24` | `gray8` `rgb24` `rgba` `bgr24` `yuv420p` |
| `--raw-fps` | `N` | `25` | Frame rate assumption for `rawvideo` |
| `--decoder` | `NAME` | `auto` | `stb` (stills), `gif` (built-in), `ffmpeg-pipe` (subprocess), `ffmpeg-lib` (linked), `camera` |
| `--ffmpeg-path` | `PATH` | `ffmpeg` | Binary used by the `ffmpeg-pipe` decoder |
| `--probe-only` | — | off | Print detected stream info and exit |
| `--frame` | `N` | — | Extract a single frame by index from a video/GIF; forces a still pipeline |
| `--frame-at` | `TS` | — | Same, by timestamp (`12.5`, `00:01:30.250`) |
| `--start` | `TS` | `0` | Seek/trim start |
| `--end` | `TS` | — | Trim end. Mutually exclusive with `--duration` |
| `--duration` | `D` | — | Trim length |
| `--in-fps` | `N` | source | Force source frame rate interpretation |
| `--frame-step` | `N` | `1` | Take every Nth decoded frame |
| `--sequence-fps` | `N` | `12` | Frame rate when input is a multi-file image sequence |
| `--camera-format` | `WxH@FPS` | auto | Requested capture mode for `cam:N` |

**Detection order:** explicit `--input-format` → file magic bytes → extension → probe via
decoder. Directories and globs expand to a sorted sequence; mixed dimensions are an error
unless `--fit` is set to something other than `exact`.

---

## 5. Output geometry & sizing

All sizes are in **character cells** unless the flag says pixels.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-w, --width` | `N` | auto | Target width in cells. Accepts `50%` |
| `-H, --height` | `N` | auto | Target height in cells. Accepts `50%` |
| `-s, --size` | `WxH` | auto | Shorthand. `W` or `H` may be `_` to mean "derive" |
| `--fit` | `MODE` | `contain` | `contain` `cover` `stretch` `exact` `width` `height` |
| `--scale` | `F` | `1.0` | Multiplier applied after fit resolution |
| `--full` | — | off | Use the entire terminal viewport, ignoring `--max-*` |
| `--max-width` | `N` | terminal | Upper clamp |
| `--max-height` | `N` | terminal | Upper clamp |
| `--cell-aspect` | `F` | `0.5` | Cell width ÷ height. `0.5` ≈ typical monospace |
| `--auto-cell-aspect` | — | on (TTY) | Query the terminal for pixel cell size (`CSI 14 t` / `TIOCGWINSZ`) and override `--cell-aspect` |
| `--crop` | `X,Y,W,H` | — | Source-space crop, pixels or `%`, applied before resample |
| `--crop-auto` | — | off | Trim uniform borders (letterboxing) |
| `--rotate` | `DEG` | `0` | `0` `90` `180` `270` |
| `--flip` | `AXIS` | — | `h` `v` `hv` |
| `--align` | `POS` | `center` | `left` `center` `right` — padding within the target box |
| `--valign` | `POS` | `center` | `top` `middle` `bottom` |
| `--resample` | `FILTER` | `area` | `nearest` `box` `area` `bilinear` `bicubic` `mitchell` `lanczos3` |
| `--supersample` | `N` | `1` | Resample to N× the cell grid so the selector sees N× subcell detail. `2` is a cheap quality win |
| `--trim` | — | off | Strip trailing blank cells per line |

### 5.1 Default size resolution

```
if --width/--height/--size given      → use them (derive the missing one via cell-aspect)
else if stdout is a TTY               → terminal size, minus 1 row for the prompt
else if a raster sink is selected     → 160 cells wide
else                                  → 100 cells wide
                                        height always derived from source aspect × cell-aspect
```

---

## 6. Preprocessing filter chain

Applied to the `Frame` **before** resampling. The chain is an **ordered list**.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--filter` | `NAME[:k=v,...]` | — | Repeatable. Appends to the chain in the order given. The explicit form for full control |
| `--brightness` | `F` | `0` | −1..1 additive |
| `--contrast` | `F` | `1.0` | Multiplicative around mid-grey |
| `--gamma` | `F` | `1.0` | |
| `--exposure` | `F` | `0` | Stops, applied in linear light |
| `--saturation` | `F` | `1.0` | `0` = greyscale |
| `--hue` | `DEG` | `0` | |
| `--invert` | — | off | |
| `--grayscale` | — | off | Discards chroma before selection (color flags then apply to a mono image) |
| `--auto-levels` | — | off | Per-channel black/white point stretch |
| `--levels` | `B,W[,G]` | — | Manual black, white, gamma |
| `--equalize` | — | off | Histogram equalization on luma |
| `--normalize` | — | off | Stretch luma to full range |
| `--sharpen` | `F` | `0` | Convenience for `unsharp:amount=F` |
| `--unsharp` | `R,A,T` | — | Radius, amount, threshold |
| `--blur` | `SIGMA` | `0` | Gaussian |
| `--denoise` | `F` | `0` | Bilateral strength |
| `--posterize` | `N` | — | Quantize to N levels per channel |
| `--threshold` | `F` | — | Hard binarize luma |
| `--auto-threshold` | `[METHOD]` | `otsu` | `otsu` `mean` `adaptive` |
| `--vignette` | `F` | `0` | |

**Canonical order** when using the sugar flags (so results are reproducible):
`crop → rotate/flip → denoise → exposure → levels/auto-levels → equalize/normalize →
brightness → contrast → gamma → hue → saturation → grayscale → blur → unsharp/sharpen →
posterize → threshold → vignette → invert`.

Any `--filter` entries are appended **after** that canonical block, in the order written.
Passing `--filter` with `--filter-only` replaces the canonical block entirely.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--filter-only` | — | off | Ignore sugar flags; the chain is exactly the `--filter` list |
| `--show-chain` | — | off | Print the resolved filter chain and exit |

---

## 7. Character sets

`--charset` selects **what glyphs the selector may choose from**. It is fully independent
of `--algo`.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-c, --charset` | `NAME` | `mixed` | Named set, see §7.1 |
| `--chars` | `"STRING"` | — | Inline literal set. Implies `--charset custom` |
| `--charset-file` | `PATH` | — | Load a set definition (§18) |
| `--charset-add` | `NAME\|"STR"` | — | Repeatable: union another set or literal chars |
| `--charset-remove` | `"STR"` | — | Repeatable: subtract characters |
| `--charset-tier` | `0..3` | `2` | Font-safety ceiling; clamps `mixed`/`unicode` (§7.2) |
| `--sort-ramp` | `auto\|on\|off` | `auto` | Reorder the set by ink coverage so `ramp` works on arbitrary sets |
| `--ramp-order` | `given\|coverage\|reverse` | `coverage` | Ordering rule when `--sort-ramp` is active |
| `--blank-char` | `CHAR` | `' '` | Glyph used for empty cells |
| `--fill-char` | `CHAR` | auto | Glyph used for fully-covered cells |
| `--allow-wide` | — | off | Permit double-width glyphs (breaks cell alignment on some terminals) |
| `--exclude-ambiguous` | — | on | Drop East-Asian-ambiguous-width glyphs |

### 7.1 Built-in charsets

| Name | Contents | Best paired with |
|---|---|---|
| `ramp10` | `" .:-=+*#%@"` — the classic 10-step ramp | `ramp` |
| `ramp16` | 16-step luminance ramp | `ramp` |
| `ramp70` | 70-step ramp | `ramp` |
| `ascii` | Printable ASCII `0x20`–`0x7E`, minus backslash-hostile chars | `feature`, `template` |
| `ascii-safe` | `ascii` minus quotes/backslash/backtick — safe for shell paste & JSON | any |
| `ascii-digits` | `0123456789 ` | novelty |
| `ascii-binary` | `01 ` | novelty |
| `mixed` ★ | Curated ASCII + box drawing + geometric + light blocks. Structural coverage without the solid-block look | `feature`, `bitmask` |
| `unicode` | Everything permitted by the current `--charset-tier` | `template`, `bitmask` |
| `box` | Box drawing `U+2500`–`U+257F` | `edge`-heavy configs |
| `geometric` | Geometric shapes `U+25A0`–`U+25FF` | `feature` |
| `blocks` | Block elements `U+2580`–`U+259F` (eighths + quadrants) | `bitmask` |
| `blocks-quad` | Quadrants only — 2×2 subcell | `bitmask` |
| `halfblock` | `' '` + `▀` — 2×1 subcell, full color per half | `bitmask` |
| `sextant` | Legacy Computing `U+1FB00`+ — 2×3 subcell | `bitmask` |
| `octant` | Unicode 16 octants — 2×4 subcell | `bitmask` |
| `braille` | `U+2800`–`U+28FF` — 2×4 dot subcell | `bitmask` |
| `braille-6` | 6-dot braille subset (wider font support) | `bitmask` |
| `shade` | `' ░▒▓█'` | `ramp`, `dither` |
| `custom` | From `--chars` / `--charset-file` | any |

★ = default.

### 7.2 Font-safety tiers

`--charset-tier` caps which codepoint ranges `mixed` and `unicode` may draw from. This is
the knob for "will this actually render in the user's terminal".

| Tier | Adds | Risk |
|---:|---|---|
| `0` | ASCII `0x20`–`0x7E` only | none |
| `1` | + Latin-1 symbols, General Punctuation | very low |
| `2` ★ | + Box Drawing, Block Elements, Geometric Shapes, Braille | low |
| `3` | + Symbols for Legacy Computing (sextants), Unicode 16 octants | high — sparse font coverage |

### 7.3 Glyph rasterization

Template-matching algorithms need actual glyph bitmaps. These also drive the raster sinks.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--font` | `PATH` | built-in | TTF/OTF used to rasterize the charset |
| `--font-index` | `N` | `0` | Face index within a TTC |
| `--font-cell` | `WxH` | `8x16` | Rasterization grid per glyph. Larger = better matching, slower startup |
| `--ink-threshold` | `F` | `0.5` | Coverage cutoff when binarizing glyphs for `bitmask` |
| `--atlas-cache` | `PATH` | user cache dir | Where rasterized atlases are memoized |
| `--no-atlas-cache` | — | off | Always re-rasterize |
| `--dump-atlas` | `PATH` | — | Write the glyph atlas as PNG for inspection, then exit |

A built-in embedded font ships with the binary so `--font` is never *required*.

---

## 8. Glyph selection algorithms

`--algo` selects exactly one. Everything in §9 and §10 layers on top.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-a, --algo` | `NAME` | `feature` | See table below |
| `--quality` | `LEVEL` | `balanced` | `fast` `balanced` `best` — presets the algorithm's internal knobs |
| `--metric` | `NAME` | per-algo | `mse` `mae` `cosine` `ssim` — distance function where applicable |
| `--subcell` | `WxH` | `2x4` | Subcell grid for `feature` / `gradient` |
| `--candidates` | `N` | `1` | Top-K coarse match, then rescore with the exact metric. `>1` costs time, buys accuracy |
| `--glyph-bias` | `F` | `0` | Push selection toward lighter (`<0`) or heavier (`>0`) glyphs |

### 8.1 Registered algorithms

| Name | Approach | Cost | Notes |
|---|---|---|---|
| `ramp` | Mean luma → index into an ordered charset | ★ | Reference baseline. Requires an ordered set (auto-sorted otherwise) |
| `feature` ★ | Per-cell subcell mean vector → nearest neighbour among glyph vectors | ★★ | **Default.** ~90% of template quality at a fraction of the cost |
| `gradient` | Feature vector extended with dx/dy moments | ★★ | Matches orientation, not just coverage. Best general-purpose for photos |
| `template` | Full per-pixel SSD/SSIM against every glyph bitmap | ★★★★ | Exact ground truth. Use to validate the cheaper selectors |
| `dct` | Match low-frequency DCT coefficients of cell vs glyph | ★★★ | Frequency-domain variant; good on textured sources |
| `ssim` | Maximize structural similarity per cell | ★★★★ | Perceptually strongest, slowest |
| `bitmask` | 1-bit glyph masks + joint fg/bg two-color split (chafa-style) | ★★ | **Best for color.** Solves glyph and color together. Required for block/braille/half-block charsets |
| `pixel` | No glyph matching; direct subcell pixel packing | ★ | Fast path equivalent to `bitmask` over `halfblock`/`blocks` |
| `edge` | Structure-only: gradient orientation → directional glyph | ★★ | Shorthand for `--edge-mode only`; see §10 |

Algorithm-specific parameters that don't warrant a first-class flag go through `--set`:

```sh
--set feature.grid=3x6
--set template.metric=mae
--set bitmask.color_split=kmeans      # vs. mask-mean
--set dct.coeffs=6
--set ssim.window=4
```

`asciigen --help feature` prints the full parameter schema for any registered algorithm.

---

## 9. Dithering modifier

Applies error diffusion / ordered dithering to the quantization step. Composes with **any**
`--algo`. What gets dithered depends on the algorithm — luma for `ramp`, the color split for
`bitmask`, the palette mapping for indexed color modes.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-d, --dither` | `NAME` | `none` | See table |
| `--dither-strength` | `F` | `1.0` | `0`–`1` blend toward undithered |
| `--dither-target` | `WHAT` | `auto` | `luma` `color` `both` `auto` |
| `--dither-serpentine` | `on\|off` | `on` | Boustrophedon scan for error-diffusion kernels |
| `--dither-scale` | `N` | `1` | Ordered-matrix scale factor |
| `--noise` | `F` | `0` | Pre-quantization uniform noise; poor man's dither |
| `--seed` | `N` | random | Determinism for `random`/`blue-noise` |

| Name | Family | Notes |
|---|---|---|
| `none` ★ | — | |
| `floyd-steinberg` / `fs` | error diffusion | The default choice when you want dithering |
| `atkinson` | error diffusion | Diffuses 6/8 of error — lighter, classic Mac look |
| `jjn` | error diffusion | Jarvis-Judice-Ninke, wide kernel, smooth |
| `stucki` | error diffusion | |
| `burkes` | error diffusion | |
| `sierra` / `sierra-lite` | error diffusion | |
| `bayer2` `bayer4` `bayer8` `bayer16` | ordered | Deterministic, parallelizable, **stable across video frames** |
| `blue-noise` | ordered | Best-looking ordered option; needs an embedded noise texture |
| `riemersma` | space-filling curve | Hilbert-curve diffusion, no directional artifacts |
| `random` | stochastic | Novelty |

> **Video note:** error-diffusion dithers are sequential and temporally unstable — they
> shimmer between frames and serialize the inner loop. For video, prefer `bayer8` or
> `blue-noise`. The tool emits a warning when an error-diffusion dither is combined with a
> video source; suppress with `--no-warn dither-video`.

---

## 10. Edge / structure modifier

The Difference-of-Gaussians + Sobel pipeline. Composes with any base `--algo`: edges are
detected, quantized into direction buckets, mapped to directional glyphs, and **overlaid**
onto the base selection where edge magnitude exceeds threshold.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-e, --edge` | `NAME` | `none` | Detector, see table |
| `--edge-mode` | `MODE` | `overlay` | `overlay` (edges win where strong) · `only` (discard base) · `blend` (weight by magnitude) · `mask` (base algo, but only render where edges are) |
| `--edge-threshold` | `F` | `0.15` | Magnitude cutoff, `0`–`1` |
| `--edge-strength` | `F` | `1.0` | Multiplier on detected magnitude |
| `--edge-quantize` | `N` | `4` | Direction buckets: `2` `4` `8` |
| `--edge-chars` | `"STR"` | `"\|/-\\"` | One glyph per bucket, in increasing-angle order |
| `--edge-color` | `MODE` | `inherit` | `inherit` (from base cell) · `source` (sample image) · `#RRGGBB` |
| `--edge-thin` | `on\|off` | `on` | Non-maximum suppression |
| `--edge-sigma` | `F` | `1.0` | Base Gaussian σ (DoG/XDoG) |
| `--edge-k` | `F` | `1.6` | σ ratio between the two Gaussians |
| `--edge-tau` | `F` | `0.98` | XDoG sharpening |
| `--edge-phi` | `F` | `20.0` | XDoG soft-threshold falloff |
| `--canny-low` | `F` | `0.1` | |
| `--canny-high` | `F` | `0.3` | |
| `--edge-downscale` | `N` | `1` | Detect at 1/N resolution for speed |

| Name | Detector | Notes |
|---|---|---|
| `none` ★ | — | |
| `sobel` | 3×3 gradient | Cheapest, good enough for direction |
| `scharr` | 3×3, better rotational symmetry | Preferred over `sobel` |
| `prewitt` | 3×3 | |
| `laplacian` | second derivative | Zero-crossings, no direction |
| `dog` | Difference of Gaussians | Clean stylized outlines; the "Gaussian" pipeline |
| `xdog` | Extended DoG | Ink-drawing look, most controllable |
| `canny` | DoG + NMS + hysteresis | Thinnest, most selective lines |

`dog`/`xdog`/`canny` produce magnitude only; direction is always taken from a `scharr` pass
so `--edge-chars` works for every detector.

---

## 11. Color

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-C, --color` | `MODE` | `auto` | `auto` `none` `16` `256` `truecolor` |
| `--color-mode` | `MODE` | `split` | `fg` (foreground only) · `fg-bg` (cell mean bg) · `split` (mask-partitioned two-color) |
| `--fg` | `COLOR` | auto | Fixed foreground. `#RRGGBB`, `rgb(r,g,b)`, palette index, or a named color |
| `--bg` | `COLOR` | `terminal` | Fixed background, or `terminal` / `transparent` |
| `--bg-mode` | `MODE` | `source` | `source` `fixed` `transparent` `terminal` |
| `--palette` | `NAME\|PATH` | `xterm256` | Palette for indexed modes: `xterm256` `ansi16` `vga` `c64` `gameboy` `solarized` or a file |
| `--color-space` | `NAME` | `oklab` | Distance metric for nearest-color: `srgb` `linear` `lab` `oklab` |
| `--blend-space` | `NAME` | `linear` | Space used for averaging pixels into cell colors |
| `--color-saturation` | `F` | `1.0` | Post-selection saturation on emitted colors |
| `--min-contrast` | `F` | `0.0` | Force apart fg/bg pairs that are too close |
| `--attributes` | `LIST` | `auto` | `none` · `auto` · comma list of `bold,faint,italic,underline` — used as extra intensity steps in 16-color mode |
| `--invert-video` | — | off | Swap fg and bg per cell |
| `--alpha` | `MODE` | `blend` | `keep` (sink permitting) · `blend` (onto `--alpha-bg`) · `threshold` |
| `--alpha-bg` | `COLOR` | `#000000` | |
| `--alpha-threshold` | `F` | `0.5` | Cells below become `--blank-char` |
| `--dim-bg` | `F` | `0` | Darken emitted backgrounds — improves glyph legibility |

**`--color auto` resolution:** `NO_COLOR` set → `none`; `CLICOLOR_FORCE`/`FORCE_COLOR` set →
`truecolor`; `COLORTERM` ∈ {`truecolor`,`24bit`} → `truecolor`; `TERM` contains `256` →
`256`; stdout not a TTY and sink is `ansi` → `truecolor`; otherwise `16`.

**On `--color-mode split`:** partition each cell's pixels by the selected glyph's coverage
mask, average each group, emit as fg/bg. This roughly doubles apparent detail for free and
is why it's the default. It requires a glyph mask, so it degrades to `fg-bg` under `--algo ramp`
with no font atlas.

**On braille:** all dots share one foreground, so `split` degenerates to `fg`. The tool
warns once and continues.

---

## 12. Output & sinks

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-o, --output` | `PATH` | stdout | `-` for stdout |
| `-f, --format` | `FMT` | `auto` | Inferred from extension when `auto` |
| `--overwrite` | — | off | Allow clobbering an existing file |
| `--append` | — | off | Append rather than truncate (text sinks only) |
| `--line-ending` | `LF\|CRLF` | platform | |
| `--reset-at-eol` | `on\|off` | `on` | Emit `SGR 0` at each line end — safe pasting, slightly larger output |
| `--no-final-newline` | — | off | |

### 12.1 Registered sinks

| Format | Extensions | Color | Animation | Notes |
|---|---|---|---|---|
| `ansi` ★ | `.ans` `.txt` | full | via `asciicast` | Escape codes inline. `cat` displays it. Default for terminals and files |
| `plain` | `.txt` | no | no | Glyphs only, no escapes |
| `html` | `.html` | full | CSS keyframes (opt-in) | Standalone page or fragment |
| `svg` | `.svg` | full | SMIL (opt-in) | Vector, embeddable |
| `png` / `jpg` / `webp` | | full | no | Rasterizes the chosen glyphs back into an image. The shareable output |
| `gif` | `.gif` | 256 | yes | Rasterized, palette-quantized |
| `mp4` / `webm` | | full | yes | Rasterized, encoded via ffmpeg |
| `asciicast` | `.cast` | full | yes | asciinema v2 JSONL. **The recommended video archive format** |
| `json` | `.json` | full | yes | Structured `CellBuffer` dump — for downstream tooling |
| `caf` | `.caf` | full | yes | Custom binary, delta-coded + compressed (§12.6) |
| `markdown` | `.md` | no | no | Fenced code block |
| `irc` | | 16 | no | mIRC color codes |
| `discord` | | 16 | no | ANSI code block wrapper, 2000-char aware |

### 12.2 Raster sink options (`png` `jpg` `webp` `gif` `mp4` `webm`)

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--render-font` | `PATH` | `--font` value | Font used for output rasterization |
| `--render-cell` | `WxH` | `8x16` | Output pixels per cell |
| `--render-scale` | `N` | `1` | Integer upscale of the whole render |
| `--render-bg` | `COLOR` | `#000000` | Page background where cells are transparent |
| `--render-padding` | `N` | `0` | Border in pixels |
| `--render-antialias` | `on\|off` | `on` | |
| `--jpeg-quality` | `N` | `92` | |

### 12.3 HTML sink options

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--html-standalone` | `on\|off` | `on` | Full document vs. bare `<pre>` fragment |
| `--html-title` | `STR` | input filename | |
| `--html-style` | `MODE` | `classes` | `inline` (style attrs) · `classes` (deduplicated CSS) — `classes` is much smaller |
| `--html-css` | `PATH` | — | Extra stylesheet to inline |
| `--html-font-family` | `STR` | `monospace` | |
| `--html-line-height` | `F` | `1.0` | |
| `--html-animate` | `on\|off` | `off` | Emit CSS keyframe animation for video input |

### 12.4 SVG sink options

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--svg-font-family` | `STR` | `monospace` | |
| `--svg-embed-font` | `on\|off` | `off` | Base64 the font into the SVG (large, but portable) |
| `--svg-as-paths` | `on\|off` | `off` | Convert glyphs to outlines — no font dependency at all |

### 12.5 asciicast sink options

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--cast-title` | `STR` | input filename | |
| `--cast-idle-limit` | `F` | — | Cap idle gaps, in seconds |
| `--cast-version` | `2` | `2` | Reserved for v3 |

### 12.6 `caf` binary sink options

The only format that requires our own player. Justified only by random seek, small size, or
re-styling on playback without re-converting the source. Stores **cells**, not escape bytes,
which is what makes deltas compress and re-styling possible.

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--compress` | `NAME` | `zstd` | `none` `zstd` `lz4` |
| `--compress-level` | `N` | `3` | |
| `--keyframe-interval` | `N` | `60` | Frames between full (non-delta) frames — seek granularity |
| `--embed-charset` | `on\|off` | `on` | Store the charset table in the header so any player can decode |
| `--embed-font` | `on\|off` | `off` | Store the font too, for exact raster reproduction |

---

## 13. Terminal playback

Active when the sink is the terminal (stdout is a TTY, format `ansi`).

| Flag | Arg | Default | Description |
|---|---|---|---|
| `--play` / `--no-play` | — | auto | Force/disable animated playback |
| `--fps` | `N` | source | Target output frame rate |
| `--speed` | `F` | `1.0` | Playback rate multiplier |
| `--loop` | `[N]` | `0` (still) / `1` (video) | `0` = infinite |
| `--alt-screen` / `--no-alt-screen` | — | on for video | Use the alternate screen buffer |
| `--sync-output` / `--no-sync-output` | — | auto | DECSET 2026 synchronized updates — prevents tearing |
| `--delta` / `--no-delta` | — | `auto` | Redraw only changed cells. `auto` measures change ratio per frame and switches |
| `--delta-threshold` | `F` | `0.6` | Change ratio above which `auto` emits a full frame |
| `--clear` / `--no-clear` | — | on | Clear before first frame |
| `--cursor` / `--hide-cursor` | — | hide | |
| `--frame-drop` / `--no-frame-drop` | — | on | Drop late frames instead of falling behind |
| `--audio` / `--no-audio` | — | off | Play the source audio track alongside |
| `--volume` | `F` | `1.0` | |
| `--av-sync` | `MODE` | `audio` | `audio` (master clock) · `video` · `none` |
| `--progress` | `on\|off` | auto | Status line: time, fps, dropped frames |
| `--title` | `STR` | — | Set the terminal window title |
| `--pause-on-start` | — | off | |
| `--interactive` / `--no-interactive` | — | on for video | Enable keybindings (§13.1) |
| `--watch` | — | off | Re-render when the input file changes on disk |
| `--exit-on-eof` | `on\|off` | `on` | |

### 13.1 Interactive keybindings

Available under `--interactive`. Deliberately includes live algorithm cycling — the fastest
way to tune a conversion is to see the alternatives on the same frame.

| Key | Action |
|---|---|
| `space` | Pause / resume |
| `←` `→` | Seek ∓5 s |
| `,` `.` | Step one frame back / forward (paused) |
| `+` `-` | Speed up / down |
| `a` `A` | Cycle `--algo` forward / back |
| `c` | Cycle `--charset` |
| `k` | Cycle `--color` mode |
| `d` | Cycle `--dither` |
| `e` | Cycle `--edge` |
| `i` | Toggle `--invert` |
| `[` `]` | Adjust `--edge-threshold` |
| `{` `}` | Adjust `--contrast` |
| `s` | Save the current frame (uses `--output` or an auto-named PNG) |
| `p` | Print the current flag set to stderr — copy-paste to reproduce |
| `r` | Reset to the invocation's flags |
| `q` / `Esc` | Quit |

---

## 14. Presets

`--preset NAME` expands to a documented flag set at priority rank 7 — anything you pass
explicitly still wins. `asciigen --list presets` prints each expansion.

| Preset | Expansion (indicative) |
|---|---|
| `photo` | `--algo gradient --charset mixed --color truecolor --color-mode split --supersample 2` |
| `quality` | `--algo ssim --quality best --candidates 8 --supersample 3 --color truecolor` |
| `fast` | `--algo ramp --charset ramp16 --color 256 --supersample 1` |
| `lineart` | `--algo edge --edge xdog --edge-mode only --charset box --color none` |
| `sketch` | `--algo feature --edge dog --edge-mode overlay --charset ascii --color none --dither bayer8` |
| `pixel` | `--algo bitmask --charset halfblock --color truecolor --supersample 2` |
| `braille` | `--algo bitmask --charset braille --dither bayer8 --color none` |
| `mosaic` | `--algo bitmask --charset sextant --charset-tier 3 --color truecolor` |
| `retro` | `--algo ramp --charset shade --palette vga --color 16 --dither bayer4` |
| `newspaper` | `--grayscale --algo ramp --charset ascii --dither atkinson --color none` |
| `matrix` | `--charset ascii-digits --color 16 --fg green --bg black` |
| `discord` | `--format discord --color 16 --width 80 --charset ascii-safe` |
| `web` | `--format html --html-style classes --color truecolor --width 200` |

---

## 15. Performance & diagnostics

| Flag | Arg | Default | Description |
|---|---|---|---|
| `-j, --threads` | `N` | `0` | `0` = hardware concurrency. `1` = fully serial (deterministic) |
| `--simd` | `NAME` | `auto` | `auto` `none` `sse2` `avx2` `neon` — `none` selects the scalar reference path |
| `--queue-depth` | `N` | `4` | Frames buffered between decode / convert / write stages |
| `--lut` | `on\|off` | `on` | Quantized-feature lookup table instead of full nearest-neighbour search |
| `--lut-bits` | `N` | `3` | Bits per feature dimension in the LUT. Higher = more accurate, more memory |
| `--no-cache` | — | off | Disable all on-disk caches |
| `--stats` | — | off | Per-stage timings: decode, filter, resample, select, colorize, encode, write |
| `--stats-format` | `FMT` | `text` | `text` `json` |
| `--bench` | `[N]` | — | Convert N times (default 100), report throughput, write nothing |
| `--bench-sink` | `NAME` | `null` | Isolate conversion cost from terminal cost by benchmarking against `null` |
| `--profile` | `PATH` | — | Write a per-stage trace |
| `--debug-dump` | `STAGE:PATH` | — | Repeatable. Dump an intermediate buffer as PNG. `STAGE` ∈ `decoded` `filtered` `resampled` `edges` `mask` `atlas` |
| `--no-warn` | `ID` | — | Repeatable. Suppress a named warning |

> `--bench-sink null` matters more than it looks: on most terminals the emulator's own text
> rendering dominates total frame time, so benchmarking against a real TTY measures the
> terminal, not this program.

---

## 16. Environment variables

| Variable | Effect |
|---|---|
| `ASCIIGEN_CONFIG` | Config file path (equivalent to `--config`) |
| `ASCIIGEN_PRESET` | Default preset |
| `ASCIIGEN_FONT` | Default `--font` |
| `ASCIIGEN_THREADS` | Default `--threads` |
| `ASCIIGEN_CACHE_DIR` | Atlas/LUT cache location |
| `ASCIIGEN_FFMPEG` | Path to the ffmpeg binary |
| `NO_COLOR` | Any value → `--color none` |
| `FORCE_COLOR`, `CLICOLOR_FORCE` | Any value → force color even when not a TTY |
| `COLORTERM`, `TERM` | Consulted by `--color auto` |
| `COLUMNS`, `LINES` | Fallback terminal size when `ioctl` is unavailable |

---

## 17. Introspection & the registry

### 17.1 `--list <TOPIC>`

Topics: `algos` `charsets` `dither` `edge` `filters` `formats` `presets` `palettes`
`fonts` `codecs` `all`.

Output is a table on a TTY and JSON otherwise, so shell completions and any GUI wrapper can
be generated from the binary rather than hand-maintained:

```sh
asciigen --list algos            # human table
asciigen --list algos | jq .     # machine-readable when piped
```

### 17.2 `--set NS.KEY=VALUE`

The generic parameter channel. Every registered component declares a typed parameter schema;
`--set` writes into it directly. This is what keeps rule 4 true — a new algorithm gains
tunable parameters with no CLI code change, and `--help <name>` documents them automatically.

```sh
asciigen in.png --algo feature --set feature.grid=3x6 --set feature.weight_gradient=0.4
asciigen in.png --edge xdog --set xdog.epsilon=0.02
asciigen in.png --dither blue-noise --set blue_noise.texture=./noise64.png
```

Namespaces: `<component-name>.<param>`. Unknown namespaces are an error; unknown keys within
a known namespace are an error listing the valid keys.

---

## 18. Charset definition file

Loaded by `--charset-file`. TOML, with a bare-text fallback (one line = the literal set).

```toml
name        = "my-set"
description = "ASCII plus box drawing"
tier        = 2                  # advisory font-safety tier
ordered     = true               # if true, `ramp` may use the given order directly
wide        = false              # contains double-width glyphs

# Either a literal string...
chars = " .:-=+*#%@"

# ...or explicit entries with optional hints
[[glyph]]
char     = "▀"
coverage = 0.5                   # optional; otherwise measured from the font
mask     = "11110000"            # optional explicit 1-bit mask, row-major

[[glyph]]
char = "│"

# Composition
include = ["ascii-safe", "box"]  # union of other registered sets
exclude = "\"'`\\"               # characters to subtract
```

## 19. Config file

Same key names as the CLI flags, with dashes → underscores, grouped by pipeline stage.

```toml
[input]
decoder = "ffmpeg-pipe"

[geometry]
width       = 160
fit         = "contain"
cell_aspect = 0.5

[charset]
name = "mixed"
tier = 2

[algo]
name    = "gradient"
quality = "balanced"
subcell = "2x4"

[dither]
name = "bayer8"

[edge]
name      = "dog"
mode      = "overlay"
threshold = 0.18

[color]
mode        = "truecolor"
color_mode  = "split"
color_space = "oklab"

[output]
format = "ansi"

[performance]
threads = 0
simd    = "auto"

[preset.mine]                     # user-defined presets
algo    = "ssim"
charset = "mixed"
dither  = "blue-noise"
```

---

## 20. Combination validity

The interface is orthogonal by design, but not every pairing is meaningful. Three severities:
**error** (refuse and explain), **warn** (proceed, one-time message to stderr), **ignore**
(silently irrelevant).

| Combination | Severity | Behavior |
|---|---|---|
| `--algo ramp` + unordered charset | ignore | Charset auto-sorted by coverage (`--sort-ramp auto`) |
| `--algo ramp` + `--color-mode split` | warn | No glyph mask available → falls back to `fg-bg` |
| `--algo template\|ssim\|dct\|bitmask` + no font | ignore | Uses the embedded built-in font |
| `--charset braille\|halfblock\|blocks\|sextant\|octant` + `--algo` other than `bitmask`/`pixel` | warn | These sets are subcell masks; other selectors give poor results. Auto-switch is *not* performed — the user asked for it |
| `--charset braille` + `--color-mode split` | warn | Braille has one ink color per cell → degrades to `fg` |
| `--charset-tier 3` + no font coverage check | warn | Sextants/octants may render as tofu |
| `--edge-mode only` + `--algo X` | ignore | Base algorithm is bypassed; `X` only affects fallback cells |
| `--edge` + `--charset braille` | warn | Directional glyphs don't exist in the set; edges fall back to dot density |
| error-diffusion `--dither` + video input | warn | Temporally unstable and serial; suggests `bayer8` |
| `--dither` + `--color none` + `--algo bitmask` | ignore | Nothing to diffuse into |
| `--color truecolor` + `--format plain\|markdown` | ignore | Sink has no color channel |
| `--color truecolor` + `--format irc\|discord` | warn | Down-converted to 16 colors |
| `--format png\|jpg\|gif\|mp4\|webm` + no renderable font | ignore | Uses the built-in font |
| `--format mp4\|webm\|gif` + still input | error | Animated sink needs an animated source (unless `--loop N` is given) |
| `--format asciicast\|caf` + still input | warn | Produces a single-frame recording |
| `--audio` + non-TTY output | error | Audio only exists during terminal playback |
| `--audio` + no audio stream | warn | |
| `--delta`, `--alt-screen`, `--sync-output`, `--interactive` + non-terminal sink | ignore | Terminal-only concerns |
| `--play` + still input | ignore | |
| `--frame N` + `--start/--duration` | error | Single-frame extraction conflicts with a range |
| `--end` + `--duration` | error | Mutually exclusive |
| `--fit exact` + mismatched sequence dimensions | error | |
| `-q` + `-v` | error | |
| `--output` exists + no `--overwrite` | error | |
| `--threads N>1` + error-diffusion dither | ignore | That stage runs serially regardless; other stages still parallelize |
| `--simd none` + `--quality best` | ignore | Correct, just slow — this is the reference path |
| `--allow-wide` + `--fit exact` | warn | Wide glyphs break exact cell counts |

---

## 21. Exit codes

| Code | Meaning |
|---:|---|
| `0` | Success |
| `1` | Generic runtime failure |
| `2` | Invalid arguments / invalid flag combination |
| `3` | Input not found or unreadable |
| `4` | Unsupported or undetectable input format |
| `5` | Decode failure |
| `6` | Output write failure |
| `7` | Missing external dependency (ffmpeg, font, codec) |
| `8` | Unknown registry name (algo/charset/dither/edge/filter/sink) |
| `130` | Interrupted (SIGINT) — terminal state is always restored first |

---

## 22. Worked examples

```sh
# Defaults: fit terminal, mixed charset, gradient-ish feature matching, truecolor split
asciigen photo.jpg

# High-quality still to a shareable PNG
asciigen photo.jpg --preset quality -o out.png --render-cell 12x24

# Pen-and-ink line drawing, no color
asciigen portrait.jpg --algo feature --charset ascii --edge xdog --edge-mode overlay \
                      --edge-threshold 0.2 --color none --width 200

# Edges only, box-drawing glyphs — pure structure
asciigen diagram.png --edge canny --edge-mode only --charset box --color none

# Maximum apparent resolution: braille + ordered dither
asciigen photo.jpg --algo bitmask --charset braille --dither bayer8 --color none -w 120

# Effectively a pixel renderer: two full-color pixels per cell
asciigen photo.jpg --algo bitmask --charset halfblock --color truecolor --supersample 2

# Chafa-style: joint glyph + two-color optimization over a curated mixed set
asciigen photo.jpg --algo bitmask --charset mixed --color-mode split --color truecolor

# Retro palette with ordered dithering
asciigen photo.jpg --preset retro --palette c64 -w 80

# Video, played in the terminal with delta updates and tear-free output
asciigen clip.mp4 --algo feature --dither bayer8 --color truecolor \
                  --delta --sync-output --fps 30

# Video, archived as an asciicast for the web
asciigen clip.mp4 --preset photo -o demo.cast --start 00:00:05 --duration 20

# Video, re-encoded back to a real MP4 of the ASCII render
asciigen clip.mp4 --preset photo -o ascii.mp4 --render-cell 10x20 --out-fps 30

# Custom charset from a file, tuned via the generic parameter channel
asciigen photo.jpg --charset-file sets/kana.toml --algo feature \
                   --set feature.grid=3x6 --set feature.weight_gradient=0.5

# Webcam, live
asciigen cam:0 --preset pixel --fps 30 --interactive

# Raw frames piped in
ffmpeg -i in.mkv -f rawvideo -pix_fmt rgb24 - | \
  asciigen - --input-format rawvideo --raw-size 1920x1080 --raw-fps 24

# Reproducibility / debugging
asciigen photo.jpg --explain
asciigen photo.jpg --debug-dump edges:edges.png --debug-dump mask:mask.png
asciigen photo.jpg --bench 200 --bench-sink null --stats --stats-format json
```

---

## 23. Extension checklist

Adding a new component should touch exactly these places and **nothing in the CLI layer**:

| Adding a… | Implement | Register | Automatically gains |
|---|---|---|---|
| Algorithm | `IGlyphSelector` | `algo_registry` + param schema | `--algo NAME`, `--set NAME.*`, `--list algos`, `--help NAME`, interactive `a` cycling |
| Charset | data file or generator | `charset_registry` | `-c NAME`, `--charset-add NAME`, `--list charsets`, interactive `c` cycling |
| Dither | `IDither` | `dither_registry` | `-d NAME`, `--set NAME.*`, `--list dither` |
| Edge detector | `IEdgeDetector` | `edge_registry` | `-e NAME`, `--list edge` |
| Filter | `IFilter` | `filter_registry` | `--filter NAME:k=v`, `--list filters` |
| Sink | `ISink` (consumes `CellBuffer`) | `sink_registry` + extension map | `-f NAME`, extension inference, `--list formats` |
| Decoder | `ISource` (produces `Frame`) | `decoder_registry` | `--decoder NAME`, magic/extension detection |

The two data types (`Frame`, `CellBuffer`) are the only contracts. Keep them stable and the
rest of the system stays pluggable.

---

## 24. Options structs

One struct per pipeline stage (§1.1), mirroring the flag groups above. No `std::optional` —
every field carries a real default; "unset" uses a sentinel (`AUTO = 0`, `-1`, empty string)
so `Options{}` is already valid. Enum value names match the flag tables in §4–§15.

### InputOptions (§4)

| Field | Type | Default |
|---|---|---|
| paths | `string[]` | — |
| format | enum: auto/png/jpg/bmp/tga/gif/psd/hdr/mp4/mkv/webm/y4m/rawvideo/rawimage | auto |
| raw_size | `{w,h}` | `{0,0}` |
| raw_pix_fmt | enum: gray8/rgb24/rgba/bgr24/yuv420p | rgb24 |
| raw_fps | float | 25 |
| decoder | enum: auto/stb/gif/ffmpeg-pipe/ffmpeg-lib/camera | auto |
| ffmpeg_path | string | `"ffmpeg"` |
| probe_only | bool | false |
| frame | int | -1 (unset) |
| frame_at | float | -1 (unset) |
| start | float | 0 |
| end | float | -1 (unset) |
| duration | float | -1 (unset) |
| in_fps | float | 0 (= source) |
| frame_step | int | 1 |
| sequence_fps | float | 12 |
| camera_size | `{w,h}` | `{0,0}` |
| camera_fps | float | 0 |

### GeometryOptions (§5)

| Field | Type | Default |
|---|---|---|
| width, height | int | 0 (auto) |
| width_percent, height_percent | float | 0 (unused unless >0) |
| fit | enum: contain/cover/stretch/exact/width/height | contain |
| scale | float | 1.0 |
| full | bool | false |
| max_width, max_height | int | 0 (= terminal) |
| cell_aspect | float | 0.5 |
| auto_cell_aspect | tristate | auto |
| crop_enabled | bool | false |
| crop_x/y/w/h | float | 0 |
| crop_percent | bool | false |
| crop_auto | bool | false |
| rotate | enum: 0/90/180/270 | 0 |
| flip | enum: none/h/v/hv | none |
| align | enum: left/center/right | center |
| valign | enum: top/middle/bottom | middle |
| resample | enum: nearest/box/area/bilinear/bicubic/mitchell/lanczos3 | area |
| supersample | int | 1 |
| trim | bool | false |

### PreprocessOptions (§6)

| Field | Type | Default |
|---|---|---|
| filters | `FilterSpec[]` (name + key/value params) | empty |
| filter_only | bool | false |
| show_chain | bool | false |
| brightness | float | 0 |
| contrast | float | 1.0 |
| gamma | float | 1.0 |
| exposure | float | 0 |
| saturation | float | 1.0 |
| hue | float | 0 |
| invert | bool | false |
| grayscale | bool | false |
| auto_levels | bool | false |
| levels_enabled | bool | false |
| levels_black/white/gamma | float | 0 / 1 / 1 |
| equalize | bool | false |
| normalize | bool | false |
| sharpen | float | 0 |
| unsharp_enabled | bool | false |
| unsharp_radius/amount/threshold | float | 1 / 0 / 0 |
| blur | float | 0 |
| denoise | float | 0 |
| posterize | int | 0 (off) |
| threshold | float | -1 (off) |
| auto_threshold_enabled | bool | false |
| auto_threshold | enum: otsu/mean/adaptive | otsu |
| vignette | float | 0 |

### CharsetOptions (§7)

| Field | Type | Default |
|---|---|---|
| name | enum: ramp10/ramp16/ramp70/ascii/ascii-safe/ascii-digits/ascii-binary/mixed/unicode/box/geometric/blocks/blocks-quad/halfblock/sextant/octant/braille/braille-6/shade/custom | mixed |
| chars | string | empty |
| file | string | empty |
| add, remove | `string[]` | empty |
| tier | int 0..3 | 2 |
| sort_ramp | tristate | auto |
| ramp_order | enum: given/coverage/reverse | coverage |
| blank_char | string | `" "` |
| fill_char | string | empty (auto) |
| allow_wide | bool | false |
| exclude_ambiguous | bool | true |
| font | string | empty (built-in) |
| font_index | int | 0 |
| font_cell | `{w,h}` | `{8,16}` |
| ink_threshold | float | 0.5 |
| atlas_cache | string | empty (user cache dir) |
| no_atlas_cache | bool | false |
| dump_atlas | string | empty |

### AlgoOptions (§8)

| Field | Type | Default |
|---|---|---|
| name | enum: ramp/feature/gradient/template/dct/ssim/bitmask/pixel/edge | feature |
| quality | enum: fast/balanced/best | balanced |
| metric | enum: default/mse/mae/cosine/ssim | default |
| subcell | `{w,h}` | `{2,4}` |
| candidates | int | 1 |
| glyph_bias | float | 0 |

### DitherOptions (§9)

| Field | Type | Default |
|---|---|---|
| kind | enum: none/floyd-steinberg/atkinson/jjn/stucki/burkes/sierra/sierra-lite/bayer2/bayer4/bayer8/bayer16/blue-noise/riemersma/random | none |
| strength | float | 1.0 |
| target | enum: auto/luma/color/both | auto |
| serpentine | bool | true |
| scale | int | 1 |
| noise | float | 0 |
| seed_is_random | bool | true |
| seed | uint64 | 0 |

### EdgeOptions (§10)

| Field | Type | Default |
|---|---|---|
| detector | enum: none/sobel/scharr/prewitt/laplacian/dog/xdog/canny | none |
| mode | enum: overlay/only/blend/mask | overlay |
| threshold | float | 0.15 |
| strength | float | 1.0 |
| quantize | int (2/4/8) | 4 |
| chars | string | `"\|/-\\"` |
| color_mode | enum: inherit/source/fixed | inherit |
| color | `Color` | — |
| thin | bool | true |
| sigma | float | 1.0 |
| k | float | 1.6 |
| tau | float | 0.98 |
| phi | float | 20.0 |
| canny_low, canny_high | float | 0.1 / 0.3 |
| downscale | int | 1 |

### ColorOptions (§11)

| Field | Type | Default |
|---|---|---|
| depth | enum: auto/none/16/256/truecolor | auto |
| cell_mode | enum: fg/fg-bg/split | split |
| fg | `Color` | auto |
| bg | `Color` | terminal |
| bg_mode | enum: source/fixed/transparent/terminal | source |
| palette | enum: xterm256/ansi16/vga/c64/gameboy/solarized/file | xterm256 |
| palette_file | string | empty |
| color_space | enum: srgb/linear/lab/oklab | oklab |
| blend_space | enum: srgb/linear | linear |
| saturation | float | 1.0 |
| min_contrast | float | 0 |
| attributes_auto | bool | true |
| attributes | bitflags: bold/faint/italic/underline | none |
| invert_video | bool | false |
| alpha | enum: keep/blend/threshold | blend |
| alpha_bg | `Color` | `#000000` |
| alpha_threshold | float | 0.5 |
| dim_bg | float | 0 |

`Color` = `{kind: auto/rgb/palette-index/named/terminal/transparent, r,g,b: u8, index: int}`.

### OutputOptions (§12)

| Field | Type | Default |
|---|---|---|
| path | string | empty (stdout) |
| format | enum: auto/ansi/plain/html/svg/png/jpg/webp/gif/mp4/webm/asciicast/json/caf/markdown/irc/discord/null | auto |
| overwrite | bool | false |
| append | bool | false |
| line_ending | enum: platform/lf/crlf | platform |
| reset_at_eol | bool | true |
| no_final_newline | bool | false |
| render_font | string | empty |
| render_cell | `{w,h}` | `{8,16}` |
| render_scale | int | 1 |
| render_bg | `Color` | `#000000` |
| render_padding | int | 0 |
| render_antialias | bool | true |
| jpeg_quality | int | 92 |
| html_standalone | bool | true |
| html_title | string | empty |
| html_style | enum: inline/classes | classes |
| html_css | string | empty |
| html_font_family | string | `"monospace"` |
| html_line_height | float | 1.0 |
| html_animate | bool | false |
| svg_font_family | string | `"monospace"` |
| svg_embed_font | bool | false |
| svg_as_paths | bool | false |
| cast_title | string | empty |
| cast_idle_limit | float | -1 (unset) |
| cast_version | int | 2 |
| compress | enum: none/zstd/lz4 | zstd |
| compress_level | int | 3 |
| keyframe_interval | int | 60 |
| embed_charset | bool | true |
| embed_font | bool | false |

### PlaybackOptions (§13)

| Field | Type | Default |
|---|---|---|
| play | tristate | auto |
| fps | float | 0 (= source) |
| speed | float | 1.0 |
| loop | int | -1 (context default), 0 = infinite |
| alt_screen | tristate | auto |
| sync_output | tristate | auto |
| delta | tristate | auto |
| delta_threshold | float | 0.6 |
| clear | bool | true |
| show_cursor | bool | false |
| frame_drop | bool | true |
| audio | bool | false |
| volume | float | 1.0 |
| av_sync | enum: audio/video/none | audio |
| progress | tristate | auto |
| title | string | empty |
| pause_on_start | bool | false |
| interactive | tristate | auto |
| watch | bool | false |
| exit_on_eof | bool | true |

### PerfOptions (§15)

| Field | Type | Default |
|---|---|---|
| threads | int | 0 (hardware concurrency) |
| simd | enum: auto/none/sse2/avx2/neon | auto |
| queue_depth | int | 4 |
| lut | bool | true |
| lut_bits | int | 3 |
| no_cache | bool | false |
| stats | bool | false |
| stats_format | enum: text/json | text |
| bench | int | 0 (off) |
| bench_sink | `OutputFormat` | null |
| profile_path | string | empty |
| debug_dumps | `{stage, path}[]` | empty |
| suppressed_warnings | `string[]` | empty |

### Top-level Options (§3.1)

| Field | Type | Default |
|---|---|---|
| help_topic | enum: none/general/input/geometry/preprocess/charset/algo/dither/edge/color/output/playback/performance/presets/examples/all/item | none |
| help_query | string | empty (set only when help_topic == item) |
| list_topic | enum: none/algos/charsets/dither/edge/filters/formats/presets/palettes/fonts/codecs/all | none |
| show_version | bool | false |
| verbosity | enum: quiet/normal/info/debug/trace | normal |
| config_path | string | empty |
| no_config | bool | false |
| presets | `string[]` | empty |
| save_preset | string | empty |
| set_options | `{ns, key, value}[]` | empty |
| explain | bool | false |
| dry_run | bool | false |
| json_errors | bool | false |
| input / geometry / preprocess / charset / algo / dither / edge / color / output / playback / perf | the structs above | — |

`none`-valued enums (`help_topic == none`, `list_topic == none`) mean "flag not given" —
no separate bool needed to track whether it was requested.
