#include "file_management/VideoManager.hpp"
#include "core/Profiler.hpp"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace VideoManager {

namespace {

// Every FFmpeg handle this reader owns, zero-initialised so the destructor can tear
// down a partially-opened file (a failure halfway through the open sequence) the same
// way it tears down a fully-opened one -- each av_*_free is already a no-op on null.
struct RawHandles
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int streamIndex = -1;

    // Cached against the frame just converted, so sws is only rebuilt when the
    // stream's own format actually changes instead of once per frame.
    int swsWidth = 0, swsHeight = 0;
    int swsFormat = -1;

    bool flushedDecoder = false;

    ~RawHandles()
    {
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (codec) avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
    }
};

}   // namespace

struct VideoReader::Impl
{
    RawHandles h;
    VideoInfo info;
    bool open = false;
};

namespace {

// Shared by the constructor and looksLikeVideo: opens the container, finds the best
// video stream, and (unless probeOnly) stands up a decoder for it. Leaves partial
// state in `h` on failure -- RawHandles' destructor cleans up whatever got that far.
bool openVideoStream(const std::filesystem::path& filepath, RawHandles& h, bool probeOnly)
{
    if (avformat_open_input(&h.fmt, filepath.string().c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(h.fmt, nullptr) < 0) return false;

    const AVCodec* decoder = nullptr;
    h.streamIndex = av_find_best_stream(h.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (h.streamIndex < 0 || !decoder) return false;

    if (probeOnly) return true;

    h.codec = avcodec_alloc_context3(decoder);
    if (!h.codec) return false;

    if (avcodec_parameters_to_context(h.codec, h.fmt->streams[h.streamIndex]->codecpar) < 0)
        return false;

    if (avcodec_open2(h.codec, decoder, nullptr) < 0) return false;

    h.frame = av_frame_alloc();
    h.packet = av_packet_alloc();
    return h.frame && h.packet;
}

VideoInfo computeInfo(const RawHandles& h)
{
    VideoInfo info;
    info.width = h.codec->width;
    info.height = h.codec->height;

    const AVStream* stream = h.fmt->streams[h.streamIndex];
    const AVRational fpsRational = av_guess_frame_rate(h.fmt, const_cast<AVStream*>(stream), nullptr);
    if (fpsRational.num > 0 && fpsRational.den > 0) info.fps = av_q2d(fpsRational);

    if (stream->duration != AV_NOPTS_VALUE) {
        info.durationSeconds = stream->duration * av_q2d(stream->time_base);
    } else if (h.fmt->duration != AV_NOPTS_VALUE) {
        info.durationSeconds = (double)h.fmt->duration / AV_TIME_BASE;
    }

    if (stream->nb_frames > 0) info.frameCount = stream->nb_frames;
    else if (info.fps > 0 && info.durationSeconds > 0)
        info.frameCount = (int64_t)(info.fps * info.durationSeconds + 0.5);

    return info;
}

// Writes frame->data through sws_scale directly into `out`'s buffer, flipped to match
// ImageManager::loadImage's bottom-to-top row order (stbi_set_flip_vertically_on_load)
// -- a negative destination stride walking backward from the last row does the flip as
// part of the same pass instead of a separate copy.
void convertFrame(RawHandles& h, const AVFrame* frame, Image& out)
{
    const int w = frame->width, h_ = frame->height;

    if (!h.sws || w != h.swsWidth || h_ != h.swsHeight || frame->format != h.swsFormat) {
        if (h.sws) sws_freeContext(h.sws);
        h.sws = sws_getContext(
            w, h_, (AVPixelFormat)frame->format, w, h_, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr,
            nullptr, nullptr
        );
        h.swsWidth = w;
        h.swsHeight = h_;
        h.swsFormat = frame->format;
    }

    if (out.width != w || out.height != h_ || out.depth != 3 || !out.pixels) out = Image(w, h_, 3);

    byte* dst[4] = {out.pixels + (size_t)(h_ - 1) * w * 3, nullptr, nullptr, nullptr};
    const int dstStride[4] = {-(w * 3), 0, 0, 0};
    sws_scale(h.sws, frame->data, frame->linesize, 0, h_, dst, dstStride);
}

}   // namespace

VideoReader::VideoReader(const std::filesystem::path& filepath) : m_impl(std::make_unique<Impl>())
{
    ASCIIGEN_PROFILE("VideoReader::open", "io");

    if (!openVideoStream(filepath, m_impl->h, false)) {
        std::cout << "couldnt open video from " << filepath << "\n";
        return;
    }

    m_impl->info = computeInfo(m_impl->h);
    m_impl->open = true;
}

VideoReader::~VideoReader() = default;
VideoReader::VideoReader(VideoReader&&) noexcept = default;
VideoReader& VideoReader::operator=(VideoReader&&) noexcept = default;

bool VideoReader::isOpen() const { return m_impl->open; }
const VideoInfo& VideoReader::info() const { return m_impl->info; }

bool VideoReader::nextFrame(Image& out)
{
    if (!m_impl->open) return false;

    ASCIIGEN_PROFILE("VideoReader::nextFrame", "io");
    RawHandles& h = m_impl->h;

    for (;;) {
        const int recvRet = avcodec_receive_frame(h.codec, h.frame);
        if (recvRet == 0) {
            convertFrame(h, h.frame, out);
            av_frame_unref(h.frame);
            return true;
        }
        if (recvRet != AVERROR(EAGAIN)) return false;   // AVERROR_EOF or a real error

        if (h.flushedDecoder) return false;   // drained everything after EOF

        const int readRet = av_read_frame(h.fmt, h.packet);
        if (readRet < 0) {
            avcodec_send_packet(h.codec, nullptr);   // flush: no more packets coming
            h.flushedDecoder = true;
            continue;
        }

        if (h.packet->stream_index != h.streamIndex) {
            av_packet_unref(h.packet);
            continue;
        }

        avcodec_send_packet(h.codec, h.packet);
        av_packet_unref(h.packet);
    }
}

bool looksLikeVideo(const std::filesystem::path& filepath)
{
    RawHandles h;
    return openVideoStream(filepath, h, true);
}

}   // namespace VideoManager
