module wavsen.video;

import rstd;
import rstd.cppstd;
import rstd.log;
import vulkan;
import :vk_device;
import :video_decoder;
import avutil;
import avcodec;
import avformat;
import swscale;

namespace wavsen::video
{

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

template<typename T, void (*Release)(T*&)>
class AvOwner {
public:
    AvOwner() = default;
    explicit AvOwner(T* value): value_(value) {}
    AvOwner(const AvOwner&)            = delete;
    AvOwner& operator=(const AvOwner&) = delete;

    AvOwner(AvOwner&& other) noexcept: value_(rstd::exchange(other.value_, nullptr)) {}
    AvOwner& operator=(AvOwner&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = rstd::exchange(other.value_, nullptr);
        }
        return *this;
    }

    ~AvOwner() { reset(); }

    void reset(T* value = nullptr) {
        if (value_) Release(value_);
        value_ = value;
    }

    T*       get() const { return value_; }
    T*       operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    T* value_ { nullptr };
};

void release_format(AVFormatContext*& value) {
    if (value) avformat_close_input(&value);
}
void release_codec(AVCodecContext*& value) {
    if (value) avcodec_free_context(&value);
}
void release_frame(AVFrame*& value) {
    if (value) av_frame_free(&value);
}
void release_packet(AVPacket*& value) {
    if (value) av_packet_free(&value);
}
void release_sws(SwsContext*& value) {
    if (value) sws_freeContext(value);
    value = nullptr;
}
void release_buffer(AVBufferRef*& value) {
    if (value) av_buffer_unref(&value);
}

using FmtCtxPtr   = AvOwner<AVFormatContext, release_format>;
using CodecCtxPtr = AvOwner<AVCodecContext, release_codec>;
using FramePtr    = AvOwner<AVFrame, release_frame>;
using PacketPtr   = AvOwner<AVPacket, release_packet>;
using SwsPtr      = AvOwner<SwsContext, release_sws>;
using BufRefPtr   = AvOwner<AVBufferRef, release_buffer>;

/* Defined further down — forward-declared so the helpers above the
 * definitions can use them. */
bool fail(Error* err, ref<str> message);
bool fail(Error* err, rstd::string::String message);
auto av_err_str(int rc) -> rstd::string::String;

/* Translate FFmpeg's colorspace/range enums into our ColorSpace /
 * ColorRange ints (which the public Nv12Frame / VkFrameView carry).
 * Unknowns default to BT.709 limited — the most common case. */
u32 map_colorspace(int cs) {
    switch (cs) {
    case AVCOL_SPC_BT709: return u32();
    case AVCOL_SPC_BT470BG: // PAL / BT.601 625
    case AVCOL_SPC_SMPTE170M: return u32(1);
    case AVCOL_SPC_BT2020_NCL: return u32(2);
    case AVCOL_SPC_BT2020_CL: return u32(2);
    default: return u32();
    }
}
u32 map_range(int r) { return r == AVCOL_RANGE_JPEG ? u32(1) : u32(); }

/* `get_format` callback: prefer AV_PIX_FMT_VULKAN whenever the codec
 * offers it; fall back to whatever FFmpeg picks by default otherwise.
 *
 * This is the moment to bootstrap hw_frames_ctx — get_format fires
 * during avcodec_open2 with avctx->internal already allocated, so
 * avcodec_get_hw_frames_parameters (which derefs internal in
 * ff_decode_get_hw_frames_ctx) is safe to call. Calling it earlier
 * (between avcodec_alloc_context3 and avcodec_open2) segfaults.
 *
 * A rejected Vulkan frame configuration returns AV_PIX_FMT_NONE so
 * build_internal fails this decoder attempt and the outer trial loop
 * can rebuild it for the next backend. */
AVPixelFormat get_format_prefer_vulkan(AVCodecContext* cctx, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p != AV_PIX_FMT_VULKAN) continue;

        AVBufferRef* hw_frames = nullptr;
        int          rc        = avcodec_get_hw_frames_parameters(
            cctx, cctx->hw_device_ctx, AV_PIX_FMT_VULKAN, &hw_frames);
        if (rc < 0 || ! hw_frames) {
            rstd::log::warn("get_format_prefer_vulkan: avcodec_get_hw_frames_parameters "
                            "failed ({}); rejecting this Vulkan decode attempt.",
                            av_err_str(rc).as_str());
            if (hw_frames) av_buffer_unref(&hw_frames);
            return AV_PIX_FMT_NONE;
        }
        auto* fc = reinterpret_cast<AVHWFramesContext*>(hw_frames->data);
        if (int irc = avcodec_prepare_vulkan_decode_frames(hw_frames); irc < 0) {
            rstd::log::warn("get_format_prefer_vulkan: Vulkan decode frame preparation "
                            "failed ({}); rejecting this Vulkan decode attempt.",
                            av_err_str(irc).as_str());
            av_buffer_unref(&hw_frames);
            return AV_PIX_FMT_NONE;
        }
        if (cctx->hw_frames_ctx) av_buffer_unref(&cctx->hw_frames_ctx);
        cctx->hw_frames_ctx = hw_frames;
        rstd::log::info("get_format_prefer_vulkan: AV_PIX_FMT_VULKAN selected (sw_format={}).",
                        av_get_pix_fmt_name(fc->sw_format));
        return AV_PIX_FMT_VULKAN;
    }
    return avcodec_default_get_format(cctx, fmts);
}

#if defined(WAVSEN_HAS_VAAPI)
/* VAAPI counterpart: prefer AV_PIX_FMT_VAAPI when offered. The codec
 * bootstraps an internal AVHWFramesContext from the AVHWDeviceContext
 * we attached to cctx->hw_device_ctx, so we don't pre-allocate frames
 * either. */
AVPixelFormat get_format_prefer_vaapi(AVCodecContext* cctx, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_VAAPI) return AV_PIX_FMT_VAAPI;
    }
    return avcodec_default_get_format(cctx, fmts);
}

