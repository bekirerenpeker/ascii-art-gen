#pragma once

#include "core/Image.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>

namespace VideoManager {

struct VideoInfo
{
    int width = 0;
    int height = 0;
    double fps = 0.0;                // 0 if the container doesn't say
    int64_t frameCount = 0;          // best estimate from the container; 0 if unknown
    double durationSeconds = 0.0;    // 0 if unknown
};

// Opens one video file and decodes it frame by frame. Not copyable: it owns a demuxer,
// a decoder and a scaler, none of which make sense to duplicate. The first stream
// av_find_best_stream picks as "best video" is decoded; every other stream (audio,
// subtitles, additional video tracks) is read past and discarded -- no audio support
// is in scope (see video-roadmap.md).
//
// nextFrame reuses `out`'s existing pixel buffer across calls instead of allocating a
// fresh Image every frame -- only reallocates on the first call or if the stream's
// resolution actually changes mid-file. The frame loop this feeds is explicit about
// not wanting per-frame allocation; this is where that starts.
class VideoReader
{
  public:
    explicit VideoReader(const std::filesystem::path& filepath);
    ~VideoReader();

    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;
    VideoReader(VideoReader&&) noexcept;
    VideoReader& operator=(VideoReader&&) noexcept;

    bool isOpen() const;
    const VideoInfo& info() const;

    // Decodes the next frame into `out` as RGB24, same bottom-to-top row order
    // ImageManager::loadImage produces, so downstream code can't tell the source apart.
    // Returns false at end of stream or on a decode error -- the two aren't
    // distinguished, the same way loadImage returns an empty Image for either.
    bool nextFrame(Image& out);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Cheap sniff: true if av_find_best_stream finds a video stream at all. Meant for
// input-format detection (video vs image vs the video-only-carries-audio edge case),
// not as a full open -- opens and immediately closes the demuxer.
bool looksLikeVideo(const std::filesystem::path& filepath);

}   // namespace VideoManager
