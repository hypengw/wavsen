module wavsen.decode;

import rstd.cppstd;
import rstd;
import avutil;
import avcodec;
import avformat;
import swscale;

namespace wavsen::decode
{

using namespace rstd::prelude;

namespace
{

struct FmtCtxDeleter {
    void operator()(AVFormatContext* p) const noexcept {
        if (p) avformat_close_input(&p);
    }
};
struct CodecCtxDeleter {
    void operator()(AVCodecContext* p) const noexcept {
        if (p) avcodec_free_context(&p);
    }
};
struct FrameDeleter {
    void operator()(AVFrame* p) const noexcept {
        if (p) av_frame_free(&p);
    }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const noexcept {
        if (p) av_packet_free(&p);
    }
};
struct SwsDeleter {
    void operator()(SwsContext* p) const noexcept {
        if (p) sws_freeContext(p);
    }
};

using FmtCtxPtr   = std::unique_ptr<AVFormatContext, FmtCtxDeleter>;
using CodecCtxPtr = std::unique_ptr<AVCodecContext, CodecCtxDeleter>;
using FramePtr    = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr   = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsPtr      = std::unique_ptr<SwsContext, SwsDeleter>;

std::string av_err_str(int rc) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof(buf));
    return std::string(buf);
}

Error mk(ErrorKind k, std::string m) { return Error { k, std::move(m) }; }

void compute_target(int src_w, int src_h, u32 max_edge, u32& tw, u32& th) {
    if (src_w <= 0 || src_h <= 0) {
        tw = th = u32();
        return;
    }
    const auto sw = u32(static_cast<std::uint32_t>(src_w));
    const auto sh = u32(static_cast<std::uint32_t>(src_h));
    if (sw <= max_edge && sh <= max_edge) {
        tw = sw;
        th = sh;
        return;
    }
    if (sw >= sh) {
        tw = max_edge;
        th = u32(std::max<std::uint32_t>(
            1u,
            static_cast<std::uint32_t>(static_cast<double>(sh.to_primitive()) *
                                       static_cast<double>(max_edge.to_primitive()) /
                                       static_cast<double>(sw.to_primitive()))));
    } else {
        th = max_edge;
        tw = u32(std::max<std::uint32_t>(
            1u,
            static_cast<std::uint32_t>(static_cast<double>(sw.to_primitive()) *
                                       static_cast<double>(max_edge.to_primitive()) /
                                       static_cast<double>(sh.to_primitive()))));
    }
}

} // namespace