/* Best-effort AV_HWDEVICE_TYPE_VAAPI context. FFmpeg owns the libva
 * VADisplay; we hand it the selected Vulkan device's render-node path.
 * Returns NULL on any failure with *err populated. */
AVBufferRef* make_vaapi_hwdevice(const rstd::string::String& render_node, Error* err) {
    if (render_node.is_empty()) {
        fail(err, "VAAPI requires the selected Vulkan device's render node"_str);
        return nullptr;
    }
    AVBufferRef* hwd           = nullptr;
    auto         render_node_c = rstd::ffi::CString::make(render_node.clone()).unwrap();
    const char*  dev           = render_node_c.as_ptr();
    int          rc = av_hwdevice_ctx_create(&hwd, AV_HWDEVICE_TYPE_VAAPI, dev, nullptr, 0);
    if (rc < 0 || ! hwd) {
        fail(err,
             rstd::format("av_hwdevice_ctx_create(VAAPI, {}): {}",
                          render_node.as_str(),
                          av_err_str(rc).as_str()));
        if (hwd) av_buffer_unref(&hwd);
        return nullptr;
    }
    return hwd;
}
#endif

rstd::string::String resolve_render_node(const Producer& producer, const OpenOpts& opts) {
    if (! opts.render_node.is_empty()) return opts.render_node.clone();
    auto node = producer.drm_render_node();
    return node.is_some() ? rstd::move(node).unwrap() : rstd::string::String {};
}

/* Build an AV_HWDEVICE_TYPE_VULKAN context wrapping the caller's
 * Producer-owned VkInstance/VkDevice. Returns a populated AVBufferRef
 * on success, or null + populated *err on any failure. */
AVBufferRef* make_shared_vulkan_hwdevice(const Producer& vk, Error* err) {
    AVBufferRef* hwd = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    if (! hwd) {
        fail(err, "av_hwdevice_ctx_alloc(VULKAN) failed"_str);
        return nullptr;
    }
    auto* dctx = reinterpret_cast<AVHWDeviceContext*>(hwd->data);
    auto* vctx = reinterpret_cast<AVVulkanDeviceContext*>(dctx->hwctx);

    vctx->get_proc_addr = vkGetInstanceProcAddr;
    vctx->inst          = vk.instance();
    vctx->phys_dev      = vk.physical_device();
    vctx->act_dev       = vk.device();

    auto iexts                       = vk.enabled_instance_extensions();
    auto dexts                       = vk.enabled_device_extensions();
    vctx->enabled_inst_extensions    = iexts.is_empty() ? nullptr : iexts.as_raw_ptr();
    vctx->nb_enabled_inst_extensions = static_cast<int>(iexts.len().to_primitive());
    vctx->enabled_dev_extensions     = dexts.is_empty() ? nullptr : dexts.as_raw_ptr();
    vctx->nb_enabled_dev_extensions  = static_cast<int>(dexts.len().to_primitive());

    auto qfs    = vk.queue_families();
    vctx->nb_qf = 0;
    for (usize i {}; i < qfs.len(); ++i) {
        const auto& q = qfs[i];
        if (vctx->nb_qf >= static_cast<int>(sizeof(vctx->qf) / sizeof(vctx->qf[0]))) break;
        AVVulkanDeviceQueueFamily entry {};
        entry.idx   = static_cast<int>(q.index.to_primitive());
        entry.num   = 1;
        entry.flags = static_cast<VkQueueFlagBits>(q.flags);
        entry.video_caps =
            static_cast<VkVideoCodecOperationFlagBitsKHR>(q.video_caps.to_primitive());
        vctx->qf[vctx->nb_qf++] = entry;
    }

    if (int rc = av_hwdevice_ctx_init(hwd); rc < 0) {
        fail(err, rstd::format("av_hwdevice_ctx_init(shared VULKAN): {}", av_err_str(rc).as_str()));
        av_buffer_unref(&hwd);
        return nullptr;
    }
    return hwd;
}

bool fail(Error* err, ref<str> message) {
    if (err) err->message = rstd::string::String::make(message);
    return false;
}

bool fail(Error* err, rstd::string::String message) {
    if (err) err->message = rstd::move(message);
    return false;
}

auto av_err_str(int rc) -> rstd::string::String {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof(buf));
    return rstd::string::String::make(rstd::cppstd::as_str(buf).unwrap());
}

} // namespace

struct VideoDecoder::State {
    /* Custom-IO source (open_from_stream path). Declared first so it
     * outlives every libav object that holds avio_ctx — the destructor
     * resets `fmt` and frees `avio_ctx` explicitly before the implicit
     * member destructors run, then `input_stream` is destroyed last as
     * the implicit destructions unwind in reverse declaration order. */
    Option<rstd::boxed::Box<dyn<InputStream>>> input_stream;
    AVIOContext*                               avio_ctx { nullptr };

    FmtCtxPtr   fmt;
    CodecCtxPtr cctx;
    PacketPtr   pkt;
    FramePtr    src_frame;
    /* Sw landing frame for vulkan→sw downloads via
     * av_hwframe_transfer_data. Allocated lazily on first hw frame. */
    FramePtr sw_frame;
    /* DRM_PRIME mapping target frame; allocated lazily for the VAAPI
     * zero-copy path. Holds the dup'd dma-buf fds via av_hwframe_map. */
    FramePtr drm_frame;
    SwsPtr   sws;
    /* Hwdevice context owned by the codec when present. Best-effort: a
     * NULL `hwd` here just means we run sw decode. */
    BufRefPtr     hwd;
    AVPixelFormat sws_src_fmt { AV_PIX_FMT_NONE };
    int           sws_src_w { 0 };
    int           sws_src_h { 0 };
    int           video_idx { -1 };
    AVRational    stream_tb { 0, 1 };
    bool          flushing { false };

