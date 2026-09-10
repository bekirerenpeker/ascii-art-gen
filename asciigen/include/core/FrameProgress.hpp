#pragma once

#include <atomic>

// Lock-free progress state for one frame's worth of work: what stage it's in, and
// how far through the whole frame that stage represents. Written by whatever's
// actually processing the frame (FrameProcessor::run today; a video worker thread
// later), read by a separate reporting thread that draws it -- see app/
// ProgressDisplay.hpp. Neither side ever touches anything but these two atomics,
// so there is nothing else to synchronize and nothing that can deadlock.
//
// `stage` points at a static string literal, never a heap string -- the handful of
// stage names (see FrameProcessor.cpp/Pipeline.cpp) all outlive the program, so an
// atomic pointer is enough on its own; there is no string lifetime or allocation to
// manage, and no torn read is possible the way there would be with a shared
// std::string.
struct FrameProgress
{
    std::atomic<const char*> stage {"idle"};
    std::atomic<float> fraction {0.f};

    void set(const char* newStage, float newFraction)
    {
        stage.store(newStage, std::memory_order_relaxed);
        fraction.store(newFraction, std::memory_order_relaxed);
    }
};
