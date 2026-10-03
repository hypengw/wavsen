module wavsen.image;
import wavsen.ffi.ffmpeg;
import wavsen.ffi.ffmpeg.owner;
import wavsen.ffi.ffmpeg.image;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::sync::Arc;

namespace wavsen::image
{
namespace
{
void resize_associated_rgba(const Pixels& source, int sw, int sh, Pixels& target, int w, int h) {
    const auto* source_data   = source.data();
    auto*       target_data   = target.data_mut();
    const auto  source_stride = rstd::size_t(sw) * 4;
    const auto  target_stride = rstd::size_t(w) * 4;
    for (int y = 0; y < h; ++y) {
        double      sy = rstd::cmp::max(0.0, (double(y) + 0.5) * sh / h - 0.5);
        int         y0 = static_cast<int>(sy), y1 = rstd::cmp::min(y0 + 1, sh - 1);
        double      fy         = sy - y0;
        const auto* top_row    = source_data + rstd::size_t(y0) * source_stride;
        const auto* bottom_row = source_data + rstd::size_t(y1) * source_stride;
        auto*       out        = target_data + rstd::size_t(y) * target_stride;
        for (int x = 0; x < w; ++x) {
            double      sx = rstd::cmp::max(0.0, (double(x) + 0.5) * sw / w - 0.5);
            int         x0 = static_cast<int>(sx), x1 = rstd::cmp::min(x0 + 1, sw - 1);
            double      fx           = sx - x0;
            const auto  left         = rstd::size_t(x0) * 4;
            const auto  right        = rstd::size_t(x1) * 4;
            const auto* top_left     = top_row + left;
            const auto* top_right    = top_row + right;
            const auto* bottom_left  = bottom_row + left;
            const auto* bottom_right = bottom_row + right;
            for (int c = 0; c < 4; ++c) {
                double top    = double(top_left[c]) * (1 - fx) + double(top_right[c]) * fx;
                double bottom = double(bottom_left[c]) * (1 - fx) + double(bottom_right[c]) * fx;
                out[c]        = static_cast<rstd::uint8_t>(top * (1 - fy) + bottom * fy + 0.5);
            }
            out += 4;
        }
    }
}
} // namespace

struct Pixels::Storage {
    Budget budget;
    Layout layout;
    void*  pointer;
    Storage(Budget b, Layout l, void* p): budget(rstd::move(b)), layout(l), pointer(p) {}
    ~Storage() { rstd::as<Allocator>(budget).deallocate(pointer, layout); }
};

Pixels::Pixels(Arc<Storage> storage): storage_(rstd::move(storage)) {}
Pixels::Pixels(Pixels&&) noexcept                    = default;
auto Pixels::operator=(Pixels&&) noexcept -> Pixels& = default;
Pixels::~Pixels()                                    = default;
auto Pixels::allocate(Budget budget, usize bytes, usize alignment) -> Result<Pixels, Error> {
    if (! alignment.is_power_of_two()) return Err(Error::InvalidInput);
    auto layout = Layout::from_size_align(bytes, alignment);
    if (layout.is_none()) return Err(Error::ResourceLimit);
    auto memory = rstd::as<Allocator>(budget).allocate_zeroed(*layout);
    if (memory.is_err()) return Err(Error::ResourceLimit);
    auto* pointer = memory.unwrap_unchecked().pointer;
    auto  storage = Arc<Storage>::try_make(budget.clone(), *layout, pointer);
    if (storage.is_err()) {
        rstd::as<Allocator>(budget).deallocate(pointer, *layout);
        return Err(Error::ResourceLimit);
    }
    return Ok(Pixels(storage.unwrap_unchecked()));
}
auto Pixels::clone() const -> Pixels { return Pixels(storage_.clone()); }
auto Pixels::data() const -> const rstd::uint8_t* {
    return static_cast<const rstd::uint8_t*>(storage_->pointer);
}
auto Pixels::data_mut() -> rstd::uint8_t* { return static_cast<rstd::uint8_t*>(storage_->pointer); }
auto Pixels::size() const -> usize { return storage_->layout.size; }

auto Cancellation::make() -> Result<Cancellation, Error> {
    auto state = Arc<rstd::sync::atomic::Atomic<bool>>::try_make(false);
    if (state.is_err()) return Err(Error::ResourceLimit);
    return Ok(Cancellation(state.unwrap_unchecked()));
}

struct MemoryReader {
    Pixels pixels;
};
} // namespace wavsen::image