    ~State() {
        /* Tear down libavformat first so it stops invoking our avio
         * callbacks. Then free the avio buffer + context. input_stream
         * is released last (implicit destruction) — it's still alive
         * here in case avformat_close_input touches pb. */
        fmt.reset();
        if (avio_ctx) {
            rstd::uint8_t* buf = avio_ctx->buffer;
            avio_context_free(&avio_ctx);
            if (buf) av_free(buf);
        }
    }
};

VideoDecoder::StateOwner::~StateOwner() {
    auto state = rstd::boxed::Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
}

namespace
{

bool ensure_sws(VideoDecoder::State& st, int src_w, int src_h, AVPixelFormat src_fmt, u32 target_w,
                u32 target_h) {
    if (st.sws && st.sws_src_w == src_w && st.sws_src_h == src_h && st.sws_src_fmt == src_fmt) {
        return true;
    }
    /* Always emit NV12 — that's what YuvToRgba consumes. */
    st.sws.reset(sws_getContext(src_w,
                                src_h,
                                src_fmt,
                                static_cast<int>(target_w.to_primitive()),
                                static_cast<int>(target_h.to_primitive()),
                                AV_PIX_FMT_NV12,
                                SWS_BICUBIC,
                                nullptr,
                                nullptr,
                                nullptr));
    if (! st.sws) return false;
    st.sws_src_w   = src_w;
    st.sws_src_h   = src_h;
    st.sws_src_fmt = src_fmt;
    return true;
}

bool seek_to_start(VideoDecoder::State& st) {
    int rc = av_seek_frame(st.fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) return false;
    avcodec_flush_buffers(st.cctx.get());
    st.flushing = false;
    return true;
}

} // namespace

namespace
{
bool probe_native_impl(ref<str> path, u32* native_width, u32* native_height, Error* err) {
    *native_width             = u32();
    *native_height            = u32();
    AVFormatContext* raw_fmt  = nullptr;
    auto             path_c   = rstd::ffi::CString::make(rstd::string::String::make(path)).unwrap();
    auto             path_raw = path_c.as_ptr();
    if (int rc = avformat_open_input(&raw_fmt, path_raw, nullptr, nullptr); rc < 0) {
        fail(err, rstd::format("avformat_open_input: {}", av_err_str(rc).as_str()));
        return false;
    }
    FmtCtxPtr fmt(raw_fmt);
    if (int rc = avformat_find_stream_info(fmt.get(), nullptr); rc < 0) {
        fail(err, rstd::format("avformat_find_stream_info: {}", av_err_str(rc).as_str()));
        return false;
    }
    int idx = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        fail(err, "no video stream in file"_str);
        return false;
    }
    AVCodecParameters* par = fmt->streams[idx]->codecpar;
    if (par->width <= 0 || par->height <= 0) {
        fail(err, "video stream has invalid native dimensions"_str);
        return false;
    }
    *native_width  = u32(static_cast<rstd::uint32_t>(par->width));
    *native_height = u32(static_cast<rstd::uint32_t>(par->height));
    return true;
}
} // namespace

/* AVIOContext shims that bounce libavformat IO into an IInputStream. */
namespace
{
int avio_read_shim(void* opaque, rstd::uint8_t* buf, int buf_size) {
    auto* state = static_cast<VideoDecoder::State*>(opaque);
    int   n     = state->input_stream->as_mut_ptr()->read(buf, buf_size);
    if (n == 0) return AVERROR_EOF;
    if (n < 0) return AVERROR(rstd::cppstd::IO_ERROR);
    return n;
}
rstd::int64_t avio_seek_shim(void* opaque, rstd::int64_t offset, int whence) {
    auto* state = static_cast<VideoDecoder::State*>(opaque);
    return state->input_stream->as_mut_ptr()->seek(offset, whence);
}
} // namespace

VideoDecoder::~VideoDecoder() = default;

auto VideoDecoder::probe_native(ref<str> path) -> Result<ProbeResult, Error> {
    Error err;
    u32   width;
    u32   height;
    if (! probe_native_impl(path, &width, &height, &err)) return Err(rstd::move(err));
    return Ok(ProbeResult { width, height });
}

auto VideoDecoder::open(ref<str> path, u32 target_width, u32 target_height, bool loop)
    -> Result<rstd::boxed::Box<VideoDecoder>, Error> {
    Error err;
    auto  decoder = build_internal(InputSpec { rstd::string::String::make(path), None() },
                                   target_width,
                                   target_height,
                                   loop,
                                   nullptr,
                                   FrameKind::Sw,
                                   &err);
    if (decoder.is_none()) return Err(rstd::move(err));
    return Ok(rstd::move(decoder).unwrap());
}

