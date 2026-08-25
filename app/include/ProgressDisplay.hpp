#pragma once

#include "core/FrameProgress.hpp"
#include <functional>
#include <string>
#include <vector>

// Draws live progress to the terminal without ever touching the thing being
// measured. Two halves, deliberately separate:
//
//   - Whatever is doing the work (FrameProcessor::run, on a FrameWorkerPool
//     worker thread) only ever writes to its own FrameProgress -- a couple of
//     relaxed atomics, no lock, no knowledge that anything is watching.
//   - Renderer/runUntilDone here only ever read those atomics and draw.
//     Nothing in this file runs on the thread doing the actual frame
//     processing -- it runs on whichever thread is otherwise just waiting for
//     that work to finish, which is the calling thread's own job either way.
//
// That split is what makes this the same system for a still image and a future
// multithreaded video: a still image is one Line, read from FrameWorkerPool's
// one worker; video will be one Line per worker plus an aggregate line. Nothing
// here changes shape when that lands -- only how many Lines get constructed and
// what `isDone` checks.
namespace ProgressDisplay {

// One line's worth of state to render: whose progress this is (a still image's
// "frame", or later "thread 2") next to what FrameProgress to read it from.
// Renderer never owns the FrameProgress it points at -- it outlives every Line
// pointing at it, the same way FrameStorage outlives the processing that fills it.
struct Line
{
    std::string label;
    const FrameProgress* progress = nullptr;
};

// Draws a fixed set of lines, redrawing in place on every call instead of
// scrolling the terminal -- moves the cursor back up over its own last draw and
// overwrites it. Does nothing at all when stdout isn't a real terminal: a
// redirected/piped run would otherwise fill a log with cursor-control noise.
class Renderer
{
  public:
    void draw(const std::vector<Line>& lines);

    // Erases whatever this last drew and leaves the cursor there, so whatever
    // prints next (the ASCII art itself, an error) starts on a clean line instead
    // of sitting after a stale bar.
    void finish();

  private:
    int m_linesDrawn = 0;
};

// Draws `lines` on a fixed interval until `isDone` returns true, entirely on
// the CALLING thread -- this blocks the caller, which is the point: call it
// from whichever thread is otherwise just sitting there waiting for the real
// work to finish anyway (the main thread, while a FrameWorkerPool::Manager's
// workers do the actual processing), so no thread beyond the workers
// themselves has to exist for the bar to move. Draws one final frame and
// erases before returning, so nothing stale is left on screen.
void runUntilDone(
    const std::function<bool()>& isDone, const std::vector<Line>& lines, int intervalMs = 80
);

}   // namespace ProgressDisplay