namespace rstd
{
template<>
struct Impl<io::ReadAt, wavsen::image::MemoryReader> : ImplBase<wavsen::image::MemoryReader> {
    auto read_at(mut_ref<u8[]> target, u64 offset) const -> io::Result<usize> {
        const auto& pixels = this->self().pixels;
        if (offset >= u64(pixels.size().to_primitive())) return Ok(usize());
        auto count = target.len().min(pixels.size() - usize(offset.to_primitive()));
        rstd::mem::memcpy(target.as_raw_ptr(), pixels.data() + offset.to_primitive(), count);
        return Ok(count);
    }
};
} // namespace rstd

namespace wavsen::image
{
auto Source::file(ref<rstd::path::Path> path) -> Result<Source, Error> {
    auto file = rstd::fs::File::open(path);
    if (file.is_err()) return Err(Error::Io);
    auto metadata = file->metadata();
    if (metadata.is_err()) return Err(Error::Io);
    auto owner = rstd::io::SharedReadAt::try_make(file.unwrap_unchecked());
    if (owner.is_err()) return Err(Error::ResourceLimit);
    auto range = rstd::io::ReadRange::make(owner.unwrap_unchecked(), u64(), metadata->len());
    if (range.is_err()) return Err(Error::InvalidInput);
    return Ok(Source(range.unwrap_unchecked(), String::make(path.to_str().unwrap_or(ref<str> {}))));
}

auto Source::memory(Pixels pixels, ref<str> name) -> Result<Source, Error> {
    auto length = u64(pixels.size().to_primitive());
    auto owner  = rstd::io::SharedReadAt::try_make(MemoryReader { rstd::move(pixels) });
    if (owner.is_err()) return Err(Error::ResourceLimit);
    auto range = rstd::io::ReadRange::make(owner.unwrap_unchecked(), u64(), length);
    if (range.is_err()) return Err(Error::InvalidInput);
    return Ok(Source(range.unwrap_unchecked(), String::make(name)));
}

struct ImageSession::Impl {
    Source                   source;
    Budget                   budget;
    Cancellation             cancellation;
    Limits                   limits;
    rstd::io::RangeReader    input;
    AVIOContext*             io {};
    ffi::ffmpeg::FmtCtxPtr   format;
    ffi::ffmpeg::CodecCtxPtr codec;
    ffi::ffmpeg::FramePtr    frame;
    ffi::ffmpeg::PacketPtr   packet;
    ffi::ffmpeg::ImageScaler scaler;
    ImageInfo                metadata;
    rstd::time::Instant      started = rstd::time::Instant::now();
    int                      stream {};
    bool                     flushing {};
    u64                      index;

