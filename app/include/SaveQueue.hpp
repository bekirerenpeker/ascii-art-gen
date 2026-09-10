#pragma once

#include <condition_variable>
#include <map>
#include <mutex>

// Bounded, order-preserving handoff between however many workers render frames
// -- out of order, whichever finishes first -- and the one thread that has to
// write them out in sequence (a video's muxer genuinely requires this; there's
// no such thing as writing frame 7 before frame 6). `capacity` limits how many
// rendered frames can be sitting here waiting at once: rendering is
// parallelised across N workers and writing is not, so without a limit the
// backlog would just grow for the entire length of the video.
//
// The one exception to the limit: the frame the writer is waiting for NEXT is
// always accepted, however full the queue already is. Capacity exists to
// bound frames that are waiting on something ELSE to arrive first, not to
// block the one arrival that would let the backlog drain -- refusing it too
// would risk every slot full of out-of-order frames with nowhere to go, and
// the one frame that would unblock all of them turned away at the door
// because the door happened to be full when it knocked.
//
// Templated on what's being handed off -- an Image for a pixel video, a
// std::string for a text/ANSI one (see Pipeline.cpp's runVideo) -- since the
// ordering/backpressure logic is identical either way and only the payload
// type differs. Explicitly instantiated for both in SaveQueue.cpp rather than
// header-only, matching this project's usual declaration/definition split;
// add another `template class SaveQueue<T>;` line there if a third payload
// type is ever needed.
template <typename T>
class SaveQueue
{
  public:
    explicit SaveQueue(int capacity);

    // Called by a worker once it has a frame rendered. Blocks if the queue is
    // already at capacity, unless `frameIndex` is exactly the one popNextInOrder
    // is waiting for next.
    void push(int frameIndex, T&& value);

    // Blocks until the next frame in sequence is available, or close() has
    // been called and it will never arrive (in which case this returns
    // false). Advances the sequence by one on every successful call.
    bool popNextInOrder(T& outValue);

    // No more push() calls coming -- lets a waiting popNextInOrder() give up
    // instead of blocking forever once nothing more will ever arrive. Only
    // safe to call once every push() that could ever happen already has.
    void close();

  private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<int, T> m_pending;
    int m_nextToWrite = 0;
    int m_capacity;
    bool m_closed = false;
};