auto VideoDecoder::open_with_vk(ref<str> path, u32 target_width, u32 target_height, bool loop,
                                const Producer& producer, const OpenOpts& opts)
    -> Result<rstd::boxed::Box<VideoDecoder>, Error> {
    auto render_node = resolve_render_node(producer, opts);
    /* Resolve trial order. Auto = Vulkan first, then VAAPI; explicit
     * single-mode skips the others; None goes straight to sw. */
    HwAccel order[2] = { HwAccel::None, HwAccel::None };
    int     n_order  = 0;
    switch (opts.hwaccel) {
    case HwAccel::Auto:
        order[0] = HwAccel::Vulkan;
        order[1] = HwAccel::Vaapi;
        n_order  = 2;
        break;
    case HwAccel::Vulkan:
        order[0] = HwAccel::Vulkan;
        n_order  = 1;
        break;
    case HwAccel::Vaapi:
        order[0] = HwAccel::Vaapi;
        n_order  = 1;
        break;
    case HwAccel::None: n_order = 0; break;
    }

    for (int i = 0; i < n_order; ++i) {
        Error        local_err;
        AVBufferRef* hwd  = nullptr;
        FrameKind    kind = FrameKind::Sw;
        if (order[i] == HwAccel::Vulkan) {
            hwd  = make_shared_vulkan_hwdevice(producer, &local_err);
            kind = FrameKind::VulkanShared;
        } else if (order[i] == HwAccel::Vaapi) {
#if defined(WAVSEN_HAS_VAAPI)
            hwd  = make_vaapi_hwdevice(render_node, &local_err);
            kind = FrameKind::VaapiDrm;
#else
            local_err.message =
                rstd::string::String::make("wavsen built without VAAPI support"_str);
#endif
        }
        if (! hwd) {
            rstd::log::info("VideoDecoder: hwaccel attempt {} skipped: {}",
                            order[i] == HwAccel::Vulkan ? "vulkan" : "vaapi",
                            local_err.message.as_str());
            continue;
        }
        Error err;
        auto  decoder = build_internal(InputSpec { rstd::string::String::make(path), None() },
                                       target_width,
                                       target_height,
                                       loop,
                                       hwd,
                                       kind,
                                       &err);
        if (decoder.is_some()) return Ok(rstd::move(decoder).unwrap());
        rstd::log::info("VideoDecoder: hwaccel {} build_internal failed: {} — trying next",
                        order[i] == HwAccel::Vulkan ? "vulkan" : "vaapi",
                        err.message.as_str());
        /* build_internal already unref'd `hwd` on failure via state. */
    }

    /* Final fallback: pure sw decode. */
    Error err;
    auto  decoder = build_internal(InputSpec { rstd::string::String::make(path), None() },
                                   target_width,
                                   target_height,
                                   loop,
                                   nullptr,
                                   FrameKind::Sw,
                                   &err);
    if (decoder.is_none()) return Err(rstd::move(err));
    return Ok(rstd::move(decoder).unwrap());
}

auto VideoDecoder::open_from_stream(InputStreamFactory make_stream, u32 target_width,
                                    u32 target_height, bool loop, const Producer* producer,
                                    const OpenOpts& opts)
    -> Result<rstd::boxed::Box<VideoDecoder>, Error> {
    auto fresh_stream = [&] {
        return make_stream.as_mut_ptr()->operator()();
    };

    /* Sw / vaapi-only fast path (no shared Vulkan hwdev). */
    if (! producer) {
        Error err;
        auto  decoder = build_internal(InputSpec { {}, Some(fresh_stream()) },
                                       target_width,
                                       target_height,
                                       loop,
                                       nullptr,
                                       FrameKind::Sw,
                                       &err);
        if (decoder.is_none()) return Err(rstd::move(err));
        return Ok(rstd::move(decoder).unwrap());
    }

    /* Shared Vulkan path: mirror open_with_vk's trial loop. Each trial
     * gets a fresh IInputStream from the factory — build_internal
     * consumes it, and on failure State's destructor cleans up. */
    auto    render_node = resolve_render_node(*producer, opts);
    HwAccel order[2]    = { HwAccel::None, HwAccel::None };
    int     n_order     = 0;
    switch (opts.hwaccel) {
    case HwAccel::Auto:
        order[0] = HwAccel::Vulkan;
        order[1] = HwAccel::Vaapi;
        n_order  = 2;
        break;
    case HwAccel::Vulkan:
        order[0] = HwAccel::Vulkan;
        n_order  = 1;
        break;
    case HwAccel::Vaapi:
        order[0] = HwAccel::Vaapi;
        n_order  = 1;
        break;
    case HwAccel::None: n_order = 0; break;
    }

    for (int i = 0; i < n_order; ++i) {
        Error        local_err;
        AVBufferRef* hwd  = nullptr;
        FrameKind    kind = FrameKind::Sw;
        if (order[i] == HwAccel::Vulkan) {
            hwd  = make_shared_vulkan_hwdevice(*producer, &local_err);
            kind = FrameKind::VulkanShared;
        } else if (order[i] == HwAccel::Vaapi) {
#if defined(WAVSEN_HAS_VAAPI)
            hwd  = make_vaapi_hwdevice(render_node, &local_err);
            kind = FrameKind::VaapiDrm;
#else
            local_err.message =
                rstd::string::String::make("wavsen built without VAAPI support"_str);
#endif
        }
        if (! hwd) {
            rstd::log::info("VideoDecoder: hwaccel attempt {} skipped: {}",
                            order[i] == HwAccel::Vulkan ? "vulkan" : "vaapi",
                            local_err.message.as_str());
            continue;
        }
        Error err;
        auto  decoder = build_internal(InputSpec { {}, Some(fresh_stream()) },
                                       target_width,
                                       target_height,
                                       loop,
                                       hwd,
                                       kind,
                                       &err);
        if (decoder.is_some()) return Ok(rstd::move(decoder).unwrap());
        rstd::log::info("VideoDecoder: hwaccel {} build_internal failed: {} — trying next",
                        order[i] == HwAccel::Vulkan ? "vulkan" : "vaapi",
                        err.message.as_str());
        /* build_internal already unref'd `hwd` via State on failure. */
    }

    /* Final sw fallback — also covers HwAccel::None (n_order == 0). */
    Error err;
    auto  decoder = build_internal(InputSpec { {}, Some(fresh_stream()) },
                                   target_width,
                                   target_height,
                                   loop,
                                   nullptr,
                                   FrameKind::Sw,
                                   &err);
    if (decoder.is_none()) return Err(rstd::move(err));
    return Ok(rstd::move(decoder).unwrap());
}

