# `asciigen` — command line reference

> **Status:** spec for the app layer. The engine underneath it is built and working; this
> describes the CLI that drives it. Every flag here maps to a real field in a real struct —
> nothing below is aspirational.
>
> Images only. No video, no webcam, no playback.

---

## What it does

Turns a picture into characters. It picks a glyph per cell by matching the glyph's *shape*
against that patch of the image, colours it, and writes the result to a terminal, a text
file, or a PNG.

```sh
asciigen photo.jpg                                    # straight to the terminal
asciigen photo.jpg --out art.png --image-width 1900   # a 1900px-wide picture
asciigen photo.jpg --out art.ans --out art.txt        # two files at once
asciigen photo.jpg --preset wallpaper-cover           # a big PNG, auto backdrop
asciigen notes.ans                                    # just print an existing .ans
```

---

## The pipeline

```
  load ──▶ adjust ──▶ resample ──▶ dither ──▶ select ──▶ edges ──▶ grade ──▶ write
   │         │                        │         │          │        │         │
 --input  --source-*              --dither-*  --algo-*  --edge-*  --grid-*  --out
                                             --charset            --backdrop --image-*
                                             --font-*
```

Two things travel through it:

- **the source image** — pixels, up to the point where glyphs get chosen
- **the grid** — one cell per character, each holding a glyph plus a foreground and
  background colour

Everything before `select` acts on the image. Everything after acts on the grid. That split
matters more than it sounds — see [Why brightness is applied twice](#why-brightness-is-applied-twice).

---

## How flags are named

`--<thing>-<what-it-changes>`. The first word says **what it acts on**:

| Prefix | Acts on |
|---|---|
| `--input` | the file being read |
| `--source-` | the decoded image, before glyphs are chosen |
| `--font-` | the font file and how big glyphs are drawn |
| `--charset` | which characters may be used |
| `--dither-` `--edge-` `--algo-` | pipeline components, by their own name |
| `--grid-` | the character grid — its size *and* its colours |
| `--backdrop` | the colour behind everything |
| `--out` `--image-` | what gets written |

---

## 1. Input

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--input` | `PATH` | — | The picture to convert. Also accepted positionally |

Formats: `png` `jpg` `bmp` `tga` `gif` `psd` `hdr` — whatever `stb_image` reads.

**Passthrough.** If the input ends in `.txt` or `.ans`, nothing is converted. The file is read
and printed with the terminal put into the right mode first (VT escapes on, UTF-8 codepage on).
This exists because `cat` and `type` mangle `.ans` files on Windows. Every other flag is
ignored in this mode.

---

## 2. Source adjustments

Applied to the image **before** glyphs are chosen, so they change *which glyph gets picked*.
Use these to give the matcher a cleaner signal — not to fix how the output looks.

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--source-auto-levels` | `[LO,HI]` | off | Stretch the tonal range so darks hit black and brights hit white. `LO,HI` are percentiles, default `0.5,99.5` |
| `--source-levels` | `B,W[,G]` | off | The manual version. Black point, white point, gamma |
| `--source-contrast` | `F` | `1.0` | Pivots around mid-grey. Below 1 flattens |
| `--source-sharpen` | `A[,R]` | off | Unsharp mask. Amount, radius. Helps `structure` — a harder edge gives it more to work with |
| `--source-blur` | `R` | off | Softens. Useful on noisy photos where grain competes with real structure |

`--source-auto-levels` is the one worth reaching for first: it's the biggest single
improvement on a dull photo and it needs no tuning.

---

## 3. Font and glyph size

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--font-path` | `PATH` | built-in | TTF/OTF used to draw the glyphs |
| `--font-match-size` | `N` | `16` | Glyph height in pixels **used for matching**. Bigger = finer shape matching, slower |
| `--font-render-size` | `N` | auto | Glyph height in pixels **used for the output image**. Bigger = crisper PNG |

Only a height is given. **Width is always half the height** — the 1:2 cell aspect is baked in
so it can't be broken by accident.

Two sizes because they do different jobs. The matching atlas only has to be big enough to
tell glyphs apart (16px is plenty). The render atlas decides how sharp the PNG looks, and
wants to be much bigger. `--font-render-size` is normally left alone — see
[Sizing](#6-sizing-cells-glyphs-and-pixels).

---

## 4. Character set

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--charset` | `NAME` | `ascii` | A built-in set, below |
| `--charset-chars` | `"STR"` | — | Use exactly these characters |
| `--charset-range` | `U+XXXX-U+YYYY` | — | Use a codepoint range. Repeatable, unions together |

| Name | Contents |
|---|---|
| `ascii` | Printable ASCII, `0x20`–`0x7E`. 95 glyphs |
| `blocks` | Space plus Block Elements `U+2580`–`U+259F`. 33 glyphs |
| `ramp` | `" .:-=+*#%@"` — ordered light to dark, for `--algo ramp` |

The charset is **shared across algorithms but read differently**:

- `ramp` reads it as an ordered brightness ramp — position in the string means darkness
- `bitmask` and `structure` read it as an unordered bag of shapes — order is irrelevant

That's why `--charset ramp` with `--algo structure` is legal but wasteful (10 shapes to
choose from), and `--charset ascii` with `--algo ramp` gives nonsense ordering.