    Impl(Source s, Budget b, Cancellation c, Limits l)
        : source(rstd::move(s)),
          budget(rstd::move(b)),
          cancellation(rstd::move(c)),
          limits(l),
          input(source.reader()) {}
    ~Impl() {
        codec.reset();
        format.reset();
        if (io) {
            av_freep(&io->buffer);
            avio_context_free(&io);
        }
    }
    bool stopped() const {
        return cancellation.cancelled() || started.elapsed().as_millis() > limits.operation_ms;
    }
    static int interrupted(void* p) { return static_cast<Impl*>(p)->stopped(); }
    static int read(void* p, rstd::uint8_t* bytes, int size) {
        auto& self = *static_cast<Impl*>(p);
        if (self.stopped()) return AVERROR_EXIT;
        auto target =
            mut_ref<u8[]>::from_raw_parts(reinterpret_cast<rstd::byte*>(bytes), usize(size));
        auto count = self.input.read(target);
        if (count.is_err()) return AVERROR(EIO);
        return *count == usize() ? AVERROR_EOF : static_cast<int>(count->to_primitive());
    }
    static rstd::int64_t seek(void* p, rstd::int64_t offset, int whence) {
        auto& self = *static_cast<Impl*>(p);
        if (self.stopped()) return AVERROR_EXIT;
        if (whence == AVSEEK_SIZE)
            return static_cast<rstd::int64_t>(self.source.size().to_primitive());
        using rstd::io::SeekFrom;
        auto position = whence == 0   ? SeekFrom::from_start(u64(offset))
                        : whence == 1 ? SeekFrom::from_current(i64(offset))
                                      : SeekFrom::from_end(i64(offset));
        if (whence < 0 || whence > 2 || (whence == 0 && offset < 0)) return AVERROR(EIO);
        auto result = self.input.seek(position);
        if (result.is_err()) return AVERROR(EIO);
        return static_cast<rstd::int64_t>(result->to_primitive());
    }
    auto failure() const -> Error {
        return cancellation.cancelled() ? Error::Cancelled
               : stopped()              ? Error::ResourceLimit
                                        : Error::Decode;
    }
    auto open() -> Result<empty, Error> {
        if (cancellation.cancelled()) return Err(Error::Cancelled);
        if (source.size() == u64() || source.size() > limits.encoded_bytes ||
            source.size() > u64(0x7fffffffffffffffULL))
            return Err(Error::ResourceLimit);
        auto* buffer = static_cast<rstd::uint8_t*>(av_malloc(32768));
        if (! buffer) return Err(Error::ResourceLimit);
        io = avio_alloc_context(buffer, 32768, 0, this, read, nullptr, seek);
        if (! io) {
            av_free(buffer);
            return Err(Error::ResourceLimit);
        }
        auto* raw = avformat_alloc_context();
        if (! raw) return Err(Error::ResourceLimit);
        raw->pb = io;
        raw->flags |= AVFMT_FLAG_CUSTOM_IO;
        raw->probesize =
            static_cast<rstd::int64_t>(limits.probe_bytes.min(u64(1024 * 1024)).to_primitive());
        raw->max_analyze_duration = 1000000;
        raw->interrupt_callback   = { interrupted, this };
        AVDictionary* options {};
        av_dict_set(&options, "ignore_loop", "1", 0);
        av_dict_set(&options,
                    "format_whitelist",
                    "png_pipe,jpeg_pipe,bmp_pipe,ppm_pipe,pgm_pipe,pbm_pipe,pam_pipe,tiff_pipe,"
                    "webp_pipe,webp,webp_anim,gif,apng,ico,image2pipe",
                    0);
        av_dict_set(&options, "protocol_whitelist", "", 0);
        auto name = rstd::ffi::CString::make(String::make(source.name()));
        if (name.is_err()) {
            av_dict_free(&options);
            avformat_free_context(raw);
            return Err(Error::InvalidInput);
        }
        const AVInputFormat* input_format {};
        int                  probed = av_probe_input_buffer2(
            io, &input_format, name->as_ptr(), nullptr, 0, static_cast<unsigned>(raw->probesize));
        if (probed < 0) {
            av_dict_free(&options);
            avformat_free_context(raw);
            return Err(failure());
        }
        // image2 owns filename sequences; this session must read only its supplied AVIO.
        if (rstd::ffi::CStr::from_ptr(input_format->name).to_str().unwrap() == "image2"_str)
            input_format = av_find_input_format("image2pipe");
        int opened = avformat_open_input(&raw, name->as_ptr(), input_format, &options);
        av_dict_free(&options);
        if (opened < 0) return Err(failure());
        format.reset(raw);
        // Probe codec parameters without decoding a duplicate full-resolution frame.
        stream = av_find_best_stream(raw, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream < 0) return Err(Error::Unsupported);
        auto* params  = raw->streams[stream]->codecpar;
        auto* decoder = avcodec_find_decoder(params->codec_id);
        if (! decoder) return Err(Error::Unsupported);
        codec.reset(avcodec_alloc_context3(decoder));
        frame.reset(av_frame_alloc());
        packet.reset(av_packet_alloc());
        if (! codec || ! frame || ! packet) return Err(Error::ResourceLimit);
        if (avcodec_parameters_to_context(codec.get(), params) < 0) return Err(Error::Decode);
        codec->thread_count = 1;
        codec->max_pixels =
            static_cast<rstd::int64_t>(limits.source_pixels.min(u64(0x7fffffff)).to_primitive());
        codec->opaque       = &budget;
        codec->get_buffer2  = ffi::ffmpeg::image_get_buffer;
        codec->pkt_timebase = raw->streams[stream]->time_base;
        if (avcodec_open2(codec.get(), decoder, nullptr) < 0) return Err(failure());
        metadata.width  = u32(rstd::cmp::max(params->width, 0));
        metadata.height = u32(rstd::cmp::max(params->height, 0));
        metadata.frame_count =
            u64(rstd::cmp::max<rstd::int64_t>(raw->streams[stream]->nb_frames, 0));
        metadata.animated =
            params->codec_id == AV_CODEC_ID_GIF || params->codec_id == AV_CODEC_ID_APNG ||
            params->codec_id == AV_CODEC_ID_WEBP ||
            rstd::ffi::CStr::from_ptr(raw->iformat->name).to_str().unwrap() == "webp_anim"_str;
        return Ok(empty {});
    }
};

ImageSession::ImageSession(Box<Impl> impl): impl_(rstd::move(impl)) {}
ImageSession::ImageSession(ImageSession&&) noexcept                    = default;
auto ImageSession::operator=(ImageSession&&) noexcept -> ImageSession& = default;
ImageSession::~ImageSession()                                          = default;

auto ImageSession::open(Source source, Budget budget, Cancellation cancellation, Limits limits)
    -> Result<ImageSession, Error> {
    auto impl = Box<Impl>::try_make(
        rstd::move(source), rstd::move(budget), rstd::move(cancellation), limits);
    if (impl.is_err()) return Err(Error::ResourceLimit);
    auto result = (*impl)->open();
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    return Ok(ImageSession(impl.unwrap_unchecked()));
}

auto ImageSession::info() const -> ImageInfo { return impl_->metadata; }

auto ImageSession::reset() -> Result<empty, Error> {
    auto next = open(
        impl_->source.clone(), impl_->budget.clone(), impl_->cancellation.clone(), impl_->limits);
    if (next.is_err()) return Err(next.unwrap_err_unchecked());
    *this = next.unwrap_unchecked();
    return Ok(empty {});
}

auto ImageSession::next(DecodeRequest request) -> Result<Option<DecodedImage>, Error> {
    auto& s   = *impl_;
    s.started = rstd::time::Instant::now();
    if (s.cancellation.cancelled()) return Err(Error::Cancelled);
    av_frame_unref(s.frame.get());
    for (;;) {
        if (s.stopped()) return Err(s.failure());
        auto rc = avcodec_receive_frame(s.codec.get(), s.frame.get());
        if (rc == 0) break;
        if (rc == AVERROR_EOF) {
            s.metadata.frame_count = s.index;
            return Ok(Option<DecodedImage> {});
        }
        if (rc != AVERROR(EAGAIN) || s.flushing) return Err(s.failure());
        do {
            av_packet_unref(s.packet.get());
            rc = av_read_frame(s.format.get(), s.packet.get());
        } while (rc >= 0 && s.packet->stream_index != s.stream && ! s.stopped());
        if (rc == AVERROR_EOF) {
            s.flushing = true;
            rc         = avcodec_send_packet(s.codec.get(), nullptr);
        } else if (rc >= 0) {
            rc = avcodec_send_packet(s.codec.get(), s.packet.get());
        }
        av_packet_unref(s.packet.get());
        if (rc < 0) return Err(s.failure());
    }
    const int sw = s.frame->width, sh = s.frame->height;
    if (sw <= 0 || sh <= 0 || u64(sw) * u64(sh) > s.limits.source_pixels)
        return Err(Error::ResourceLimit);
    s.metadata.width  = u32(sw);
    s.metadata.height = u32(sh);
    u64 width(request.width.to_primitive()), height(request.height.to_primitive());
    if (width == u64() && height == u64()) {
        width  = u64(sw);
        height = u64(sh);
    } else if (width == u64())
        width = (height * u64(sw) / u64(sh)).max(u64(1));
    else if (height == u64())
        height = (width * u64(sh) / u64(sw)).max(u64(1));
    else if (request.mode != ResizeMode::Stretch) {
        const bool width_limited = width * u64(sh) <= height * u64(sw);
        if (width_limited == (request.mode == ResizeMode::Fit))
            height = ((width * u64(sh) + u64(sw / 2)) / u64(sw)).max(u64(1));
        else
            width = ((height * u64(sw) + u64(sh / 2)) / u64(sh)).max(u64(1));
    }
    if (width > u64(0x1fffffff) || height > u64(0x7fffffff) ||
        width * height > s.limits.source_pixels)
        return Err(Error::ResourceLimit);
    const u64 max_width(request.max_width.to_primitive());
    const u64 max_height(request.max_height.to_primitive());
    if (max_width > u64() && width > max_width &&
        (max_height == u64() || max_width * height <= max_height * width)) {
        height = (height * max_width / width).max(u64(1));
        width  = max_width;
    } else if (max_height > u64() && height > max_height) {
        width  = (width * max_height / height).max(u64(1));
        height = max_height;
    }
    int w = static_cast<int>(width.to_primitive()), h = static_cast<int>(height.to_primitive());
    const bool premultiplied_resize =
        (w != sw || h != sh) && ffi::ffmpeg::image_has_alpha(s.frame.get());
    int converted_w = premultiplied_resize ? sw : w;
    int converted_h = premultiplied_resize ? sh : h;
    // swscale's SIMD output needs aligned rows and tail padding, even for RGBA.
    const auto row_bytes        = u64(converted_w) * u64(4);
    const auto pitch            = (row_bytes + u64(63)) & ~u64(63);
    const auto allocation_bytes = pitch * u64(converted_h) + u64(64);
    if (pitch > u64(0x7fffffff) || allocation_bytes > u64(usize::MAX.to_primitive()))
        return Err(Error::ResourceLimit);
    auto pixels = Pixels::allocate(s.budget.clone(), usize(allocation_bytes.to_primitive()));
    if (pixels.is_err()) return Err(pixels.unwrap_err_unchecked());
    const auto stride = static_cast<int>(pitch.to_primitive());
    auto       converted =
        s.scaler.convert(*s.frame.get(), converted_w, converted_h, pixels->data_mut(), stride);
    if (converted.is_err()) return Err(converted.unwrap_err_unchecked());
    // Compact forward while premultiplying; no second full-size output allocation.
    for (int y = 0; y < converted_h; ++y) {
        const auto* input  = pixels->data() + rstd::size_t(y) * stride;
        auto*       output = pixels->data_mut() + rstd::size_t(y) * row_bytes.to_primitive();
        for (int x = 0; x < converted_w; ++x) {
            const auto alpha = input[3];
            for (int channel = 0; channel < 3; ++channel)
                output[channel] =
                    static_cast<rstd::uint8_t>((unsigned(input[channel]) * alpha + 127) / 255);
            output[3] = alpha;
            input += 4;
            output += 4;
        }
    }
    if (premultiplied_resize) {
        // Filter associated color; transparent RGB must not bleed into visible edges.
        auto scaled = Pixels::allocate(s.budget.clone(), usize(w) * usize(h) * usize(4));
        if (scaled.is_err()) return Err(scaled.unwrap_err_unchecked());
        resize_associated_rgba(*pixels, sw, sh, *scaled, w, h);
        pixels = rstd::move(scaled);
    }
    double duration = static_cast<double>(ffi::ffmpeg::image_frame_duration(s.frame.get())) *
                      ffi::ffmpeg::av_q2d(s.codec->pkt_timebase) * 1000000.0;
    if (! (duration >= 1 && duration < static_cast<double>(u64::MAX.to_primitive())))
        duration = 100000;
    auto frame = DecodedImage { pixels.unwrap_unchecked(),
                                u32(w),
                                u32(h),
                                u32(w * 4),
                                s.index++,
                                u64(static_cast<rstd::uint64_t>(duration)) };
    av_frame_unref(s.frame.get());
    if (s.cancellation.cancelled()) return Err(Error::Cancelled);
    return Ok(Some(rstd::move(frame)));
}
} // namespace wavsen::image
