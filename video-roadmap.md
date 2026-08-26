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
exist before the reporting hook does.

**Design, decided ahead of building it.** A modular bar widget, proven first against the
simple case — one bar, for a single still image's run — before the video case adds real
complexity. For multithreaded video: one line per worker thread (which frame it's on, what
stage, progress within that frame) plus one aggregate line at the top (`n/m` frames done
overall). Multiple live lines need real cursor control (move up N lines, rewrite, move back
down) — a plain carriage-return-and-overwrite only works for a single line.

**Avoiding lock contention between a worker's real work and its own progress reporting**:
no shared mutex on progress state — that would reintroduce exactly the per-call lock
overhead `ASCIIGEN_PROFILE` was deliberately kept away from (`Profiler::enabled()` is a
relaxed atomic read for a reason). Each worker writes its own progress into its own relaxed
atomics, unlocked; a single redraw pass, throttled to roughly every 100-200ms rather than
once per update, is the only thing that reads across all of them and touches the terminal.
Progress updates at stage granularity (resample done, select done, render done), not
per-cell — same reasoning as why the profiler's own scopes stop at call granularity: finer
than that buys nothing visible and starts costing something real.

## 5. A frame pipe between rendering and file output

Right now `ImageRenderer`/`AnsiRenderer` render straight into a save call. For video this
needs to become a proper handoff between decode, per-frame processing, and file output,
decoupled enough that none of the three have to run in lockstep. Building this ahead of
item 6 — before there's a real frame loop to plug into it — means the loop gets written
against this interface from the start rather than having output bolted on after the fact.

**Fixed-size slot pool, not a queue of frames.** A fixed array of N "frame workspace"
slots (N = worker count + 2-3 slack), each persistent for the life of the run and holding
every big, expensive-to-allocate per-frame buffer together: the decoded input frame, the
resampled plane, the `CellBuffer`, the rendered output. Allocated once each, lazily, on
first use — after warm-up, zero further reallocation, the same principle item 6 already
wants for the algorithm-internal scratch buffers, just one level up. Kept as a *fixed*
array specifically so the pool itself can never move or reallocate the buffers — only a
slot index and a state travel through any queue, never the buffers themselves.

Not every slot needs every buffer actually allocated: for a text/ANSI-only output run, the
rendered-output member of every slot just stays at `Image`'s own default-constructed empty
state, since nothing ever calls the pixel-render step for that format. One struct shape
covers every output format without a special case.

**Per-slot state**, not a plain claimed/free flag, since the decoder, a worker, and the
save step can all be touching different slots at once: free → being decoded into → ready
for a worker → claimed → done-with-input → saved. "Done-with-input" is deliberately its
own state before "saved" — a worker only needs the source pixels for the early part of its
own work (resample, then glyph/colour selection reads them); everything after that works
off the `CellBuffer` and render output, not the original frame, so the input buffer can
recycle back to the decoder well before that worker has finished the frame end to end.
Worth a live terminal view of this per-slot state during a run — one more thing item 4's
progress bar can show, once threading exists to make it interesting.

**Distribution.** One decode thread is the only thing that ever calls
`VideoReader::nextFrame` — it cannot be called concurrently (see the video-loading design:
decode is inherently sequential, one packet/frame stream of internal state). It claims a
free slot, decodes into it, and pushes the slot's index onto a *bounded* ready-queue.
Worker threads pop an index, run the whole per-frame pipeline (item 6) against that slot's
buffers, and mark the input buffer recyclable once they're actually done reading it.
Bounded, not unbounded, on purpose: an unbounded queue lets a decoder that's faster than
the workers race ahead and decode the entire video before processing catches up, which
defeats the reason this is a streaming design at all.

**Ordered save.** Workers finish frames in whatever order they finish — but a video's
frames have to reach the muxer in order; FFmpeg's encoder genuinely requires this, it's
not a style preference. So: a `lastSaved` counter starts at -1. When a worker reports
frame N done, and `N == lastSaved + 1`, it's written immediately, `lastSaved` advances, and
the pending set is checked again for the new `lastSaved + 1` — cascading through however
many already-finished frames were waiting on this one gap closing. Otherwise the finished
frame goes into a small `pending` map keyed by frame index until its gap closes. This only
ever needs to hold as many entries as the slot pool has in flight, so its size is bounded
by the same N as the pool above — the two should be sized together, not treated as
independent.

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

