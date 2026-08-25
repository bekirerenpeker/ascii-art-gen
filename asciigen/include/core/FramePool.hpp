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
enum class FrameSlotState
{
    Free,         // available for a new frame's input to be loaded into
    Ready,        // input loaded, queued for a worker to pick up
    Processing,   // claimed by a worker; FrameProcessor::run is running
    Done          // finished; result is ready for the consumer to read/save
};

struct FrameSlot
{
    FrameStorage storage;
    std::atomic<FrameSlotState> state {FrameSlotState::Free};
    int frameIndex = -1;   // which frame (by sequence) this currently holds -- video's ordering, unused for a still
};

// A fixed-size array of frame slots plus the queues that move work between
// whatever produces frames, whatever processes them, and whatever consumes the
// results -- one object holding everything those three roles need pointed at
// them, so each only ever touches the ONE slot index it was handed, never this
// pool's other bookkeeping directly.
//
// Three producer/consumer relationships share the same mutex and condition
// variable rather than getting one each: a loader waits for a Free slot and a
// worker's markFree() wakes it; a worker waits for a Ready slot and a loader's
// submit() wakes it; a saver waits for a Done slot and a worker's markDone()
// wakes it. Every state-changing call notifies ALL waiters, not just the one
// kind that logically cares, which costs a handful of pointless wakeups per
// frame in exchange for being unable to miss one by picking the wrong
// condition_variable to notify -- a bad trade at real contention, a good one
// at "once per frame."
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
    // worker. `frameIndex` is the frame's sequence number, used by the saver to
    // write frames back out in order regardless of which finishes processing
    // first; pass -1 (the default) for the still-image case, which has no
    // ordering to preserve.
    void submit(int slotIndex, int frameIndex = -1);

    // No more submit() calls coming. Wakes every thread blocked in nextReady()
    // or waitForDone() so they can notice there is nothing left to ever arrive.
    void closeQueue();

    // --- Worker side ---

    // Blocks until a submitted slot is available, or the queue has been closed
    // and drained -- the latter is a worker's signal to stop. Marks the
    // returned slot Processing before returning it.
    bool nextReady(int& outSlotIndex);

    // Called once FrameProcessor::run returns. Moves the slot to Done and wakes
    // whatever is waiting in waitForDone().
    void markDone(int slotIndex);

    // --- Saver side (main thread for a still image; a dedicated thread for video) ---

    // Blocks until some slot is Done, or every slot that could ever reach Done
    // already has and nothing more is coming (queue closed, nothing Ready or
    // Processing) -- the latter returns -1, a saver's signal to stop.
    int waitForDone();

    // Called once the saver has copied whatever it needs out of a Done slot.
    // Moves it back to Free and wakes whatever is waiting in waitForFreeSlot().
    void markFree(int slotIndex);

    // --- Whole-batch case (a still image; a fixed one-shot batch of frames) ---

    // True once every slot has reached Done. Not used by the video path, which
    // recycles slots through markFree() instead of leaving them Done forever.
    bool allDone() const;

  private:
    bool anyProcessingLocked() const;

    std::vector<std::unique_ptr<FrameSlot>> m_slots;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<int> m_readyQueue;
    std::queue<int> m_doneQueue;
    bool m_closed = false;
};
