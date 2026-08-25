#include "FrameWorkerPool.hpp"

namespace FrameWorkerPool {

Manager::Manager(
    FramePool& pool, const App::Options& opts, const FrameProcessor::Context& ctx, int workerCount
)
    : m_pool(pool), m_opts(opts), m_ctx(ctx), m_workerCount(workerCount)
{
    m_currentSlot = std::make_unique<std::atomic<int>[]>(workerCount);
    for (int i = 0; i < workerCount; i++) m_currentSlot[i].store(-1, std::memory_order_relaxed);

    m_workers.reserve(workerCount);
    for (int i = 0; i < workerCount; i++) {
        m_workers.emplace_back([this, i] {
            int slotIndex;
            while (m_pool.nextReady(slotIndex)) {
                m_currentSlot[i].store(slotIndex, std::memory_order_relaxed);

                FrameSlot& slot = m_pool.slot(slotIndex);
                FrameProcessor::run(slot.storage, m_opts, m_ctx);
                m_pool.markDone(slotIndex);

                m_currentSlot[i].store(-1, std::memory_order_relaxed);
            }
        });
    }
}

Manager::~Manager() { join(); }

void Manager::join()
{
    if (m_joined) return;
    m_joined = true;

    m_pool.closeQueue();
    for (std::thread& t : m_workers)
        if (t.joinable()) t.join();
}

}   // namespace FrameWorkerPool