auto VideoDecoder::build_internal(InputSpec input, u32 target_width, u32 target_height, bool loop,
                                  void* prebuilt_hwdevice_value, FrameKind requested_kind,
                                  Error* err) -> Option<rstd::boxed::Box<VideoDecoder>> {
    AVBufferRef* prebuilt_hwdevice = static_cast<AVBufferRef*>(prebuilt_hwdevice_value);
    if (target_width == u32() || target_height == u32()) {
        fail(err, "target dimensions must be non-zero"_str);
        if (prebuilt_hwdevice) av_buffer_unref(&prebuilt_hwdevice);
        return None();
    }
    if (input.path.is_empty() && input.stream.is_none()) {
        fail(err, "InputSpec: neither path nor stream provided"_str);
        if (prebuilt_hwdevice) av_buffer_unref(&prebuilt_hwdevice);
        return None();
    }
    /* NV12 chroma is half-resolution → both dims must be even. */
    if (target_width % u32(2) != u32()) ++target_width;
    if (target_height % u32(2) != u32()) ++target_height;

    auto state           = rstd::boxed::Box<VideoDecoder::State>::make();
    auto state_ptr       = rstd::move(state).into_raw().as_raw_ptr();
    auto self            = rstd::boxed::Box<VideoDecoder>::make(state_ptr);
    self->target_width_  = target_width;
    self->target_height_ = target_height;
    self->loop_          = loop;
    /* Provisional; downgraded to Sw below if hwdevice attach fails. */
    self->kind_ = requested_kind;
    /* Take ownership of the caller's hwdevice ref immediately so that
     * any early-return path below (avformat_open_input failure etc.)
     * unrefs it via state.hwd's deleter rather than leaking. The codec
     * gets its own ref later. */
    if (prebuilt_hwdevice) self->state_->hwd.reset(prebuilt_hwdevice);

    AVFormatContext* raw_fmt = nullptr;
    if (input.stream.is_some()) {
        /* Custom-IO open: install an AVIOContext that calls back into
         * the caller's IInputStream. fmt->pb must outlive fmt — see
         * State's explicit destructor for the cleanup ordering. */
        auto input_stream          = rstd::move(input.stream).unwrap();
        self->state_->input_stream = Some(rstd::move(input_stream));
        constexpr int kAvioBuf     = 4096;
        auto*         avio_buf     = static_cast<unsigned char*>(av_malloc(kAvioBuf));
        if (! avio_buf) {
            fail(err, "av_malloc(avio buffer) failed"_str);
            return None();
        }
        self->state_->avio_ctx = avio_alloc_context(avio_buf,
                                                    kAvioBuf,
                                                    /*write_flag=*/0,
                                                    /*opaque=*/&*self->state_,
                                                    &avio_read_shim,
                                                    /*write_packet=*/nullptr,
                                                    &avio_seek_shim);
        if (! self->state_->avio_ctx) {
            av_free(avio_buf);
            fail(err, "avio_alloc_context failed"_str);
            return None();
        }
        raw_fmt = avformat_alloc_context();
        if (! raw_fmt) {
            fail(err, "avformat_alloc_context failed"_str);
            return None();
        }
        raw_fmt->pb = self->state_->avio_ctx;
        raw_fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (int rc = avformat_open_input(&raw_fmt, nullptr, nullptr, nullptr); rc < 0) {
            /* On failure avformat_open_input frees raw_fmt for us, but
             * leaves avio_ctx alone — State's destructor will free it. */
            fail(err, rstd::format("avformat_open_input(stream): {}", av_err_str(rc).as_str()));
            return None();
        }
    } else {
        auto input_path     = rstd::ffi::CString::make(rstd::move(input.path)).unwrap();
        auto input_path_raw = input_path.as_ptr();
        if (int rc = avformat_open_input(&raw_fmt, input_path_raw, nullptr, nullptr); rc < 0) {
            fail(err, rstd::format("avformat_open_input: {}", av_err_str(rc).as_str()));
            return None();
        }
    }
    self->state_->fmt.reset(raw_fmt);

    if (int rc = avformat_find_stream_info(self->state_->fmt.get(), nullptr); rc < 0) {
        fail(err, rstd::format("avformat_find_stream_info: {}", av_err_str(rc).as_str()));
        return None();
    }

    int idx = av_find_best_stream(self->state_->fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        fail(err, "no video stream in file"_str);
        return None();
    }
    self->state_->video_idx = idx;
    AVStream*          st   = self->state_->fmt->streams[idx];
    AVCodecParameters* par  = st->codecpar;
    self->state_->stream_tb = st->time_base;

    /* FFmpeg's native `av1` decoder has no software path — it's a
     * parser + hwaccel dispatcher and returns ENOSYS on send_packet when
     * no hardware accelerator picked up the stream. Prefer libdav1d for
     * the pure-sw case so disabling hwdec actually works on AV1. */
    const AVCodec* dec = nullptr;
    if (par->codec_id == AV_CODEC_ID_AV1 && requested_kind == FrameKind::Sw) {
        dec = avcodec_find_decoder_by_name("libdav1d");
    }
    if (! dec) dec = avcodec_find_decoder(par->codec_id);
    if (! dec) {
        fail(err, rstd::format("no decoder for codec {}", avcodec_get_name(par->codec_id)));
        return None();
    }
    self->state_->cctx.reset(avcodec_alloc_context3(dec));
    if (! self->state_->cctx) {
        fail(err, "avcodec_alloc_context3 failed"_str);
        return None();
    }
    if (int rc = avcodec_parameters_to_context(self->state_->cctx.get(), par); rc < 0) {
        fail(err, rstd::format("avcodec_parameters_to_context: {}", av_err_str(rc).as_str()));
        return None();
    }

    /* Hand the codec its own ref on the hwdevice the trial loop picked
     * (if any). Sw mode has hwd == nullptr — codec stays sw. */
    if (self->state_->hwd) {
        self->state_->cctx->hw_device_ctx = av_buffer_ref(self->state_->hwd.get());
        if (requested_kind == FrameKind::VulkanShared) {
            /* get_format_prefer_vulkan runs during avcodec_open2 below
             * and bootstraps a compatible hw_frames_ctx if it picks
             * AV_PIX_FMT_VULKAN. On any failure inside that
             * callback the codec falls through to a sw pix_fmt; we
             * detect that after open and reset kind_ to Sw. */
            self->state_->cctx->get_format = get_format_prefer_vulkan;
            rstd::log::info("VideoDecoder: AV_HWDEVICE_TYPE_VULKAN attached for codec {}.",
                            avcodec_get_name(par->codec_id));
        }
#if defined(WAVSEN_HAS_VAAPI)
        else if (requested_kind == FrameKind::VaapiDrm) {
            self->state_->cctx->get_format = get_format_prefer_vaapi;
            rstd::log::info("VideoDecoder: AV_HWDEVICE_TYPE_VAAPI attached for codec {}.",
                            avcodec_get_name(par->codec_id));
        }
#endif
    } else {
        rstd::log::info("VideoDecoder: sw decode for codec {}.", avcodec_get_name(par->codec_id));
    }

    if (int rc = avcodec_open2(self->state_->cctx.get(), dec, nullptr); rc < 0) {
        fail(err, rstd::format("avcodec_open2: {}", av_err_str(rc).as_str()));
        return None();
    }

    self->state_->pkt.reset(av_packet_alloc());
    self->state_->src_frame.reset(av_frame_alloc());
    if (! self->state_->pkt || ! self->state_->src_frame) {
        fail(err, "av_packet_alloc / av_frame_alloc failed"_str);
        return None();
    }

    /* Force get_format to run by feeding one probe packet — h264
     * (and most modern codecs) only invoke get_format on first frame
     * decode, not at avcodec_open2 time. We need to know whether the
     * hwaccel actually accepted the codec NOW, before returning, so
     * the trial loop can fall through to the next backend / sw on
     * mismatch instead of dying at first frame.
     *
     * The probe is also necessary for VAAPI specifically: when libva
     * rejects a profile (e.g. h264 profile 77), FFmpeg's get_format
     * silently falls back to a sw pix_fmt — avcodec_open2 still
     * succeeds, but the per-frame pump would then see a sw frame and
     * abort with "decoder produced non-VAAPI frame".
     *
     * Read frames until we hit a video packet, send it, then seek back
     * and flush so the user's first next_*_frame starts from byte 0. */
    AVPixelFormat want_pix_fmt = AV_PIX_FMT_NONE;
    const char*   hw_label     = nullptr;
    if (requested_kind == FrameKind::VulkanShared) {
        want_pix_fmt = AV_PIX_FMT_VULKAN;
        hw_label     = "vulkan";
    } else if (requested_kind == FrameKind::VaapiDrm) {
        want_pix_fmt = AV_PIX_FMT_VAAPI;
        hw_label     = "vaapi";
    }
    if (want_pix_fmt != AV_PIX_FMT_NONE) {
        AVPacket* probe = av_packet_alloc();
        if (probe) {
            bool got_video = false;
            while (av_read_frame(self->state_->fmt.get(), probe) >= 0) {
                if (probe->stream_index == self->state_->video_idx) {
                    got_video = true;
                    break;
                }
                av_packet_unref(probe);
            }
            if (got_video) {
                avcodec_send_packet(self->state_->cctx.get(), probe);
                av_packet_unref(probe);
            }
            av_packet_free(&probe);

            if (av_seek_frame(self->state_->fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD) >= 0) {
                avcodec_flush_buffers(self->state_->cctx.get());
            }
        }

        if (self->state_->cctx->pix_fmt != want_pix_fmt) {
            /* Return failure so the trial loop in open_with_vk /
             * open_from_stream falls through to the next backend (or
             * final sw fallback, which picks libdav1d for AV1). Just
             * flipping kind_ would leave cctx bound to a hw-only native
             * decoder and trip ENOSYS / wrong-format on first frame. */
            fail(err,
                 rstd::format("{} hwaccel rejected codec {} (probe pix_fmt={})",
                              hw_label,
                              avcodec_get_name(par->codec_id),
                              av_get_pix_fmt_name(self->state_->cctx->pix_fmt)));
            return None();
        }
    }

    return Some(rstd::move(self));
}

