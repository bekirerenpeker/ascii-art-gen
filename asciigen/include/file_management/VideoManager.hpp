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

// Encodes and muxes frames into one video file. Two codecs, chosen by the
// output extension rather than a separate flag -- .mkv gets AV_CODEC_ID_FFV1
// (genuinely lossless, encoded as planar RGB so not even chroma-subsampled --
// every pixel this project rendered comes back out bit-for-bit unchanged),
// anything else gets AV_CODEC_ID_MPEG4 same as before. Both are FFmpeg's own
// native encoders, present with zero extra runtime dependencies in the LGPL
// build this project fetches (see lib/ffmpeg/CMakeLists.txt); H.264 needs
// libx264 (GPL, not in that build) and the LGPL-safe alternatives here
// (libopenh264, libvpx) need their own external shared libraries this
// project doesn't vendor. Worth revisiting once that's sorted out -- not a
// permanent ceiling on the lossy side.
class VideoWriter
{
  public:
    // `width`/`height` must be even -- YUV420P halves both chroma planes.
    VideoWriter(const std::filesystem::path& filepath, int width, int height, double fps);
    ~VideoWriter();

    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;

    bool isOpen() const;

    // Encodes and muxes one frame. MUST be called in presentation order (frame 0
    // first, frame 1 second, ...) -- the encoder and muxer both assume it, same
    // as any video file. `frame` must be RGB24, exactly (width, height) as given
    // to the constructor.
    bool writeFrame(const Image& frame);

    // Flushes the encoder (frames it's still holding for lookahead/reordering)
    // and writes the trailer. Called by the destructor too if it wasn't already
    // -- but the return value only reaches a caller that calls this directly.
    bool finish();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}   // namespace VideoManager