---

## 5. Choosing the algorithm

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--algo` | `NAME` | `structure` | Which selector runs |

| Name | How it picks | Good for |
|---|---|---|
| `ramp` | Cell brightness → index into an ordered charset | Fast, simple, nostalgic |
| `bitmask` | Per-pixel correlation of glyph ink against the cell | Solid all-rounder |
| `structure` | Edge direction + ink distribution, matched separately | **Default.** Cleanest edges and surfaces |

### Shared by every algorithm

These live on the algorithm because that's where colour is decided, but they mean the same
thing everywhere:

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--algo-allow-background` | — | off | Solve a *second* colour per cell instead of drawing on one backdrop. Much higher fidelity, but the output stops looking like text and starts looking like coloured blocks |
| `--algo-brightness-gamma` | `F` | `1.0` | Below 1 brightens the foreground. See [below](#why-brightness-is-applied-twice) |

### `--algo bitmask`

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--algo-bitmask-softness` | `F` | `0.0` | Blend a blurred match into the sharp one, so a stroke a pixel off still counts. **Measured as unhelpful** — see [Known gaps](#known-gaps) |
| `--algo-bitmask-blur-radius` | `N` | `1` | How far a stroke may drift, in cell pixels |

### `--algo structure`

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--algo-structure-orientation-weight` | `F` | `0.25` | How much *edge direction* counts. Raise for bold outlines, lower for smoother surfaces |
| `--algo-structure-mass-weight` | `F` | `1.0` | How much *where the ink sits* counts. This is what resolves shading |
| `--algo-structure-tone-weight` | `F` | `4.0` | Pull toward glyphs whose ink coverage matches the cell's brightness |
| `--algo-structure-orient-blocks` | `WxH` | `2x4` | Grid the cell is cut into before counting directions. Coarse on purpose |
| `--algo-structure-mass-blocks` | `WxH` | `8x16` | Grid for the ink-position term. One pixel per block by default |
| `--algo-structure-bins` | `N` | `4` | How finely direction is split, over a half turn |

`orientation-weight` is the real dial. At `0` you get pure surface fidelity; at `1.0`
everything turns into heavy outlines and flat panels go mushy. `0.25` is the balance.

---

## 6. Sizing: cells, glyphs and pixels

Three different sizes, and they're easy to confuse. They resolve **in this order**.

### Step 1 — how many cells

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--grid-width` | `N` | auto | Grid width in characters |
| `--grid-height` | `N` | auto | Grid height in characters |

- **One given** → the other is derived from the source's aspect ratio (allowing for cells
  being twice as tall as they are wide).
- **Both given** → used exactly, aspect ignored.
- **Neither** → terminal size if writing to a terminal, otherwise 160 wide.

### Step 2 — how many pixels per cell

`--font-render-size`, above. Grid × glyph size = the **natural size** of the picture.

### Step 3 — how big the file is

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--image-width` | `N` | natural | Output width in pixels |
| `--image-height` | `N` | natural | Output height in pixels |
| `--image-fit` | `MODE` | `contain` | `none` `width` `height` `contain` `cover` `stretch` |
| `--image-align` | `POS` | `center` | `top-left` `top` `top-right` `left` `center` `right` `bottom-left` `bottom` `bottom-right` |
| `--image-margin` | `N` | `0` | Pixels kept clear on every side |
| `--image-scale` | `F` | `1.0` | Art size as a fraction of what's left |
| `--image-aspect` | `R` | off | Reshape the final picture, e.g. `16:9` |

**`--image-aspect` only ever grows the canvas.** The short side is extended with backdrop �
nothing cropped, art never resampled. It's the alternative to naming a pixel size: a square
source becomes a 16:9 wallpaper at full glyph resolution rather than being squeezed to fit.
Reach for it when a fixed `--image-width` would throw away resolution you just paid for with
`--font-render-size`. Ignored when both width and height are given, since those already fix
the shape.

**The art is fitted into a box *inside* the canvas, not into the canvas itself.** `margin`
insets by a fixed number of pixels, `scale` then takes a fraction of what remains. Whatever
is left over stays backdrop � and that leftover is what `--image-align` positions within. At
`scale 1.0` with `fit contain` the art fills one axis completely, so alignment has nothing to
do on that axis; shrink the box and it becomes meaningful.

```sh
--image-scale 0.6 --image-align top-left    # art in the corner, rest clear for desktop icons
--image-margin 250                          # 250px kept clear on every side
```

### The rule: image size wins

If you ask for a 1900×1200 image, you get exactly 1900×1200. Everything upstream bends to
suit.

Concretely, when `--image-*` is given and `--font-render-size` is **not**, the render size is
worked backwards from the target:

```
renderSize = clamp(imageHeight / gridRows, 8, 64)   rounded down to even
```

Then: render at that size → scale to fit per `--image-fit` → place on the canvas per
`--image-align`, padded with the backdrop.

The clamp is the point. Without it you either render tiny and upscale into mush, or render a
4000px-tall picture and throw most of it away. If you set `--font-render-size` yourself it's
honoured exactly and the scale step picks up the difference.

`--image-fit` in one line each:

| Mode | Behaviour |
|---|---|
| `none` | Natural size, no scaling. Crops or pads to the canvas |
| `width` | Scale until the width matches |
| `height` | Scale until the height matches |
| `contain` | Scale until it *fits inside*. Nothing is cut off. **Default** |
| `cover` | Scale until it *fills*. Edges get cropped |
| `stretch` | Exactly the canvas, aspect ratio ignored |

---

## 7. Dithering

Nudges pixel values before the glyph is chosen, so a limited set of glyphs can suggest more
tones than it really has.

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--dither` | `NAME` | `none` | `none` or `bayer4` |
| `--dither-levels` | `N` | `4` | Tone steps the selector can resolve. **Lower dithers harder** — this is the strength knob |
| `--dither-adaptive` | `on\|off` | `on` | Back off where the picture already has detail |
| `--dither-flat-contrast` | `F` | `10` | Below this much local variation, dither at full strength |
| `--dither-edge-contrast` | `F` | `45` | Above this, don't dither at all |

**Leave `--dither-adaptive` on.** Dithering exists to break up banding, and banding only
happens in smooth areas. Applied everywhere, it shakes clean edges and fine detail apart —
measured at 39% of detail cells disturbed versus 2% with the gate on, with flat areas
unaffected either way.

---

## 8. Edges

Detects edges and stamps directional characters (`-` `/` `|` `\`) over the chosen glyphs.

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--edge` | `NAME` | `none` | `none` or `scharr` |
| `--edge-threshold` | `F` | `0.3` | How strong an edge must be. The "how much edging" dial |
| `--edge-coherence` | `F` | `0.55` | How much the cell must agree on one direction. Rejects texture |
| `--edge-subsamples` | `N` | `4` | Gradient samples per cell per axis |

**Off by default, and that's deliberate.** `bitmask` and `structure` already match glyph shape
across the whole charset; overwriting their choice with one of four characters throws away
detail. Orientation alone can't tell `` ` `` from `_` — both are horizontal, they just sit at
different heights, and the base algorithms already know that. Turn edges on for `ramp`, for
very small grids, or for a deliberate outlined look.

**The charset does not need to contain `- / | \`.** If any are missing they are appended to
the end of the set, which leaves every index already in use untouched. This happens *after*
selection, so the selector never had them available to pick for a cell that isn't an edge —
they only ever appear where the edge gates fire. A `blocks` charset has none of the four and
previously produced no edges at all; it now produces all four directions.

One consequence: if the charset grows, any glyph atlas used for **rendering** must be built
after `--edge` runs, not before. The atlas that drove selection does not need rebuilding.

---

## 9. Grid colour

Applied **after** glyphs are chosen, so they change how it's drawn without changing what was
drawn.

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--grid-brightness` | `F` | `1.0` | Linear gain. Clips at the top |
| `--grid-gamma` | `F` | `1.0` | Below 1 lifts without clipping. Usually what you want |
| `--grid-vibrance` | `F` | `0.0` | Saturation that boosts dull colours most and leaves vivid ones alone |
| `--grid-palette` | `NAME` | — | Snap colours to a palette: `gruvbox` `nord` |
| `--grid-palette-strength` | `F` | `1.0` | Below 1 tints toward the palette instead of quantising |
| `--grid-despeckle` | `F` | `0.1` | Blank cells too faint *and* too close to their background to be worth drawing |
| `--no-grid-despeckle` | — | — | Turn it off |

### Why brightness is applied twice

Output comes out around **half** the source's brightness, and it isn't a bug. A dark cell gets
a sparse glyph *and* a dark colour, so brightness is applied twice over — the picture lands
near luma squared.

Brightening the **source** doesn't fix it: the selector just answers with denser glyphs and
you end up in the same place. It has to be corrected on the grid, after selection.
`--grid-gamma 0.7` recovers roughly a third of the gap; below about `0.5` it stops
brightening and starts washing out.

---

## 10. Backdrop

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--backdrop` | `MODE` | `none` | `auto` · `none` (black) · `#RRGGBB` |
| `--backdrop-darken` | `F` | `0.15` | `auto` only: how far down from the picture's own colour |
| `--backdrop-luma-threshold` | `F` | `40` | `auto` only: cells dimmer than this don't count toward the average |

`auto` takes the mean colour of every cell bright enough to be *subject* rather than
background, then darkens it. On a blue car you get a deep teal; on a sunset, a dark amber. The
threshold is the part that matters — averaging every cell would give you near-black on any
picture with a dark background, whatever the subject looked like.

The backdrop is filled into the grid **and** used as the padding colour, so blanked cells and
the border around the picture always match.

---

## 11. Output

| Flag | Arg | Default | What it does |
|---|---|---|---|
| `--out` | `PATH` | — | Write here. **Repeatable** — every one is produced |
| `--stdout` / `--no-stdout` | — | auto | Print to the terminal. On by default when no `--out` is given |
| `--color` | `MODE` | `truecolor` | `truecolor` · `ansi16` · `none` |
| `--overwrite` | — | off | Allow replacing an existing file |

Format comes from the extension:

| Extension | What you get |
|---|---|
| `.png` `.jpg` | The glyphs drawn back into a picture. Honours `--image-*` |
| `.ans` | Text with colour escape codes. Displays in a terminal |
| `.txt` | Characters only, no colour |

`--out a.png --out a.ans --out a.txt` writes all three from one conversion.

---

## 12. Overrides, not conflicts

The pieces are independent enough that almost every combination means something, so **there
are no combination errors.** More specific beats less specific, later beats earlier, and
nothing is refused.

- `--charset-chars` beats `--charset`; `--charset-range` unions on top
- `--image-width` beats `--font-render-size`, which beats the default
- `--grid-width`/`--grid-height` beat the terminal size
- flags beat presets, presets beat the config file, config beats defaults

The only hard failures are an input that can't be read and an output that can't be written.

A short list of things that are legal but won't do what you hoped, and warn once:

| Combination | What happens |
|---|---|
| `--edge` with a charset missing `/ \ | -` | The missing characters are appended to the set. No warning; this is expected |
| `--algo ramp` with an unordered charset | Brightness maps to arbitrary characters |
| `--grid-palette` with `--color none` | Nothing carries colour to the output |
| `--algo-allow-background` with `.txt` output | The second colour is discarded |
| `--dither` with `--algo ramp` and a tiny charset | Little to dither between |

---

## 13. Presets

`--preset NAME` expands to a flag set. Anything you pass explicitly still wins.

| Preset | Expands to |
|---|---|
| `wallpaper-center` | `photo` + `--font-render-size 64 --grid-height 35 --image-aspect 16:9 --image-scale 0.6 --backdrop-darken 0.115` |
| `wallpaper-cover` | `photo` + `--font-render-size 64 --grid-height 80` |
| `terminal` | `--algo structure --color truecolor --stdout` |
| `lineart` | `--algo ramp --charset ramp --edge scharr --edge-threshold 0.2 --color none` |
| `poster` | `--algo bitmask --algo-allow-background --charset blocks --image-width 2400 --backdrop auto` |
| `plain` | `--algo ramp --charset ramp --color none` |
| `gruvbox` | `--algo structure --grid-palette gruvbox --backdrop #282828` |

---

## 14. Worked examples

```sh
# Default: structure, ASCII, truecolor, sized to the terminal
asciigen photo.jpg

# Desktop wallpaper, backdrop picked from the photo
asciigen photo.jpg --preset wallpaper-center --out ./output

# Exactly 1900x1200, nothing cropped, art centred
asciigen photo.jpg --out art.png --image-width 1900 --image-height 1200 \
                   --image-fit contain --image-align center

# Bold outlines rather than smooth shading
asciigen photo.jpg --algo-structure-orientation-weight 0.8 --source-sharpen 0.8

# Highest fidelity, least "texty" — two colours per cell
asciigen photo.jpg --algo bitmask --algo-allow-background --charset blocks --out art.png

# Plain text, no colour, fixed width
asciigen photo.jpg --algo ramp --charset ramp --color none --grid-width 100 --out art.txt

# Terminal plus two files in one pass
asciigen photo.jpg --out art.png --out art.ans --stdout

# Print an .ans file the way it was meant to look
asciigen art.ans
```

---

## 15. Known gaps

Things that are true today and worth knowing before they surprise you.

**`--algo-bitmask-softness` measured worse, not better.** It was added to make matching
tolerant of a stroke sitting a pixel off. Every value above 0 scored monotonically worse, on
both charsets, at both radii. Kept at `0` and left in place as a dial, not removed, because
the mechanism is sound even though this dose of it isn't.

**The quality metric can't judge dithering or vibrance.** Reconstruction SSIM compares against
the un-dithered source, so it marks dithering down every time even where dithering clearly
looks better. Vibrance measures as *over*-saturated because coloured ink against a dark
backdrop reads as extremely saturated in RGB. Use the metric to compare **algorithms**; judge
modifiers by eye.

**Source-filter radii are in source pixels.** `--source-sharpen` and `--source-blur` take a
radius measured on the *original* image, and they run before it is resampled down to the
grid. Radius 1 on a 1920px-wide photo is a strong effect; the same radius on a 6914px-wide
one is nearly invisible by the time it reaches a 160-cell grid. Everything downstream —
`--dither-*`, `--edge-threshold`, `--backdrop-luma-threshold` — works on the resampled plane
or on cells, so those are already resolution-independent. Only these two are not. The fix
would be to express the radius as a fraction of image width, or to resample before filtering.
Neither is built.

**`GlyphAtlas` has no move or copy semantics.** It owns a raw pointer with a destructor, so
the implicitly generated copy assignment would double-free. In practice this means an atlas
cannot be reassigned — build it at the point it is needed instead. Worth fixing when the app
layer starts constructing atlases conditionally.

---

## 16. Config file

Same names as the flags, dashes to underscores, grouped by prefix. TOML.

Searched in order, later winning: `%APPDATA%\asciigen\config.toml` (or
`$XDG_CONFIG_HOME/asciigen/config.toml`), then the nearest `.asciigen.toml` walking up from
the current directory, then `--config PATH`.

```toml
[font]
path        = "C:/Windows/Fonts/CascadiaMono.ttf"
match_size  = 16

[charset]
name = "ascii"

[algo]
name = "structure"

[algo.structure]
orientation_weight = 0.25
mass_weight        = 1.0
tone_weight        = 4.0

[dither]
name     = "bayer4"
levels   = 4
adaptive = true

[grid]
gamma     = 0.7
vibrance  = 0.4
despeckle = 0.1

[backdrop]
mode = "auto"

[image]
fit   = "contain"
align = "center"

[preset.mine]              # your own presets
algo         = "structure"
grid_palette = "nord"
```

| Flag | What it does |
|---|---|
| `--config PATH` | Use this file |
| `--no-config` | Ignore every config file |
| `--explain` | Print the resolved settings and where each came from, then exit |

### Precedence

| Rank | Source |
|---:|---|
| 1 | Built-in defaults |
| 2 | User config |
| 3 | Project config (`.asciigen.toml`) |
| 4 | `--config PATH` |
| 5 | `--preset NAME` |
| 6 | Command-line flags |

---

## 17. Options structs

One struct per group. No `std::optional` — every field has a real default, so `Options{}` is
already valid and "unset" is a sentinel (`0`, `-1`, empty string).

Fields marked **→** already exist in the engine and are passed straight through.

### InputOptions
| Field | Type | Default |
|---|---|---|
| path | `string` | — |
| passthrough | bool | auto (from extension) |

### SourceOptions → `ImageFilters`
| Field | Type | Default |
|---|---|---|
| auto_levels | bool | false |
| auto_levels_low / high | float | 0.5 / 99.5 |
| levels_enabled | bool | false |
| levels_black / white / gamma | float | 0 / 255 / 1 |
| contrast | float | 1.0 |
| sharpen_amount | float | 0 (off) |
| sharpen_radius | int | 1 |
| blur_radius | int | 0 (off) |

### FontOptions → `GlyphAtlas`
| Field | Type | Default |
|---|---|---|
| path | `string` | empty (built-in) |
| match_size | int | 16 |
| render_size | int | 0 (auto) |

### CharsetOptions → `Charset`
| Field | Type | Default |
|---|---|---|
| name | enum: ascii/blocks/ramp/custom | ascii |
| chars | `string` | empty |
| ranges | `{first,last}[]` | empty |

### GridOptions → `CellBuffer`, `CellFilters`
| Field | Type | Default |
|---|---|---|
| width / height | int | 0 (auto) |
| brightness | float | 1.0 |
| gamma | float | 1.0 |
| vibrance | float | 0 |
| palette | enum: none/gruvbox/nord | none |
| palette_strength | float | 1.0 |
| despeckle | float | 0.1 |

### DitherOptions → `Dithering::Options`
| Field | Type | Default |
|---|---|---|
| name | enum: none/bayer4 | none |
| levels | int | 4 |
| adaptive | bool | true |
| flat_contrast | float | 10 |
| edge_contrast | float | 45 |

### EdgeOptions → `Edges::Options`
| Field | Type | Default |
|---|---|---|
| name | enum: none/scharr | none |
| subsamples | int | 4 |
| threshold | float | 0.3 |
| coherence | float | 0.55 |

### AlgoOptions → `BitmaskOptions`, `StructureOptions`, `CellColorOptions`
| Field | Type | Default |
|---|---|---|
| name | enum: ramp/bitmask/structure | structure |
| allow_background | bool | false |
| brightness_gamma | float | 1.0 |
| bitmask_softness | float | 0 |
| bitmask_blur_radius | int | 1 |
| structure_orientation_weight | float | 0.25 |
| structure_mass_weight | float | 1.0 |
| structure_tone_weight | float | 4.0 |
| structure_orient_blocks | `{w,h}` | `{2,4}` |
| structure_mass_blocks | `{w,h}` | `{8,16}` |
| structure_bins | int | 4 |

### BackdropOptions → `CellBuffer::suggestedBackground`
| Field | Type | Default |
|---|---|---|
| mode | enum: none/auto/fixed | none |
| color | `RGB` | `{0,0,0}` |
| darken | float | 0.15 |
| luma_threshold | float | 40 |

### OutputOptions → `ImageRenderOptions`, `AnsiRenderOptions`
| Field | Type | Default |
|---|---|---|
| paths | `string[]` | empty |
| stdout_enabled | tristate | auto |
| color | enum: truecolor/ansi16/none | truecolor |
| overwrite | bool | false |
| image_width / height | int | 0 (natural) |
| image_fit | enum: none/width/height/contain/cover/stretch | contain |
| image_align | enum: 9 positions | center |

### Top-level
| Field | Type | Default |
|---|---|---|
| config_path | `string` | empty |
| no_config | bool | false |
| presets | `string[]` | empty |
| explain | bool | false |
| input / source / font / charset / grid / dither / edge / algo / backdrop / output | the structs above | — |

---

## 18. What the engine already provides

The app layer is a parser and a dispatcher. Everything it calls exists:

| Stage | Call |
|---|---|
| Load | `ImageManager::loadImage` |
| Adjust | `ImageFilters::autoLevels` / `levels` / `contrast` / `unsharpMask` / `blur` |
| Dither | `Dithering::options` + `Dithering::apply` (called inside each selector) |
| Select | `Ramp::generate` / `Bitmask::generate` / `Structure::generate` |
| Edges | `Edges::options` + `Edges::apply` — takes `Charset&`, returns whether it grew |
| Grade | `CellFilters::brightness` / `vibrance` / `paletteMap` / `despeckle` |
| Backdrop | `CellBuffer::suggestedBackground` + `fillBackground` |
| Write text | `AnsiRenderer::render` + `OutputManager::saveAns` |
| Write image | `ImageRenderer::render` → `scale` → `compose`, or `save` |
| Terminal | `Terminal::enableAnsi` / `enableUtf8` / `isTty` / `getSize` |

`ImageRenderer` is deliberately split into three so sizing is composable:
`render()` gives the natural size, `scale()` resamples it, `compose()` places it on a canvas
with alignment and a backdrop. `renderToSize()` runs all three per `ImageRenderOptions`, and
`suggestedGlyphHeight()` is the back-solve described in [Sizing](#6-sizing-cells-glyphs-and-pixels).
