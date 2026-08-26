#pragma once

#include "FrameProcessor.hpp"
#include "Options.hpp"
#include "core/FramePool.hpp"
#include "output/AnsiRenderer.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Spawns and owns a fixed set of worker threads, each running the exact same
// loop no matter how many there are: pop a ready slot from the pool, run
// FrameProcessor::run against it (rendering into a buffer local to this call,
// not into the slot -- see FrameStorage.hpp's own note on why), free the slot
// immediately, then hand the rendered result to `onRendered` if one was given.
// A still image runs this with workerCount=1 against a 1-slot FramePool -- the
// identical code path video uses with more of both, not a separate
// single-threaded mode kept around for comparison.
//
// Everything except the frame processing itself stays off these threads: no
// drawing, no loading, no saving beyond however `onRendered` chooses to hand
// the result onward, no pool bookkeeping beyond the one slot index each pop
// hands back. The calling thread owns submission, progress display, and
// waiting for completion -- see Pipeline.cpp and ProgressDisplay::runUntilDone.
namespace FrameWorkerPool {

// Called on a WORKER thread, right after that worker's slot is already freed --
// so this can block (a video's callback pushes to a capacity-bounded SaveQueue,
// see Pipeline.cpp) without holding up the decoder or any other worker. Not
// called at all for a still image unless one is explicitly given.
//
// `text` is only ever non-empty when the Manager was constructed with a
// `textOptions` (see below) -- a pixel-video callback ignores it, a still
// image doesn't get one at all (it reads FrameStorage::text straight off the
// one slot afterward instead, safe there since nothing ever reuses it).
using OnRendered = std::function<void(int frameIndex, Image&& rendered, std::string&& text)>;

// Why a worker with currentSlot() < 0 is idle -- currentSlot alone can't tell
// these apart, since it's cleared before onRendered() runs too (see the
// worker loop's own note on why). Read by the progress display so "idle"
// can say what it's actually idle ON, not just that it is.
enum class WorkerState
{
    WaitingForWork,   // blocked in FramePool::nextReady -- nothing decoded yet to give it
    Processing,       // running FrameProcessor::run against a claimed slot
    HandingOff        // slot already freed; blocked inside onRendered (e.g. a full SaveQueue)
};

class Manager
{
  public:
    // `textOptions`, when given, has each worker render its own text/ANSI
    // string (from the slot's CellBuffer, before the slot is freed) and hand
    // it to `onRendered` alongside the pixel result -- see OnRendered's own
    // note. A pixel-video or still-image run leaves this unset; a text/ANSI
    // video run sets it to exactly the AnsiRenderOptions that output format
    // needs (see Pipeline.cpp's runVideo), including screenControls so the
    // saved frames are already playback-ready.
    Manager(
        FramePool& pool, const App::Options& opts, const FrameProcessor::Context& ctx, int workerCount,
        OnRendered onRendered = nullptr,
        std::optional<AnsiRenderer::AnsiRenderOptions> textOptions = std::nullopt
    );

    // Closes the pool's queue (so idle workers wake up and exit) and joins
    // every thread. Safe to call once explicitly before reading results; the
    // destructor calls it too if it wasn't, so an early return can't leak a
    // running thread either.
    ~Manager();
    void join();

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    int workerCount() const { return m_workerCount; }

    // Which slot worker `i` is currently processing, or -1 if it's idle
    // (waiting for work, or between the pool closing and the thread exiting).
    // Read by the progress display -- see Pipeline.cpp's per-worker Lines --
    // never by anything on the worker's own critical path.
    int currentSlot(int workerIndex) const
    {
        return m_currentSlot[workerIndex].load(std::memory_order_relaxed);
    }

    // What worker `i` is doing right now -- meaningful even when currentSlot()
    // is -1, since that alone can't distinguish "waiting on the decoder" from
    // "waiting to hand its finished frame to the saver". Read by the progress
    // display, same as currentSlot().
    WorkerState workerState(int workerIndex) const
    {
        return m_workerState[workerIndex].load(std::memory_order_relaxed);
    }

  private:
    FramePool& m_pool;
    const App::Options& m_opts;
    const FrameProcessor::Context& m_ctx;
    OnRendered m_onRendered;
    std::optional<AnsiRenderer::AnsiRenderOptions> m_textOptions;
    int m_workerCount;
    std::unique_ptr<std::atomic<int>[]> m_currentSlot;   // one per worker, -1 = idle
    std::unique_ptr<std::atomic<WorkerState>[]> m_workerState;   // one per worker
    std::vector<std::thread> m_workers;
    bool m_joined = false;
};

}   // namespace FrameWorkerPool
