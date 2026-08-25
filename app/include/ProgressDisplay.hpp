#pragma once

#include "core/FrameProgress.hpp"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

// Draws live progress to the terminal without ever touching the thing being
// measured. Two halves, deliberately separate:
//
//   - Whatever is doing the work (FrameProcessor::run today; a video worker
//     thread later) only ever writes to its own FrameProgress -- a couple of
//     relaxed atomics, no lock, no knowledge that anything is watching.
//   - Renderer/Watcher here only ever read those atomics and draw. Nothing in
//     this file is called from the thread doing the actual frame processing.
//
// That split is what makes this the same system for a still image and a future
// multithreaded video: a still image is one Line, drawn by a Watcher polling one
// FrameProgress; video will be one Line per worker plus an aggregate line, drawn
// by the same Watcher polling more of them. Nothing here has to change shape when
// that lands -- only how many Lines get constructed.
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

// Polls a fixed set of lines on its own thread and redraws them at a fixed
// interval, so the thread doing the actual work never has to pause to report
// itself. Starts polling on construction, stops and joins on destruction --
// RAII, so a normal return or an early one both clean it up the same way.
class Watcher
{
  public:
    explicit Watcher(std::vector<Line> lines, int intervalMs = 80);
    ~Watcher();

    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    // Stops polling and draws one final frame -- call before reading whatever the
    // watched work produced, so the terminal isn't still being written to by the
    // background thread when the caller moves on.
    void stop();

  private:
    std::vector<Line> m_lines;
    Renderer m_renderer;
    std::atomic<bool> m_running {true};
    std::thread m_thread;
};

}   // namespace ProgressDisplay
