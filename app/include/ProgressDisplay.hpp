#pragma once

#include <functional>
#include <string>
#include <vector>

// Draws live progress to the terminal without ever touching the thing being
// measured. Two halves, deliberately separate:
//
//   - Whatever is doing the work (FrameProcessor::run, on a FrameWorkerPool
//     worker thread) only ever writes to its own FrameProgress -- a couple of
//     relaxed atomics, no lock, no knowledge that anything is watching.
//   - Renderer/runUntilDone here only ever read state and draw. Nothing in
//     this file runs on the thread doing the actual frame processing -- it
//     runs on whichever thread is otherwise just waiting for that work to
//     finish, which is the calling thread's own job either way.
//
// That split is what makes this the same system for a still image and video: a
// still image is one Line reading FrameWorkerPool's one worker; video is one
// Line per worker plus an aggregate line. Nothing here changes shape between
// them -- only how many Lines get constructed and what each one's `read`
// closure looks at.
namespace ProgressDisplay {

// What one line shows at the moment it's drawn. A plain snapshot, not a
// pointer into anything live: a video worker moves between different frame
// slots over its lifetime, so which FrameProgress a line even reads from can
// change between one redraw and the next -- Line's `read` closure re-resolves
// that itself every call rather than a Line being bound to one fixed source.
struct Snapshot
{
    std::string label;
    std::string stage;
    float fraction = 0.f;
};

struct Line
{
    std::function<Snapshot()> read;
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
// workers -- and, for video, a decode thread and a save thread -- do the
// actual work), so no thread beyond those has to exist for the bar to move.
// Draws one final frame and erases before returning, so nothing stale is left
// on screen.
void runUntilDone(
    const std::function<bool()>& isDone, const std::vector<Line>& lines, int intervalMs = 80
);

}   // namespace ProgressDisplay
