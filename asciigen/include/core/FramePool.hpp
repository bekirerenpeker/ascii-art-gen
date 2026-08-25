#pragma once

#include "core/FrameStorage.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

// What stage of its own lifecycle one pool slot is in. A still image walks this
// exact sequence once, on its one slot; video will cycle many frames through a
// handful of slots the same way -- one lifecycle per FRAME, not per slot, once
// slots start being reused.
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

// A fixed-size array of frame slots plus the queue that hands work to whatever
// workers exist -- one object holding everything a worker needs pointed at it,
// so a worker only ever touches the ONE slot index it was handed, never this
// pool's other bookkeeping directly.
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

    // Called once a slot's storage.input has been filled and is ready for a
    // worker -- by whatever produces frames (the main thread today; a decode
    // thread for video, later).
    void submit(int slotIndex);

    // Blocks until a submitted slot is available, or the queue has been closed
    // and drained -- the latter is a worker's signal to stop. Marks the
    // returned slot Processing before returning it.
    bool nextReady(int& outSlotIndex);

    // No more submit() calls coming. Wakes every thread blocked in nextReady()
    // so idle workers can exit instead of waiting forever.
    void closeQueue();

    // True once every slot has reached Done -- the whole-batch completion check
    // a still image (and, for now, a fixed one-shot batch of frames) needs.
    // Continuous video streaming, once frames are recycled through fewer slots
    // than the total frame count, will want a different condition ("queue
    // empty and nothing Processing", not "every slot Done") -- not needed yet,
    // not built yet.
    bool allDone() const;

  private:
    std::vector<std::unique_ptr<FrameSlot>> m_slots;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<int> m_readyQueue;
    bool m_closed = false;
};