int VideoDecoder::next_vk_frame_(VkFrameView& out, Error* err) {
    if (kind_ != FrameKind::VulkanShared) {
        fail(err, "next_vk_frame called on non-shared-device decoder"_str);
        return -1;
    }
    State& st     = *state_;
    bool   looped = false;

    /* Release the previously-yielded AVVkFrame back to the pool. The
     * caller's GPU work that referenced it has been queue-submitted by
     * now (the contract of next_vk_frame), so it's safe to unref —
     * the AVVkFrame survives in the pool's hwframe context. */
    av_frame_unref(st.src_frame.get());

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
            if (st.src_frame->format != AV_PIX_FMT_VULKAN) {
                fail(err, "next_vk_frame: decoder produced non-vulkan frame"_str);
                return -1;
            }
            auto* vkf        = reinterpret_cast<AVVkFrame*>(st.src_frame->data[0]);
            out.img          = vkf->img;
            out.layout       = vkf->layout;
            out.sem          = vkf->sem;
            out.sem_value    = vkf->sem_value;
            out.queue_family = vkf->queue_family;
            out.plane_count  = vkf->img[1] != VK_NULL_HANDLE ? u32(2) : u32(1);
            out.width        = u32(static_cast<rstd::uint32_t>(st.src_frame->width));
            out.height       = u32(static_cast<rstd::uint32_t>(st.src_frame->height));
            out.colorspace   = map_colorspace(st.src_frame->colorspace);
            out.color_range  = map_range(st.src_frame->color_range);
            /* Look up the AVHWFramesContext's sw_format to know whether
             * the GPU images we're about to sample are 8-bit (NV12) or
             * 10-bit (P010). Both are 2-image disjoint formats here. */
            out.bit_depth = u32(8);
            if (st.src_frame->hw_frames_ctx) {
                auto* hwfc =
                    reinterpret_cast<AVHWFramesContext*>(st.src_frame->hw_frames_ctx->data);
                if (hwfc->sw_format == AV_PIX_FMT_P010 || hwfc->sw_format == AV_PIX_FMT_P016) {
                    out.bit_depth = u32(16);
                }
            }
            const rstd::int64_t pts = (st.src_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                                          ? st.src_frame->best_effort_timestamp
                                          : st.src_frame->pts;
            out.pts_seconds = pts == AV_NOPTS_VALUE
                                  ? f64(-1.0)
                                  : f64(static_cast<double>(pts) * ffi::av_q2d(st.stream_tb));
            return looped ? 2 : 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (! seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed"_str);
                    return -1;
                }
                looped = true;
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_receive_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, rstd::format("av_read_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_send_packet: {}", av_err_str(rc).as_str()));
            return -1;
        }
    }
}

