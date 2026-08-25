# Video support — roadmap

> Planning document for `feature/video`. Nothing here is built yet. Items are listed in
> working order — pick any item off this list to implement next and the rest holds.

Today `asciigen` is a stills tool: one `Image` in, one `CellBuffer` out, one file (or a
terminal print) written. This branch's job is to lift that to video/GIF input and output
without turning the single-frame path into a special case of a more complicated one — a
still image should still be exactly what it is today, just the `N == 1` case of whatever
the frame loop becomes.

---

## 1. Pick and integrate a video I/O library

Research task before anything else gets built. `stb` (already vendored) only ever handled
still images — it has nothing for video containers or codecs.

- **FFmpeg (`libav*`)** is the obvious default: covers effectively every container/codec,
  battle-tested, but it's a big dependency to link against (LGPL/GPL licensing to check
  depending on which components get built in) and its C API is not small.
- Alternatives worth a quick look before committing: **libvpx**/**libwebp** alone if we only
  ever want VP8/VP9/animated WebP and not general MP4 support; **minimp4** or similar
  minimal muxers if we only need to *write* one container; GIF specifically has small
  standalone encoders/decoders (`giflib`, `stb_image` already reads animated GIF frames)
  that don't need touching FFmpeg at all.
- Output of this step: a decision, plus the library vendored/submoduled the same way
  FreeType is (`lib/`), plus a build check that CMake links it on all target platforms.

## 2. Video loading

A decode path that hands back frames as the same `Image` type `ImageManager::loadImage`
already produces for stills — so nothing downstream (resample, dither, algorithm selection)
needs to know or care that its input came from a video. Lives next to `ImageManager` in
`asciigen/src/file_management/`.

## 3. Video saving

The write side of the same thing — takes a sequence of rendered frames and muxes/encodes
them back into a video container.

## 4. Terminal progress bar

Built early, ahead of the full per-frame loop (item 6), because item 2 already gives a
frame count to report progress against — no need to wait for the rest of the pipeline to
exist before the reporting hook does. For video encode/render progress, so a multi-second-
to-multi-minute run isn't silent. Could apply to still-image runs too where the pipeline
already takes a moment (large `--font-render-size`, big source) — worth a look at the same
time, low cost if the underlying "how far through are we" hook already exists for video.

## 5. A frame pipe between rendering and file output

Right now `ImageRenderer`/`AnsiRenderer` render straight into a save call. For video this
needs to become a proper handoff: something that takes frames as they're produced and
queues them for the file-management side to consume and write out, decoupled enough that
producer and consumer don't have to run in lockstep. Building this ahead of item 6 — before
there's a real frame loop to plug into it — means the loop gets written against this
interface from the start rather than having output bolted on after the fact. It's also what
makes item 8's multithreading tractable later: a worker thread rendering frame N+1 while
another thread is still encoding frame N needs exactly this kind of queue between them, not
a redesign once threading is added.

## 6. Loop the existing pipeline over frames

`Pipeline::run` today is load → process → save, once. For a video/GIF input it should
detect that (by content or extension — same `passthrough`-style sniff `Pipeline.cpp`
already does for `.ans`) and run the existing per-frame processing in a loop instead,
handing each rendered frame to item 5's pipe rather than saving directly.

**No needless per-frame allocation** is explicit scope here, not an afterthought — the font,
atlas, and charset are already built once per `Pipeline::run` call; that has to stay true
per *video*, not per frame. Anything currently allocated as a scratch buffer inside a
per-image call (resample plane, dither scratch, tile/descriptor buffers in `Structure.cpp`
and `Bitmask.cpp`) needs to move to being allocated once outside the frame loop and reused,
the same pattern the profiling branch already used for T2's `massCountsScratch` and the
various `blur` scratch vectors.

## 7. Trivial optimizations + tests

Whatever falls out of actually running the loop from item 6 as a real thing — likely small
stuff (a scratch buffer that got missed, an unnecessary copy at the loop boundary). Test
coverage for the new video-specific pieces (fps sampling math, frame-interval selection,
format detection) at this point, once there's a stable shape to test against.

## 8. Basic multithreading

Iterating on a 10-second clip single-threaded is not a usable feedback loop, and
flicker/frame-to-frame consistency bugs only show up by actually watching output. Doesn't
need to be sophisticated — a worker pool over independent frames, feeding item 5's pipe, is
enough to get a test clip back in seconds instead of the better part of an hour, and
correctness (no flicker, no frame reordering) matters more here than throughput. Full
multithreading tuning is its own later pass, not this one.

## 9. Broad format support

Land the pipeline against **one** format first end to end (items 1-8, likely MP4 in and out,
since that's what FFmpeg is best at) to prove the shape works, then widen:

- Images: `png`, `jpg`, `webp`, and whatever else the chosen library set makes close to free
- Video: the other common containers/codecs FFmpeg covers beyond the first one picked
- GIF as its own thing — it's simultaneously an "image" (single frame) and a "video"
  (animated), so format detection needs to treat it as whichever the actual file contains,
  not by extension alone

## 10. Investigate replacing `stb_image_write`

Flagged as slow in practice already (see `--png-compression`'s own doc note about most of
PNG encode time being stb's row-filter search). Worth a real investigation once video
saving exists anyway and a faster encoder is already in the dependency tree for other
reasons — look at what's actually slow (row filtering vs the DEFLATE pass itself) before
picking a replacement, same way the `Resample::toGrid` stb-swap in the profiling branch was
measured rather than assumed.

## 11. Input/output format compatibility check

**Mandatory, and the one deliberate exception to how this project otherwise treats
input/output.** The general rule elsewhere is: even if a given input is unusable, or an
output path will be overwritten, let it through — errors get discovered by trying, not by
asciigen second-guessing the user's own file. But a structurally nonsensical
input/output pairing isn't a "might not work," it's a "there is no way to do this at all"
(video in, single image out, is one example — not the only one this check has to cover).
That class of mismatch gets checked and rejected up front; everything else keeps the
existing "let it try" philosophy.

## 12. Video-specific options (ongoing)

Less a single task than a running list, added alongside whichever item above needs a new
knob — user-facing ease-of-use options, added as the underlying capability lands rather
than designed up front as a fixed set. A previously-added option will often already cover
what a later item needs, so this shrinks as the branch goes rather than growing. Known
candidates so far, likely to be added piecemeal:

- **Output fps** — and this should only *process* the frames that will actually be
  rendered at that fps, not decode-and-discard every source frame. If the source is 60fps
  and output is 24fps, the decode/select loop should skip straight to the frames it needs.
- **Speed** — playback speed multiplier, independent of fps (a 2x-speed render at the same
  output fps drops frames; a 2x-speed render at 2x output fps doesn't).
- **Frame/time interval selection** — a start/end trim, by frame index or by timestamp.

---

## Open questions to resolve during item 1

- FFmpeg or a lighter combination of single-purpose libraries — depends partly on how much
  container/codec breadth actually gets used (item 9) versus how much is "nice to have."
- Licensing implications of whichever library is chosen, given this project ships source.
- Whether GIF gets its own lightweight path independent of the video library (GIF encode is
  cheap and well-trodden; may not be worth pulling in for it alone).