**Stills and video become the same code path, not two.** The per-frame sequence (resample
→ dither → algorithm select → edges → grade → render) gets pulled out as its own function
operating on one of item 5's workspace slots, called identically whether it's driven by a
still image or a video worker thread. A still image really is the `N == 1` case then, not
just in spirit: `Pipeline::run` for a single image allocates one workspace, calls this
function once, saves the result — no separate still-image code path to let drift out of
sync with whatever the video path does.

## 7. Trivial optimizations + tests

Whatever falls out of actually running the loop from item 6 as a real thing — likely small
stuff (a scratch buffer that got missed, an unnecessary copy at the loop boundary). Test
coverage for the new video-specific pieces (fps sampling math, frame-interval selection,
format detection) at this point, once there's a stable shape to test against.

## 8. Basic multithreading

Iterating on a 10-second clip single-threaded is not a usable feedback loop, and
flicker/frame-to-frame consistency bugs only show up by actually watching output. The
actual worker-pool/slot-pool/ordered-save design is already written up under item 5 — this
item is building it, not designing it. Correctness (no flicker, no frame reordering)
matters more here than throughput; full multithreading tuning is its own later pass.

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

## 13. Audio passthrough

Not touched at all yet -- video in, video out currently drops any audio track entirely.
The goal is passthrough, not processing: whatever audio the source has should reach the
output unedited (no re-encoding artifacts introduced on purpose), just muxed alongside the
rendered video frames instead of being discarded.

Real complication: it has to stay in sync with whatever the video side did to get there.
`--start-time`/`--end-time`/`--start-frame`/`--end-frame` trim the video to a window: the
audio needs the same window cut from it, not the whole original track. `--fps` downsamples
which video frames get kept, but does NOT speed up or slow down playback -- the output's
wall-clock duration is unchanged (see VideoOptions' own note on why frames are only ever
dropped, never invented) -- so the audio track's own rate and duration need no adjustment
at all in that case, just needs to still be cut to the same trimmed window. Text/ANSI video
output has no channel for this at all, so this is scoped to pixel video output only.

## 14. Reduce frame-to-frame flicker

Confirmed real, not imagined: re-encoding an animation with flat, stable colour regions
and no real-world sensor noise (a Bad Apple-style clip) looks genuinely good, while a
live-action source visibly flickers even where the actual scene is static, because natural
sensor grain/lighting noise most human eyes read as "the same flat wall" is enough
per-pixel variation to flip the glyph a cell's dithered, selected against from one frame to
the next. Dithering isn't temporally coherent either -- its pattern has no notion of "what
did this cell look like last frame."

Worth investigating: some form of temporal hysteresis in glyph selection (biasing toward
the previous frame's choice for a cell unless a new candidate scores meaningfully better,
not just marginally), and/or a dithering approach that's stable across frames instead of
effectively fresh noise every time. Not attempted yet -- noted here as the natural next
thing to look at, not urgent (the flicker is present but described as only mildly
noticeable, not something blocking real use of the video output as it stands).

## 15. Parallel or segmented video encoding

The pixel-video save step is single-threaded by construction -- one `AVCodecContext`,
`writeFrame` called strictly in frame order -- and was observed to be the actual bottleneck
for most real clips: workers spend far more time blocked handing a finished frame to a full
`SaveQueue` than idle waiting on the decoder (see FrameWorkerPool::WorkerState -- this is
exactly what the "saving" label means). Growing the queue's capacity doesn't fix this, only
delays when the backpressure shows up, since the queue still fills at whatever rate the one
encoder drains it at.

A real fix means encoding more than one frame at a time. Two directions, neither tried yet:
- The encoder's own internal thread_count (distinct from the decoder-side one already tried
  and reverted, see VideoManager.cpp) -- worth testing, though the native mpeg4 encoder's
  threading support is limited, and every non-keyframe already depends on the reconstructed
  output of the one immediately before it, which caps how much frame-level parallelism is
  even possible at the current gop_size=2 (see its own note on why that value was chosen).
- Splitting a clip into independent segments, each encoded on its own thread/writer, then
  concatenated -- real complexity of its own (keyframe alignment at segment boundaries,
  exact frame-count bookkeeping, an actual concat step), not attempted.

---

## Open questions to resolve during item 1

- FFmpeg or a lighter combination of single-purpose libraries — depends partly on how much
  container/codec breadth actually gets used (item 9) versus how much is "nice to have."
- Licensing implications of whichever library is chosen, given this project ships source.
- Whether GIF gets its own lightweight path independent of the video library (GIF encode is
  cheap and well-trodden; may not be worth pulling in for it alone).
