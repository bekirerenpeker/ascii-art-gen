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

// FFmpeg logs warnings/errors straight to stderr on its own, asynchronously
// from whichever thread hit them -- which, with the video pipeline's own
// cursor-tracked progress display also writing to the terminal, is exactly the
// kind of interleaving that corrupts it: a single unexpected log line between
// two redraws throws off how many lines the display thinks it needs to move
// back over. Quieted once, here, so every failure path in this file surfaces
// through this project's own std::cerr messages and return codes instead --
// see VideoReader/VideoWriter's own error handling for those.
struct QuietFFmpegLogging
{
    QuietFFmpegLogging() { av_log_set_level(AV_LOG_QUIET); }
} const quietFFmpegLogging;

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

    // Tried and reverted: setting thread_count = 0 here to let libavcodec
    // decode each call across multiple internal threads. Measured slower, not
    // faster, on this machine -- the worker pool below already sizes itself to
    // hardware_concurrency(), so every core is already spoken for by the time
    // a frame reaches this decoder; giving it its own thread pool on top just
    // buys CPU oversubscription. Left at the default (1) deliberately.
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
    if (!openVideoStream(filepath, h, true)) return false;

    // A single still image still reports a "video" stream once opened this way
    // -- FFmpeg's own pipe demuxers (png_pipe, image2 for a lone jpg, ...) model
    // an image as a one-frame video, complete with a fake frame rate. Measured
    // directly rather than assumed: a bare PNG reports nb_frames unknown and
    // duration unknown; a bare JPEG reports nb_frames unknown and a duration of
    // EXACTLY one frame at the fake rate (0.04s at the default 25fps = 1/25); a
    // real video reports either a genuine nb_frames above 1, or a duration
    // spanning meaningfully more than one frame. So: a stream existing is not
    // enough on its own, only a stream that's clearly carrying more than one
    // frame's worth of content counts as video here.
    const AVStream* stream = h.fmt->streams[h.streamIndex];
    if (stream->nb_frames > 1) return true;

    double durationSeconds = 0.0;
    if (stream->duration != AV_NOPTS_VALUE)
        durationSeconds = stream->duration * av_q2d(stream->time_base);
    else if (h.fmt->duration != AV_NOPTS_VALUE)
        durationSeconds = (double)h.fmt->duration / AV_TIME_BASE;

    if (durationSeconds <= 0.0) return false;   // unknown duration -- the still-image case

    const AVRational fpsRational = av_guess_frame_rate(h.fmt, const_cast<AVStream*>(stream), nullptr);
    const double fps = (fpsRational.num > 0 && fpsRational.den > 0) ? av_q2d(fpsRational) : 0.0;
    if (fps <= 0.0) return true;   // a real duration but no frame rate to estimate a count from -- assume video

    return durationSeconds * fps > 1.5;   // meaningfully more than one frame's worth
}

namespace {

// Mirrors RawHandles above, for the encode side. Zero-initialised for the same
// reason: a failure partway through open() still tears down cleanly.
struct WriterHandles
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    AVStream* stream = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    bool fileOpened = false;   // whether avio_open succeeded -- only then does avio_closep apply
    bool headerWritten = false;
    int64_t nextPts = 0;

    ~WriterHandles()
    {
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (codec) avcodec_free_context(&codec);
        if (fmt) {
            if (fileOpened && fmt->pb) avio_closep(&fmt->pb);
            avformat_free_context(fmt);
        }
    }
};

}   // namespace

struct VideoWriter::Impl
{
    WriterHandles h;
    int width = 0, height = 0;
    bool open = false;
    bool finished = false;
};

