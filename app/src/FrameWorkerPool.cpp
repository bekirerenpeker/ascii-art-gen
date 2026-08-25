#include "FrameWorkerPool.hpp"

namespace FrameWorkerPool {

Manager::Manager(
    FramePool& pool, const App::Options& opts, const FrameProcessor::Context& ctx, int workerCount
)
    : m_pool(pool), m_opts(opts), m_ctx(ctx)
{
    m_workers.reserve(workerCount);
    for (int i = 0; i < workerCount; i++) {
        m_workers.emplace_back([this] {
            int slotIndex;
            while (m_pool.nextReady(slotIndex)) {
                FrameSlot& slot = m_pool.slot(slotIndex);
                FrameProcessor::run(slot.storage, m_opts, m_ctx);
                slot.state.store(FrameSlotState::Done, std::memory_order_release);
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