int VideoDecoder::next_drm_frame_(DrmFrameView& out, Error* err) {
    if (kind_ != FrameKind::VaapiDrm) {
        fail(err, "next_drm_frame called on non-VAAPI decoder"_str);
        return -1;
    }
    State& st     = *state_;
    bool   looped = false;

    if (! st.drm_frame) st.drm_frame.reset(av_frame_alloc());
    if (! st.drm_frame) {
        fail(err, "av_frame_alloc(drm_frame) failed"_str);
        return -1;
    }

    /* Release prior pull's mapped fds before grabbing the next surface. */
    av_frame_unref(st.src_frame.get());
    av_frame_unref(st.drm_frame.get());

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
#if defined(WAVSEN_HAS_VAAPI)
            if (st.src_frame->format != AV_PIX_FMT_VAAPI) {
                fail(err, "next_drm_frame: decoder produced non-VAAPI frame"_str);
                return -1;
            }
#else
            fail(err, "next_drm_frame: VAAPI support not built"_str);
            return -1;
#endif
            st.drm_frame->format = AV_PIX_FMT_DRM_PRIME;
            int mrc              = av_hwframe_map(st.drm_frame.get(),
                                                  st.src_frame.get(),
                                                  AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
            if (mrc < 0) {
                fail(err, rstd::format("av_hwframe_map(DRM_PRIME): {}", av_err_str(mrc).as_str()));
                return -1;
            }
            const auto* desc = reinterpret_cast<const AVDRMFrameDescriptor*>(st.drm_frame->data[0]);
            if (! desc) {
                fail(err, "av_hwframe_map: DRM_PRIME descriptor null"_str);
                return -1;
            }
            const int n_obj  = desc->nb_objects < 4 ? desc->nb_objects : 4;
            const int n_lay  = desc->nb_layers < 4 ? desc->nb_layers : 4;
            out.object_count = u32(static_cast<rstd::uint32_t>(n_obj));
            for (int i = 0; i < n_obj; ++i) {
                out.objects[i].fd              = desc->objects[i].fd;
                out.objects[i].size            = u64(desc->objects[i].size);
                out.objects[i].format_modifier = desc->objects[i].format_modifier;
            }
            out.layer_count = u32(static_cast<rstd::uint32_t>(n_lay));
            for (int li = 0; li < n_lay; ++li) {
                const auto& la             = desc->layers[li];
                out.layers[li].fourcc      = la.format;
                const int np               = la.nb_planes < 4 ? la.nb_planes : 4;
                out.layers[li].plane_count = u32(static_cast<rstd::uint32_t>(np));
                for (int p = 0; p < np; ++p) {
                    out.layers[li].planes[p].object_index =
                        u32(static_cast<rstd::uint32_t>(la.planes[p].object_index));
                    out.layers[li].planes[p].offset = u64(la.planes[p].offset);
                    out.layers[li].planes[p].pitch  = u64(la.planes[p].pitch);
                }
            }
            out.width       = u32(static_cast<rstd::uint32_t>(st.src_frame->width));
            out.height      = u32(static_cast<rstd::uint32_t>(st.src_frame->height));
            out.colorspace  = map_colorspace(st.src_frame->colorspace);
            out.color_range = map_range(st.src_frame->color_range);
            /* VAAPI 8-bit profiles land as NV12; 10-bit as P010. We only
             * support 8-bit on the DRM_PRIME zero-copy path for now. */
            out.bit_depth           = u32(8);
            const rstd::int64_t pts = (st.src_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                                          ? st.src_frame->best_effort_timestamp
                                          : st.src_frame->pts;
            out.pts_seconds = pts == AV_NOPTS_VALUE
                                  ? f64(-1.0)
                                  : f64(static_cast<double>(pts) * ffi::av_q2d(st.stream_tb));
            return looped ? 2 : 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (! seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed"_str);
                    return -1;
                }
                looped = true;
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_receive_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, rstd::format("av_read_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_send_packet: {}", av_err_str(rc).as_str()));
            return -1;
        }
    }
}

