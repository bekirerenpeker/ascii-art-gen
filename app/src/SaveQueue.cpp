#include "SaveQueue.hpp"
#include "core/Image.hpp"
#include <algorithm>
#include <string>

template <typename T>
SaveQueue<T>::SaveQueue(int capacity) : m_capacity(std::max(1, capacity))
{
}

template <typename T>
void SaveQueue<T>::push(int frameIndex, T&& value)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [&] {
        return (int)m_pending.size() < m_capacity || frameIndex == m_nextToWrite;
    });

    m_pending.emplace(frameIndex, std::move(value));

    lock.unlock();
    m_cv.notify_all();
}

template <typename T>
bool SaveQueue<T>::popNextInOrder(T& outValue)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [&] { return m_pending.count(m_nextToWrite) > 0 || m_closed; });

    auto it = m_pending.find(m_nextToWrite);
    if (it == m_pending.end()) return false;   // closed, and this frame is never coming

    outValue = std::move(it->second);
    m_pending.erase(it);
    m_nextToWrite++;

    lock.unlock();
    // Wakes a worker blocked on capacity (a slot just freed) and any worker
    // blocked because it wasn't yet the next one due -- it might be now.
    m_cv.notify_all();
    return true;
}

template <typename T>
void SaveQueue<T>::close()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
    }
    m_cv.notify_all();
}

template class SaveQueue<Image>;
template class SaveQueue<std::string>;
