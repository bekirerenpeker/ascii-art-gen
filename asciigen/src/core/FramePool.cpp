#include "core/FramePool.hpp"

void FramePool::allocate(int slotCount, int cols, int rows, int planeW, int planeH)
{
    m_slots.clear();
    m_slots.reserve(slotCount);

    for (int i = 0; i < slotCount; i++) {
        auto s = std::make_unique<FrameSlot>();
        s->storage.allocate(cols, rows, planeW, planeH);
        m_slots.push_back(std::move(s));
    }
}

int FramePool::waitForFreeSlot()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        for (int i = 0; i < (int)m_slots.size(); i++)
            if (m_slots[i]->state.load(std::memory_order_acquire) == FrameSlotState::Free) return i;

        if (m_closed) return -1;
        m_cv.wait(lock);
    }
}

void FramePool::submit(int slotIndex, int frameIndex)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        FrameSlot& s = *m_slots[slotIndex];
        s.frameIndex = frameIndex;
        s.state.store(FrameSlotState::Ready, std::memory_order_release);
        m_readyQueue.push(slotIndex);
    }
    m_cv.notify_all();
}

bool FramePool::nextReady(int& outSlotIndex)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [&] { return !m_readyQueue.empty() || m_closed; });
    if (m_readyQueue.empty()) return false;   // closed and drained -- caller should stop

    outSlotIndex = m_readyQueue.front();
    m_readyQueue.pop();
    m_slots[outSlotIndex]->state.store(FrameSlotState::Processing, std::memory_order_release);
    return true;
}

void FramePool::markDone(int slotIndex)
{
    m_slots[slotIndex]->state.store(FrameSlotState::Done, std::memory_order_release);
}

void FramePool::markFree(int slotIndex)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slots[slotIndex]->state.store(FrameSlotState::Free, std::memory_order_release);
    }
    m_cv.notify_all();
}

void FramePool::closeQueue()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }
    m_cv.notify_all();
}
