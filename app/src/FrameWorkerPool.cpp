#include "FrameWorkerPool.hpp"

namespace FrameWorkerPool {

Manager::Manager(
    FramePool& pool, const App::Options& opts, const FrameProcessor::Context& ctx, int workerCount,
    OnRendered onRendered, std::optional<AnsiRenderer::AnsiRenderOptions> textOptions
)
    : m_pool(pool), m_opts(opts), m_ctx(ctx), m_onRendered(std::move(onRendered)),
      m_textOptions(textOptions), m_workerCount(workerCount)
{
    m_currentSlot = std::make_unique<std::atomic<int>[]>(workerCount);
    for (int i = 0; i < workerCount; i++) m_currentSlot[i].store(-1, std::memory_order_relaxed);

    m_workerState = std::make_unique<std::atomic<WorkerState>[]>(workerCount);
    for (int i = 0; i < workerCount; i++) m_workerState[i].store(WorkerState::WaitingForWork, std::memory_order_relaxed);

    m_workers.reserve(workerCount);
    for (int i = 0; i < workerCount; i++) {
        m_workers.emplace_back([this, i] {
            int slotIndex;
            // Set here, not just at the bottom of the loop below: the very
            // first wait, before any frame has ever arrived, needs the same
            // label as every wait after that.
            m_workerState[i].store(WorkerState::WaitingForWork, std::memory_order_relaxed);
            while (m_pool.nextReady(slotIndex)) {
                m_workerState[i].store(WorkerState::Processing, std::memory_order_relaxed);
                m_currentSlot[i].store(slotIndex, std::memory_order_relaxed);

                FrameSlot& slot = m_pool.slot(slotIndex);

                // Renders into a buffer local to this call, not the slot -- see
                // FrameStorage.hpp's own note. The slot itself has nothing left
                // worth keeping the instant FrameProcessor::run returns, so it's
                // freed right here, BEFORE onRendered() (which may block on a
                // full SaveQueue) rather than after -- one worker waiting to
                // hand off its frame should never hold up the decoder or every
                // other worker waiting on a free slot.
                Image rendered;
                FrameProcessor::run(slot.storage, m_opts, m_ctx, rendered);
                const int frameIndex = slot.frameIndex;

                // Built here, from the slot's own CellBuffer, rather than
                // reusing FrameStorage::text -- that field is always rendered
                // with plain opts.output.color/no screenControls (stdout and
                // still-image output want it that way), which isn't
                // necessarily what a text/ANSI video's saved frame needs.
                // Has to happen before markFree() below, same reason the
                // pixel render is extracted into `rendered` above: nothing in
                // the slot is safe to read the instant this worker gives it
                // back to the pool.
                std::string text;
                if (m_textOptions) text = AnsiRenderer::render(slot.storage.buffer, *m_ctx.charset, *m_textOptions);

                m_pool.markDone(slotIndex);
                m_pool.markFree(slotIndex);

                // Before onRendered(), not after: the instant markFree() returns,
                // the decoder may already be reusing this slot for a different
                // frame -- currentSlot() has to stop pointing at it right away, or
                // the display could read a totally different frame's data while
                // still labelling it as this worker's, for however long
                // onRendered() (a full SaveQueue can genuinely block here) takes.
                m_currentSlot[i].store(-1, std::memory_order_relaxed);

                m_workerState[i].store(WorkerState::HandingOff, std::memory_order_relaxed);
                if (m_onRendered) m_onRendered(frameIndex, std::move(rendered), std::move(text));
                m_workerState[i].store(WorkerState::WaitingForWork, std::memory_order_relaxed);
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