auto extract_thumbnail(std::string_view path_sv, const ThumbOptions& opts)
    -> rstd::Result<RgbaImage, Error> {
    if (opts.max_edge == u32()) {
        return rstd::Err(mk(ErrorKind::InvalidArgs, "max_edge must be non-zero"));
    }

    const std::string path(path_sv);

    AVFormatContext* raw_fmt = nullptr;
    if (int rc = avformat_open_input(&raw_fmt, path.c_str(), nullptr, nullptr); rc < 0) {
        return rstd::Err(mk(ErrorKind::OpenFailed, "avformat_open_input: " + av_err_str(rc)));
    }
    FmtCtxPtr fmt(raw_fmt);

    if (int rc = avformat_find_stream_info(fmt.get(), nullptr); rc < 0) {
        return rstd::Err(mk(ErrorKind::OpenFailed, "avformat_find_stream_info: " + av_err_str(rc)));
    }

    int video_idx = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_idx < 0) {
        return rstd::Err(mk(ErrorKind::NoVideoStream, "no video/image stream in file"));
    }

    AVStream*          st  = fmt->streams[video_idx];
    AVCodecParameters* par = st->codecpar;

    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (! dec) {
        return rstd::Err(
            mk(ErrorKind::DecoderInit,
               std::string("no decoder for codec ") + avcodec_get_name(par->codec_id)));
    }

    CodecCtxPtr cctx(avcodec_alloc_context3(dec));
    if (! cctx) {
        return rstd::Err(mk(ErrorKind::DecoderInit, "avcodec_alloc_context3 failed"));
    }
    if (int rc = avcodec_parameters_to_context(cctx.get(), par); rc < 0) {
        return rstd::Err(
            mk(ErrorKind::DecoderInit, "avcodec_parameters_to_context: " + av_err_str(rc)));
    }
    if (int rc = avcodec_open2(cctx.get(), dec, nullptr); rc < 0) {
        return rstd::Err(mk(ErrorKind::DecoderInit, "avcodec_open2: " + av_err_str(rc)));
    }

    // Seek to a sensible thumbnail target if the source is long enough. Failure
    // here is non-fatal — falling through decodes from the start.
    f64 duration_sec;
    if (fmt->duration > 0) {
        duration_sec = f64(static_cast<double>(fmt->duration) / AV_TIME_BASE);
    } else if (st->duration > 0 && st->time_base.den > 0) {
        duration_sec = f64(static_cast<double>(st->duration) * ffi::av_q2d(st->time_base));
    }
    if (duration_sec > f64(0.5)) {
        f64 target_sec = opts.seek_seconds.max(duration_sec * opts.seek_fraction);
        target_sec     = target_sec.min((duration_sec - f64(0.05)).max(f64()));
        if (target_sec > f64()) {
            const std::int64_t ts =
                static_cast<std::int64_t>(target_sec.to_primitive() * AV_TIME_BASE);
            const int seek_flags = opts.prefer_keyframe ? AVSEEK_FLAG_BACKWARD : AVSEEK_FLAG_ANY;
            if (av_seek_frame(fmt.get(), -1, ts, seek_flags) >= 0) {
                avcodec_flush_buffers(cctx.get());
            }
        }
    }

    PacketPtr pkt(av_packet_alloc());
    FramePtr  src_frame(av_frame_alloc());
    if (! pkt || ! src_frame) {
        return rstd::Err(mk(ErrorKind::DecoderInit, "av_packet_alloc / av_frame_alloc failed"));
    }

    bool got_frame = false;
    while (! got_frame) {
        int rc = av_read_frame(fmt.get(), pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(cctx.get(), nullptr);
        } else if (rc < 0) {
            return rstd::Err(mk(ErrorKind::DecodeFailed, "av_read_frame: " + av_err_str(rc)));
        } else if (pkt->stream_index != video_idx) {
            av_packet_unref(pkt.get());
            continue;
        } else {
            rc = avcodec_send_packet(cctx.get(), pkt.get());
            av_packet_unref(pkt.get());
            if (rc < 0 && rc != AVERROR(EAGAIN)) {
                return rstd::Err(
                    mk(ErrorKind::DecodeFailed, "avcodec_send_packet: " + av_err_str(rc)));
            }
        }
        while (true) {
            rc = avcodec_receive_frame(cctx.get(), src_frame.get());
            if (rc == AVERROR(EAGAIN)) break;
            if (rc == AVERROR_EOF) {
                return rstd::Err(
                    mk(ErrorKind::DecodeFailed, "decoder flushed without producing a frame"));
            }
            if (rc < 0) {
                return rstd::Err(
                    mk(ErrorKind::DecodeFailed, "avcodec_receive_frame: " + av_err_str(rc)));
            }
            got_frame = true;
            break;
        }
    }

    const auto src_fmt = static_cast<AVPixelFormat>(src_frame->format);
    const int  src_w   = src_frame->width;
    const int  src_h   = src_frame->height;
    if (src_w <= 0 || src_h <= 0 || src_fmt == AV_PIX_FMT_NONE) {
        return rstd::Err(
            mk(ErrorKind::DecodeFailed, "decoded frame has invalid dimensions/format"));
    }

    u32 tw;
    u32 th;
    compute_target(src_w, src_h, opts.max_edge, tw, th);
    if (tw == u32() || th == u32()) {
        return rstd::Err(mk(ErrorKind::ScaleFailed, "computed target size is zero"));
    }

    SwsPtr sws(sws_getContext(src_w,
                              src_h,
                              src_fmt,
                              static_cast<int>(tw.to_primitive()),
                              static_cast<int>(th.to_primitive()),
                              AV_PIX_FMT_RGBA,
                              SWS_BICUBIC,
                              nullptr,
                              nullptr,
                              nullptr));
    if (! sws) {
        return rstd::Err(
            mk(ErrorKind::ScaleFailed,
               std::string("sws_getContext failed (src=") + av_get_pix_fmt_name(src_fmt) + ")"));
    }

    RgbaImage out;
    out.width  = tw;
    out.height = th;
    out.stride = tw * u32(4);
    out.data.assign(static_cast<std::size_t>(out.stride.to_primitive()) *
                        static_cast<std::size_t>(th.to_primitive()),
                    0u);

    std::uint8_t* dst_planes[4]  = { out.data.data(), nullptr, nullptr, nullptr };
    int           dst_strides[4] = { static_cast<int>(out.stride.to_primitive()), 0, 0, 0 };

    int scaled = sws_scale(
        sws.get(), src_frame->data, src_frame->linesize, 0, src_h, dst_planes, dst_strides);
    if (scaled <= 0) {
        return rstd::Err(mk(ErrorKind::ScaleFailed, "sws_scale produced no rows"));
    }

    return rstd::Ok(std::move(out));
}

} // namespace wavsen::decode