VideoWriter::VideoWriter(const std::filesystem::path& filepath, int width, int height, double fps)
    : m_impl(std::make_unique<Impl>())
{
    ASCIIGEN_PROFILE("VideoWriter::open", "io");

    WriterHandles& h = m_impl->h;
    m_impl->width = width;
    m_impl->height = height;

    if (width <= 0 || height <= 0 || fps <= 0.0) return;

    avformat_alloc_output_context2(&h.fmt, nullptr, nullptr, filepath.string().c_str());
    if (!h.fmt) {
        std::cout << "couldnt determine a container format for \"" << filepath << "\"\n";
        return;
    }

    // See VideoManager.hpp's note on VideoWriter: fixed to MPEG-4 part 2, the one
    // video encoder guaranteed present with no extra runtime dependency in this
    // project's LGPL FFmpeg build.
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!encoder) return;

    h.stream = avformat_new_stream(h.fmt, nullptr);
    if (!h.stream) return;

    h.codec = avcodec_alloc_context3(encoder);
    if (!h.codec) return;

    // A plain N/1 time base, one tick per frame -- pts is just a frame counter
    // below rather than needing to track wall-clock time anywhere.
    const AVRational timeBase = av_d2q(1.0 / fps, 1000000);
    h.codec->width = width;
    h.codec->height = height;
    h.codec->pix_fmt = AV_PIX_FMT_YUV420P;
    h.codec->time_base = timeBase;
    h.codec->framerate = av_inv_q(timeBase);
    // Was 12 (a typical natural-video GOP). Dropped after a real quality bug:
    // P-frames drifted visibly worse the further they sat from the last
    // I-frame, and for THIS content that's structural, not a tuning problem --
    // each glyph cell one macroblock roughly aligns with is independently
    // near-random frame to frame (dithering isn't temporally coherent, and
    // glyph selection can flip between two similarly-scored but visually
    // different shapes for the same cell), which is close to a worst case for
    // motion-compensated prediction. Confirmed directly: re-encoding the same
    // clip with gop_size 12 produced visible horizontal banding by the last
    // P-frame of a GOP that a same-position I-frame didn't have; gop_size 1
    // (all-intra) removed it completely but roughly tripled file size; 2 --
    // at most one P-frame of drift between resets -- looked identical to
    // all-intra on the same frames while costing about half as much size
    // increase (~2.2x here vs ~3.4x). Not re-tuned per clip; a future
    // improvement, not this fix's job.
    h.codec->gop_size = 2;

    // Tried and reverted: an rc_max_rate/rc_buffer_size ceiling here, meant to
    // fix a specific file (2560x4544 @ 60fps) a strict player rejected as
    // "unsupported format" -- its real bitrate was 118 Mbps against a
    // MPEG-4 Simple Profile tag that doesn't legally go anywhere near that.
    // Measured effect: it knocked a normal file's overshoot down from 1.65x to
    // about 1.3x (24.3 -> 18.6 Mbps for a 1080p source), visibly softer for no
    // reason since that file already played fine -- while the actual target,
    // whose content needs more bits per macroblock than any legal quantizer
    // can give up, barely moved (118 -> 112 Mbps) and still doesn't play.
    // Confirmed directly by re-encoding one of this project's own rendered
    // outputs through plain `ffmpeg -c:v mpeg4 -maxrate -bufsize`: it logged
    // "rc buffer underflow ... max bitrate possibly too small" on nearly every
    // frame and still came out oversized (our av_log_set_level above is what
    // keeps that same warning from ever surfacing through this writer). A real
    // capacity limit of MPEG-4 Part 2 at that resolution/frame rate, not
    // something a bit_rate number can paper over -- fixing that file for real
    // would mean encoding fewer pixels or fewer frames per second, not a
    // tighter ceiling that only costs quality on every other file instead.
    h.codec->bit_rate = (int64_t)width * height * 4;

    if (h.fmt->oformat->flags & AVFMT_GLOBALHEADER) h.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(h.codec, encoder, nullptr) < 0) return;
    if (avcodec_parameters_from_context(h.stream->codecpar, h.codec) < 0) return;
    h.stream->time_base = h.codec->time_base;

    if (!(h.fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&h.fmt->pb, filepath.string().c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cout << "couldnt open \"" << filepath << "\" for writing\n";
            return;
        }
        h.fileOpened = true;
    }

    if (avformat_write_header(h.fmt, nullptr) < 0) return;
    h.headerWritten = true;

    h.frame = av_frame_alloc();
    h.packet = av_packet_alloc();
    if (!h.frame || !h.packet) return;

    h.frame->format = AV_PIX_FMT_YUV420P;
    h.frame->width = width;
    h.frame->height = height;
    if (av_frame_get_buffer(h.frame, 0) < 0) return;

    m_impl->open = true;
}

VideoWriter::~VideoWriter()
{
    if (m_impl && m_impl->open && !m_impl->finished) finish();
}

bool VideoWriter::isOpen() const { return m_impl->open; }

bool VideoWriter::writeFrame(const Image& frameImg)
{
    if (!m_impl->open || m_impl->finished) return false;
    if (!frameImg.pixels || frameImg.width != m_impl->width || frameImg.height != m_impl->height
        || frameImg.depth != 3)
        return false;

    ASCIIGEN_PROFILE("VideoWriter::writeFrame", "io");
    WriterHandles& h = m_impl->h;
    const int w = m_impl->width, hgt = m_impl->height;

    if (!h.sws) {
        h.sws = sws_getContext(
            w, hgt, AV_PIX_FMT_RGB24, w, hgt, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr,
            nullptr
        );
        if (!h.sws) return false;
    }

    if (av_frame_make_writable(h.frame) < 0) return false;

    // Same bottom-to-top row order every Image already uses (see VideoReader's
    // convertFrame) -- read backward from the last row instead of flipping first.
    const byte* src[4] = {frameImg.pixels + (size_t)(hgt - 1) * w * 3, nullptr, nullptr, nullptr};
    const int srcStride[4] = {-(w * 3), 0, 0, 0};
    sws_scale(h.sws, src, srcStride, 0, hgt, h.frame->data, h.frame->linesize);

    h.frame->pts = h.nextPts++;

    if (avcodec_send_frame(h.codec, h.frame) < 0) return false;

    for (;;) {
        const int ret = avcodec_receive_packet(h.codec, h.packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;

        av_packet_rescale_ts(h.packet, h.codec->time_base, h.stream->time_base);
        h.packet->stream_index = h.stream->index;
        av_interleaved_write_frame(h.fmt, h.packet);
        av_packet_unref(h.packet);
    }

    return true;
}

bool VideoWriter::finish()
{
    if (!m_impl->open || m_impl->finished) return false;
    m_impl->finished = true;

    ASCIIGEN_PROFILE("VideoWriter::finish", "io");
    WriterHandles& h = m_impl->h;

    // Flush: the encoder may still be holding frames back for lookahead/
    // reordering: a null frame tells it no more are coming, and receive_packet
    // keeps handing back whatever it was still sitting on until it's drained.
    avcodec_send_frame(h.codec, nullptr);
    for (;;) {
        const int ret = avcodec_receive_packet(h.codec, h.packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        av_packet_rescale_ts(h.packet, h.codec->time_base, h.stream->time_base);
        h.packet->stream_index = h.stream->index;
        av_interleaved_write_frame(h.fmt, h.packet);
        av_packet_unref(h.packet);
    }

    if (h.headerWritten) av_write_trailer(h.fmt);
    return true;
}

}   // namespace VideoManager