int VideoDecoder::next_frame_(Nv12Frame& out, Error* err) {
    State& st     = *state_;
    bool   looped = false;

    /* Resize output buffer to NV12 size on first call (and on extent
     * change, but the extent is fixed for VideoDecoder lifetime). */
    const usize want = usize(target_width_.to_primitive()) * usize(target_height_.to_primitive()) *
                       usize(3) / usize(2);
    if (out.width != target_width_ || out.height != target_height_ || out.data.len() != want) {
        out.width                = target_width_;
        out.height               = target_height_;
        const rstd::uint8_t zero = 0;
        out.data.resize(want, zero);
    }

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
            /* If the decoder produced a vulkan-typed frame (Iter 4 hw
             * path), download it to a sw frame first. The download lands
             * in whatever YUV format the AVHWFramesContext exposes —
             * typically NV12 — and swscale handles whatever it is. */
            AVFrame* feed = st.src_frame.get();
            if (feed->format == AV_PIX_FMT_VULKAN) {
                if (! st.sw_frame) st.sw_frame.reset(av_frame_alloc());
                if (! st.sw_frame) {
                    fail(err, "av_frame_alloc(sw_frame) failed"_str);
                    return -1;
                }
                av_frame_unref(st.sw_frame.get());
                int trc = av_hwframe_transfer_data(st.sw_frame.get(), feed, 0);
                if (trc < 0) {
                    fail(err,
                         rstd::format("av_hwframe_transfer_data: {}", av_err_str(trc).as_str()));
                    av_frame_unref(st.src_frame.get());
                    return -1;
                }
                /* Preserve PTS across the transfer (transfer_data copies
                 * pixel data only). */
                st.sw_frame->pts                   = feed->pts;
                st.sw_frame->best_effort_timestamp = feed->best_effort_timestamp;
                feed                               = st.sw_frame.get();
            }

            const auto src_fmt = static_cast<AVPixelFormat>(feed->format);
            const int  src_w   = feed->width;
            const int  src_h   = feed->height;
            if (src_w <= 0 || src_h <= 0 || src_fmt == AV_PIX_FMT_NONE) {
                fail(err, "decoded frame has invalid dimensions/format"_str);
                return -1;
            }
            if (! ensure_sws(st, src_w, src_h, src_fmt, target_width_, target_height_)) {
                fail(err,
                     rstd::format("sws_getContext failed (src={})", av_get_pix_fmt_name(src_fmt)));
                return -1;
            }
            rstd::uint8_t* y_dst         = out.data.data();
            rstd::uint8_t* uv_dst        = out.data.data() + (usize(target_width_.to_primitive()) *
                                                              usize(target_height_.to_primitive()))
                                                                 .to_primitive();
            rstd::uint8_t* dst_planes[4] = { y_dst, uv_dst, nullptr, nullptr };
            int dst_strides[4] = { static_cast<int>(target_width_.to_primitive()),
                                   static_cast<int>(
                                       target_width_.to_primitive()), /* NV12 UV pitch == width */
                                   0,
                                   0 };
            int scaled         = sws_scale(
                st.sws.get(), feed->data, feed->linesize, 0, src_h, dst_planes, dst_strides);
            if (scaled <= 0) {
                fail(err, "sws_scale produced no rows"_str);
                return -1;
            }
            const rstd::int64_t pts = (feed->best_effort_timestamp != AV_NOPTS_VALUE)
                                          ? feed->best_effort_timestamp
                                          : feed->pts;
            out.pts_seconds = pts == AV_NOPTS_VALUE
                                  ? f64(-1.0)
                                  : f64(static_cast<double>(pts) * ffi::av_q2d(st.stream_tb));
            out.colorspace  = map_colorspace(feed->colorspace);
            out.color_range = map_range(feed->color_range);
            av_frame_unref(st.src_frame.get());
            if (st.sw_frame) av_frame_unref(st.sw_frame.get());
            return looped ? 2 : 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (! seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed"_str);
                    return -1;
                }
                looped = true;
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_receive_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }

        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, rstd::format("av_read_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_send_packet: {}", av_err_str(rc).as_str()));
            return -1;
        }
    }
}

// ---------------------------------------------------------------------------
// Public Result wrappers for the per-frame pull
// ---------------------------------------------------------------------------

auto VideoDecoder::next_frame(Nv12Frame& out) -> Result<NextFrame, Error> {
    Error err;
    int   rc = next_frame_(out, &err);
    if (rc < 0) return Err(rstd::move(err));
    if (rc == 1) return Ok(NextFrame::Eof);
    if (rc == 2) return Ok(NextFrame::Looped);
    return Ok(NextFrame::Ok);
}

auto VideoDecoder::next_vk_frame(VkFrameView& out) -> Result<NextFrame, Error> {
    Error err;
    int   rc = next_vk_frame_(out, &err);
    if (rc < 0) return Err(rstd::move(err));
    if (rc == 1) return Ok(NextFrame::Eof);
    if (rc == 2) return Ok(NextFrame::Looped);
    return Ok(NextFrame::Ok);
}

auto VideoDecoder::next_drm_frame(DrmFrameView& out) -> Result<NextFrame, Error> {
    Error err;
    int   rc = next_drm_frame_(out, &err);
    if (rc < 0) return Err(rstd::move(err));
    if (rc == 1) return Ok(NextFrame::Eof);
    if (rc == 2) return Ok(NextFrame::Looped);
    return Ok(NextFrame::Ok);
}

auto VideoDecoder::seek(f64 seconds) -> Result<empty, Error> {
    if (! seconds.is_finite() || seconds < f64()) {
        return Err(Error("seek time must be finite and non-negative"_str));
    }

    State& st    = *state_;
    auto   limit = duration();
    if (limit.is_some()) seconds = seconds.min(*limit);
    const double time_base = ffi::av_q2d(st.stream_tb);
    if (time_base <= 0.0) {
        return Err(Error("video stream has an invalid time base"_str));
    }
    const auto timestamp = static_cast<rstd::int64_t>(seconds.to_primitive() / time_base);
    const int  rc = av_seek_frame(st.fmt.get(), st.video_idx, timestamp, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) {
        return Err(Error(rstd::format("av_seek_frame: {}", av_err_str(rc).as_str())));
    }

    avcodec_flush_buffers(st.cctx.get());
    av_packet_unref(st.pkt.get());
    av_frame_unref(st.src_frame.get());
    if (st.sw_frame) av_frame_unref(st.sw_frame.get());
    if (st.drm_frame) av_frame_unref(st.drm_frame.get());
    st.flushing = false;
    return Ok(empty {});
}

auto VideoDecoder::duration() const -> Option<f64> {
    const State& st = *state_;
    if (st.fmt->duration > 0) {
        return Some(f64(static_cast<double>(st.fmt->duration) / AV_TIME_BASE));
    }
    const auto* stream = st.fmt->streams[st.video_idx];
    if (stream->duration <= 0) return None();
    return Some(f64(static_cast<double>(stream->duration) * ffi::av_q2d(stream->time_base)));
}

} // namespace wavsen::video
