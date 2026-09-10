#pragma once

#include "core/FrameStorage.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

// What stage of its own lifecycle one pool slot is in. A still image walks this
// exact sequence once, on its one slot; video cycles many frames through a
// handful of slots the same way -- one lifecycle per FRAME, not per slot.
//
// Done is momentary, not a resting state anything waits on: whoever finishes a
// slot's processing extracts whatever it needs (the rendered frame; the
// frame's own index) and moves it to Free again immediately afterward, in the
// same breath -- see FrameWorkerPool::Manager. It exists purely so a live
// per-slot status display has something to show between Processing and Free,
// not because any consumer blocks waiting for it.
enum class FrameSlotState
{
    Free,         // available for a new frame's input to be loaded into
    Ready,        // input loaded, queued for a worker to pick up
    Processing,   // claimed by a worker; FrameProcessor::run is running
    Done          // momentary: processing just finished, about to go Free
};

struct FrameSlot
{
    FrameStorage storage;
    std::atomic<FrameSlotState> state {FrameSlotState::Free};
    int frameIndex = -1;   // which frame (by sequence) this currently holds -- video's ordering, unused for a still
};

// A fixed-size array of frame slots plus the queue that hands work to whatever
// workers exist -- one object holding everything a worker needs pointed at it,
// so a worker only ever touches the ONE slot index it was handed, never this
// pool's other bookkeeping directly.
//
// Two producer/consumer relationships share the same mutex and condition
// variable rather than getting one each: a loader waits for a Free slot and a
// worker's markFree() wakes it; a worker waits for a Ready slot and a loader's
// submit() wakes it. Every state-changing call notifies ALL waiters, not just
// the one kind that logically cares, which costs a couple of pointless
// wakeups per frame in exchange for being unable to miss one by picking the
// wrong condition_variable to notify -- a bad trade at real contention, a
// good one at "once per frame."
//
// Notably absent: anything a SAVER waits on. That used to live here (a Done
// queue a save step blocked on), but it meant a slot -- input, plane, every
// scratch buffer, all of it -- stayed occupied until that consumer got around
// to it. Now the worker itself extracts the rendered result and frees the
// slot in the same breath processing finishes; what happens to the render
// result afterward (see FrameWorkerPool::Manager's onRendered callback, and
// SaveQueue for video specifically) is a separate, much lighter-weight
// handoff that doesn't hold this pool's slots hostage.
//
// Allocated once, up front, with a single call: `slotCount` is worker count
// plus slack for video, exactly 1 for a still image -- same class, same call
// shape, just a different count. Slots are held as unique_ptr specifically
// because FrameSlot owns an atomic (non-movable) on top of FrameStorage's own
// non-movable Image members -- a plain std::vector<FrameSlot> couldn't grow
// without moving elements; a vector of unique_ptr never needs to.
class FramePool
{
  public:
    void allocate(int slotCount, int cols, int rows, int planeW, int planeH);

    int slotCount() const { return (int)m_slots.size(); }
    FrameSlot& slot(int index) { return *m_slots[index]; }

    // --- Loader side (main thread for a still image; a decode thread for video) ---

    // Blocks until some slot is Free, and returns its index without changing its
    // state -- there is only ever one loader, so nothing else can claim it
    // between this call returning and the matching submit() below. Returns -1
    // only if the pool is closed before any slot ever frees, which a real
    // loader should never see.
    int waitForFreeSlot();

    // Called once a slot's storage.input has been filled and is ready for a
    // worker. `frameIndex` is the frame's sequence number, used downstream to
    // write frames back out in order regardless of which finishes processing
    // first; pass -1 (the default) for the still-image case, which has no
    // ordering to preserve.
    void submit(int slotIndex, int frameIndex = -1);

    // No more submit() calls coming. Wakes every thread blocked in nextReady()
    // so idle workers can notice there is nothing left to ever arrive.
    void closeQueue();

    // --- Worker side ---

    // Blocks until a submitted slot is available, or the queue has been closed
    // and drained -- the latter is a worker's signal to stop. Marks the
    // returned slot Processing before returning it.
    bool nextReady(int& outSlotIndex);

    // Sets a slot to Done -- purely observational, see FrameSlotState's own
    // note -- then to markFree(): call both, back to back, once
    // FrameProcessor::run returns and whatever's needed from the slot has
    // been extracted.
    void markDone(int slotIndex);

    // Moves a slot back to Free and wakes whatever is waiting in
    // waitForFreeSlot(). Called immediately after markDone() by whoever just
    // finished processing -- there is no separate consumer this waits on.
    void markFree(int slotIndex);

  private:
    std::vector<std::unique_ptr<FrameSlot>> m_slots;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<int> m_readyQueue;
    bool m_closed = false;
};
