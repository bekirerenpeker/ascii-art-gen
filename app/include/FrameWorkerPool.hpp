#pragma once

#include "FrameProcessor.hpp"
#include "Options.hpp"
#include "core/FramePool.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

// Spawns and owns a fixed set of worker threads, each running the exact same
// loop no matter how many there are: pop a ready slot from the pool, run
// FrameProcessor::run against it, mark it Done, repeat until the pool's queue
// closes and drains. A still image runs this with workerCount=1 against a
// 1-slot FramePool -- the identical code path video uses with more of both,
// not a separate single-threaded mode kept around for comparison.
//
// Everything except the frame processing itself stays off these threads: no
// drawing, no loading, no saving, no pool bookkeeping beyond the one slot index
// each pop hands back. The calling thread owns submission, progress display,
// and waiting for completion -- see Pipeline.cpp and ProgressDisplay::runUntilDone.
namespace FrameWorkerPool {

class Manager
{
  public:
    Manager(
        FramePool& pool, const App::Options& opts, const FrameProcessor::Context& ctx, int workerCount
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

  private:
    FramePool& m_pool;
    const App::Options& m_opts;
    const FrameProcessor::Context& m_ctx;
    int m_workerCount;
    std::unique_ptr<std::atomic<int>[]> m_currentSlot;   // one per worker, -1 = idle
    std::vector<std::thread> m_workers;
    bool m_joined = false;
};

}   // namespace FrameWorkerPool
