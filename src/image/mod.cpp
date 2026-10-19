module;
#include <rstd/macro.hpp>
module wavsen.image;
import wavsen.ffi.ffmpeg.base;
import wavsen.ffi.ffmpeg.base.owner;
import wavsen.ffi.ffmpeg.image;
#if LITO_FEAT_JXL
import wavsen.ffi.jxl;
#endif

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
struct Impl<io::ReadAt, wavsen::image::MemoryReader>
    : DefaultInImpl<io::ReadAt, wavsen::image::MemoryReader> {
    auto read_buf_at(io::BorrowedCursor target, u64 offset) const -> io::Result<empty> {
        const auto& pixels = this->self().pixels;
        if (offset >= u64(pixels.size().to_primitive())) return Ok(empty {});
        auto count = target.capacity().min(pixels.size() - usize(offset.to_primitive()));
        target.append(slice<u8>::from_raw_parts(
            reinterpret_cast<const byte*>(pixels.data()) + offset.to_primitive(), count));
        return Ok(empty {});
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
    bool still_complete {};
    auto next_frame() -> Result<bool, Error> {
        if (still_complete) return Ok(false);
        auto& s = *this;
        av_frame_unref(s.frame.get());
        for (;;) {
            if (s.stopped()) return Err(s.failure());
            auto rc = avcodec_receive_frame(s.codec.get(), s.frame.get());
            if (rc == 0) break;
            if (rc == AVERROR_EOF) {
                s.metadata.frame_count = s.index;
                return Ok(false);
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
        return Ok(true);
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
                    "webp_pipe,webp,webp_anim,gif,apng,ico,image2pipe,jpegxl_pipe,jpegxl_anim",
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
            return Err(stopped() ? failure() : Error::Unsupported);
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
        auto* params = raw->streams[stream]->codecpar;
#if ! LITO_FEAT_JXL
        if (params->codec_id == AV_CODEC_ID_JPEGXL) return Err(Error::Unsupported);
#endif
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

auto ImageSession::prepare_still() -> Result<StillImage, Error> {
    auto& s = *impl_;
    if (s.index != u64()) return Err(Error::InvalidInput);
    s.started     = rstd::time::Instant::now();
    auto encoding = ffi::ffmpeg::image_encoding(s.codec->codec_id);
    if (encoding == Encoding::Other) return Err(Error::Unsupported);
    ffi::ffmpeg::image_strict_decode(s.codec.get());
    auto ready = s.next_frame();
    if (ready.is_err()) return Err(ready.unwrap_err_unchecked());
    if (! *ready) return Err(Error::Decode);
    auto image =
        ffi::ffmpeg::prepare_still(*s.frame.get(), s.codec->codec_id, s.budget.clone(), s.limits);
    if (image.is_err()) return Err(image.unwrap_err_unchecked());
    if (encoding == Encoding::Jpeg || encoding == Encoding::Jxl)
        image->encoded_source = Some(s.source.clone());
    s.index = u64(1);
    // TODO: Validate source completeness beyond FFmpeg's decoder error checks.
    auto extra = s.next_frame();
    if (extra.is_err()) return Err(extra.unwrap_err_unchecked());
    if (*extra) return Err(Error::Unsupported);
    if (s.stopped()) return Err(s.failure());
    s.still_complete       = true;
    s.metadata.frame_count = u64(1);
    return image;
}

auto capabilities() -> Capabilities {
    auto result = ffi::ffmpeg::image_capabilities();
#if LITO_FEAT_JXL
    result.jxl_version         = u32(wavsen_jxl_version());
    result.jpeg_reconstruction = true;
#endif
    return result;
}

#if LITO_FEAT_JXL
auto recompress_jpeg(const Source& source, u32 effort, Budget budget, const Cancellation& cancel,
                     Limits limits) -> Result<Pixels, Error> {
    return ffi::jxl::recompress(source, effort, rstd::move(budget), cancel, limits);
}

auto reconstruct_jpeg(const Pixels& input, Budget budget, const Cancellation& cancel, Limits limits)
    -> Result<Pixels, Error> {
    return ffi::jxl::reconstruct(input, rstd::move(budget), cancel, limits);
}

auto verify_jpeg(const Source& source, const Pixels& encoded, Budget budget,
                 const Cancellation& cancel, Limits limits) -> Result<empty, Error> {
    auto restored = reconstruct_jpeg(encoded, rstd::move(budget), cancel, limits);
    if (restored.is_err()) return Err(restored.unwrap_err_unchecked());
    if (source.size() != u64(restored->size().to_primitive())) return Err(Error::Encode);
    auto             input = source.reader();
    array<u8, 65536> buffer {};
    usize            position {};
    while (position < restored->size()) {
        if (cancel.cancelled()) return Err(Error::Cancelled);
        auto count = input.read(buffer.as_mut_slice());
        if (count.is_err()) return Err(Error::Io);
        if (*count == usize() || *count > restored->size() - position ||
            rstd::mem::memcmp(buffer.as_slice().as_raw_ptr(),
                              restored->data() + position.to_primitive(),
                              *count) != 0)
            return Err(Error::Encode);
        position += *count;
    }
    return Ok(empty {});
}

auto archive(const StillImage& input, Budget budget, const Cancellation& cancel, Limits limits)
    -> Result<MasterImage, Error> {
    if (cancel.cancelled()) return Err(Error::Cancelled);
    if (input.descriptor.encoding == Encoding::Jpeg) {
        if (input.encoded_source.is_none()) return Err(Error::InvalidInput);
        auto bytes = rstd_try(
            recompress_jpeg(*input.encoded_source, u32(3), budget.clone(), cancel, limits));
        rstd_try(verify_jpeg(*input.encoded_source, bytes, budget.clone(), cancel, limits));
        return Ok(MasterImage { rstd::move(bytes), Fidelity::JpegReconstruction });
    }
    if (input.descriptor.encoding != Encoding::Png && input.descriptor.encoding != Encoding::Webp)
        return Err(Error::Unsupported);
    auto bytes     = rstd_try(encode(input, {}, budget.clone(), cancel, limits));
    auto exif      = input.source_exif.is_some() && input.descriptor.orientation == u32(1)
                         ? Ok(input.source_exif->clone())
                         : exif_snapshot(input, budget.clone());
    auto payload   = rstd_try(rstd::move(exif));
    auto container = rstd_try(JxlContainer::open(rstd::move(bytes), limits));
    auto view      = [](const Pixels& value) {
        return slice<u8>::from_raw_parts(reinterpret_cast<const rstd::byte*>(value.data()),
                                         value.size());
    };
    auto output = rstd_try(container.rewrite_metadata(
        { Some(view(payload)),
          input.source_xmp.is_some() ? Some(view(*input.source_xmp)) : None() },
        budget.clone(),
        cancel));
    rstd_try(verify_lossless(input, output, budget.clone(), cancel, limits));
    auto checked  = rstd_try(JxlContainer::open(output.clone(), limits));
    auto metadata = rstd_try(checked.metadata(budget.clone()));
    if (metadata.exif.is_none() || metadata.exif->size() != payload.size() ||
        rstd::mem::memcmp(metadata.exif->data(), payload.data(), payload.size()) != 0)
        return Err(Error::Encode);
    if (input.source_xmp.is_some() &&
        (metadata.xmp.is_none() || metadata.xmp->size() != input.source_xmp->size() ||
         rstd::mem::memcmp(
             metadata.xmp->data(), input.source_xmp->data(), input.source_xmp->size()) != 0))
        return Err(Error::Encode);
    return Ok(MasterImage { rstd::move(output), Fidelity::SampleLossless });
}

#endif

auto encode(const StillImage& image, EncodeSpec spec, Budget budget,
            const Cancellation& cancellation, Limits limits) -> Result<Pixels, Error> {
    if (cancellation.cancelled()) return Err(Error::Cancelled);
    auto started = rstd::time::Instant::now();
    auto result  = ffi::ffmpeg::encode_still(image, spec, rstd::move(budget), limits);
    if (cancellation.cancelled()) return Err(Error::Cancelled);
    if (started.elapsed().as_millis() > limits.operation_ms) return Err(Error::ResourceLimit);
    return result;
}

auto ImageSession::next(DecodeRequest request) -> Result<Option<DecodedImage>, Error> {
    auto& s   = *impl_;
    s.started = rstd::time::Instant::now();
    if (s.cancellation.cancelled()) return Err(Error::Cancelled);
    auto ready = s.next_frame();
    if (ready.is_err()) return Err(ready.unwrap_err_unchecked());
    if (! *ready) return Ok(Option<DecodedImage> {});
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

namespace wavsen::image
{
namespace
{
#if LITO_FEAT_JXL
constexpr rstd::uint32_t box_type(char a, char b, char c, char d) {
    return (rstd::uint32_t(a) << 24) | (rstd::uint32_t(b) << 16) | (rstd::uint32_t(c) << 8) |
           rstd::uint32_t(d);
}
constexpr auto signature_box  = box_type('J', 'X', 'L', ' ');
constexpr auto ftyp_box       = box_type('f', 't', 'y', 'p');
constexpr auto code_box       = box_type('j', 'x', 'l', 'c');
constexpr auto part_box       = box_type('j', 'x', 'l', 'p');
constexpr auto exif_box       = box_type('E', 'x', 'i', 'f');
constexpr auto xmp_box        = box_type('x', 'm', 'l', ' ');
constexpr auto compressed_box = box_type('b', 'r', 'o', 'b');
constexpr auto jpeg_box       = box_type('j', 'b', 'r', 'd');
constexpr auto uuid_box       = box_type('u', 'u', 'i', 'd');
auto           read_be(const rstd::uint8_t* p, unsigned bytes) -> rstd::uint64_t {
    rstd::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value = (value << 8) | p[i];
    return value;
}
#endif
void write_be(rstd::uint8_t* p, rstd::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) p[bytes - i - 1] = (value >> (8 * i)) & 255;
}
} // namespace

#if LITO_FEAT_JXL
struct JxlContainer::Impl {
    struct BoxRange {
        usize          start, size, payload;
        rstd::uint32_t type, actual_type;
    };
    Pixels        bytes;
    Limits        limits;
    Vec<BoxRange> boxes = Vec<BoxRange>::make();
    bool          bare {}, reconstruction {};
    usize         codestream_size;
    explicit Impl(Pixels p, Limits l): bytes(rstd::move(p)), limits(l) {}

    template<typename Omit, typename Emit>
    auto repack(u64 extra, Omit omitted, Emit emit, Budget budget, const Cancellation& cancel) const
        -> Result<Pixels, Error> {
        const auto& s      = *this;
        u64         length = u64(s.bytes.size().to_primitive()) + extra;
        if (s.bare)
            length += u64(40);
        else
            for (const auto& b : s.boxes)
                if (omitted(b)) length -= u64(b.size.to_primitive());
        if (length > s.limits.output_bytes || length > u64(0xffffffff))
            return Err(Error::ResourceLimit);
        auto result = Pixels::allocate(rstd::move(budget), usize(length.to_primitive()));
        if (result.is_err()) return Err(result.unwrap_err_unchecked());
        auto* out = result->data_mut();
        usize cursor {};
        if (s.bare) {
            write_be(out, 12, 4);
            write_be(out + 4, signature_box, 4);
            write_be(out + 8, 0x0d0a870a, 4);
            write_be(out + 12, 20, 4);
            write_be(out + 16, ftyp_box, 4);
            write_be(out + 20, box_type('j', 'x', 'l', ' '), 4);
            write_be(out + 24, 0, 4);
            write_be(out + 28, box_type('j', 'x', 'l', ' '), 4);
            cursor = usize(32);
            emit(out, cursor);
            write_be(out + cursor.to_primitive(), s.bytes.size().to_primitive() + 8, 4);
            write_be(out + cursor.to_primitive() + 4, code_box, 4);
            cursor += usize(8);
            rstd::mem::memcpy(out + cursor.to_primitive(), s.bytes.data(), s.bytes.size());
            cursor += s.bytes.size();
        } else {
            for (usize i {}; i < s.boxes.len(); ++i) {
                if (cancel.cancelled()) return Err(Error::Cancelled);
                const auto& b = s.boxes[i];
                if (! omitted(b)) {
                    rstd::mem::memcpy(out + cursor.to_primitive(),
                                      s.bytes.data() + b.start.to_primitive(),
                                      b.size);
                    cursor += b.size;
                }
                if (i == usize(1)) emit(out, cursor);
            }
        }
        if (cancel.cancelled()) return Err(Error::Cancelled);
        if (cursor != result->size()) return Err(Error::Encode);
        return result;
    }
};
JxlContainer::JxlContainer(Box<Impl> impl): impl_(rstd::move(impl)) {}
JxlContainer::JxlContainer(JxlContainer&&) noexcept                    = default;
auto JxlContainer::operator=(JxlContainer&&) noexcept -> JxlContainer& = default;
JxlContainer::~JxlContainer()                                          = default;

auto JxlContainer::open(Pixels bytes, Limits limits) -> Result<JxlContainer, Error> {
    if (bytes.size() < usize(2) || u64(bytes.size().to_primitive()) > limits.encoded_bytes)
        return Err(Error::ResourceLimit);
    auto box = Box<Impl>::try_make(rstd::move(bytes), limits);
    if (box.is_err()) return Err(Error::ResourceLimit);
    auto&       s    = **box;
    const auto* data = s.bytes.data();
    if (data[0] == 0xff && data[1] == 0x0a) {
        s.bare            = true;
        s.codestream_size = s.bytes.size();
        return Ok(JxlContainer(box.unwrap_unchecked()));
    }
    if (s.bytes.size() < usize(28) || read_be(data, 4) != 12 ||
        read_be(data + 4, 4) != signature_box || read_be(data + 8, 4) != 0x0d0a870a)
        return Err(Error::InvalidInput);
    usize          offset {};
    bool           complete = false, partial = false, whole = false;
    rstd::uint32_t next_part = 0;
    while (offset < s.bytes.size()) {
        if (s.boxes.len() >= usize(4096) || s.bytes.size() - offset < usize(8))
            return Err(Error::ResourceLimit);
        const auto* p      = data + offset.to_primitive();
        auto        length = read_be(p, 4);
        auto        type   = rstd::uint32_t(read_be(p + 4, 4));
        usize       header(8);
        if (length == 1) {
            if (s.bytes.size() - offset < usize(16)) return Err(Error::InvalidInput);
            length = read_be(p + 8, 8);
            header = usize(16);
        } else if (length == 0)
            length = (s.bytes.size() - offset).to_primitive();
        if (length < header.to_primitive() || length > (s.bytes.size() - offset).to_primitive())
            return Err(Error::InvalidInput);
        auto count        = usize(length);
        auto payload_size = count - header;
        auto actual_type  = type;
        if (s.boxes.len() == usize(1) &&
            (type != ftyp_box || payload_size < usize(8) ||
             read_be(p + header.to_primitive(), 4) != box_type('j', 'x', 'l', ' ')))
            return Err(Error::Unsupported);
        if (type == compressed_box) {
            if (payload_size < usize(4)) return Err(Error::InvalidInput);
            actual_type = rstd::uint32_t(read_be(p + header.to_primitive(), 4));
        }
        if (type == code_box) {
            if (partial || whole || payload_size == usize()) return Err(Error::InvalidInput);
            whole    = true;
            complete = true;
            s.codestream_size += payload_size;
        } else if (type == part_box) {
            if (whole || complete || payload_size <= usize(4)) return Err(Error::InvalidInput);
            auto number = rstd::uint32_t(read_be(p + header.to_primitive(), 4));
            if ((number & 0x7fffffff) != next_part++) return Err(Error::Unsupported);
            partial  = true;
            complete = (number & 0x80000000) != 0;
            s.codestream_size += payload_size - usize(4);
        }
        if (type == uuid_box && payload_size < usize(16)) return Err(Error::InvalidInput);
        if (actual_type == jpeg_box) s.reconstruction = true;
        s.boxes.push({ offset, count, header, type, actual_type });
        offset += count;
    }
    if (! complete || s.boxes.len() < usize(3)) return Err(Error::InvalidInput);
    return Ok(JxlContainer(box.unwrap_unchecked()));
}

auto JxlContainer::codestream(Budget budget) const -> Result<Pixels, Error> {
    const auto& s = *impl_;
    if (s.bare) return Ok(s.bytes.clone());
    auto result = Pixels::allocate(rstd::move(budget), s.codestream_size);
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    usize cursor {};
    for (const auto& b : s.boxes) {
        if (b.type != code_box && b.type != part_box) continue;
        auto prefix = b.payload + usize(b.type == part_box ? 4 : 0);
        auto count  = b.size - prefix;
        rstd::mem::memcpy(result->data_mut() + cursor.to_primitive(),
                          s.bytes.data() + (b.start + prefix).to_primitive(),
                          count);
        cursor += count;
    }
    return result;
}

auto JxlContainer::metadata(Budget budget) const -> Result<MetadataSnapshot, Error> {
    const auto&      s = *impl_;
    MetadataSnapshot result;
    u64              total {};
    for (const auto& box : s.boxes) {
        if (box.actual_type != exif_box && box.actual_type != xmp_box) continue;
        if (box.type == compressed_box) return Err(Error::Unsupported);
        auto& target = box.type == exif_box ? result.exif : result.xmp;
        if (target.is_some()) return Err(Error::Unsupported);
        auto offset = box.start + box.payload;
        auto count  = box.size - box.payload;
        if (box.type == exif_box) {
            if (count < usize(4)) return Err(Error::InvalidInput);
            auto skip = usize(read_be(s.bytes.data() + offset.to_primitive(), 4));
            if (skip > count - usize(4)) return Err(Error::InvalidInput);
            offset += skip + usize(4);
            count -= skip + usize(4);
        }
        total += u64(count.to_primitive());
        if (total > s.limits.metadata_bytes) return Err(Error::ResourceLimit);
        auto data = Pixels::allocate(budget.clone(), count);
        if (data.is_err()) return Err(data.unwrap_err_unchecked());
        rstd::mem::memcpy(data->data_mut(), s.bytes.data() + offset.to_primitive(), count);
        target = Some(data.unwrap_unchecked());
    }
    return Ok(rstd::move(result));
}

auto JxlContainer::rewrite_metadata(MetadataPatch patch, Budget budget,
                                    const Cancellation& cancel) const -> Result<Pixels, Error> {
    const auto& s = *impl_;
    if (cancel.cancelled()) return Err(Error::Cancelled);
    if (patch.exif.is_none() && patch.xmp.is_none()) return Ok(s.bytes.clone());
    if (s.reconstruction) return Err(Error::Unsupported);
    auto omitted = [&](const Impl::BoxRange& b) {
        return (patch.exif.is_some() && b.actual_type == exif_box) ||
               (patch.xmp.is_some() && b.actual_type == xmp_box);
    };
    u64 extra {};
    if (patch.exif.is_some() && ! patch.exif->is_empty())
        extra += u64(patch.exif->len().to_primitive()) + u64(12);
    if (patch.xmp.is_some() && ! patch.xmp->is_empty())
        extra += u64(patch.xmp->len().to_primitive()) + u64(8);
    if (extra > s.limits.metadata_bytes) return Err(Error::ResourceLimit);
    auto metadata = [&](rstd::uint8_t* out, usize& cursor) {
        auto emit = [&](rstd::uint32_t type, slice<u8> payload, unsigned prefix) {
            auto* p = out + cursor.to_primitive();
            write_be(p, payload.len().to_primitive() + 8 + prefix, 4);
            write_be(p + 4, type, 4);
            if (prefix) write_be(p + 8, 0, 4);
            rstd::mem::memcpy(p + 8 + prefix, payload.as_raw_ptr(), payload.len());
            cursor += payload.len() + usize(8 + prefix);
        };
        if (patch.exif.is_some() && ! patch.exif->is_empty()) emit(exif_box, *patch.exif, 4);
        if (patch.xmp.is_some() && ! patch.xmp->is_empty()) emit(xmp_box, *patch.xmp, 0);
    };
    return s.repack(extra, omitted, metadata, rstd::move(budget), cancel);
}
auto JxlContainer::encoded() const -> Pixels { return impl_->bytes.clone(); }
auto JxlContainer::info() const -> ContainerInfo {
    return { impl_->bare, impl_->reconstruction, impl_->boxes.len() };
}
auto JxlContainer::extension(ExtensionId id, Budget budget) const -> Result<Option<Pixels>, Error> {
    const auto&    s = *impl_;
    Option<Pixels> result;
    for (const auto& b : s.boxes) {
        if (b.actual_type != uuid_box) continue;
        if (b.type == compressed_box) return Err(Error::Unsupported);
        const auto* p = s.bytes.data() + (b.start + b.payload).to_primitive();
        if (rstd::mem::memcmp(p, id.bytes, usize(16)) != 0) continue;
        if (result.is_some()) return Err(Error::InvalidInput);
        auto count = b.size - b.payload - usize(16);
        if (u64(count.to_primitive()) > s.limits.metadata_bytes) return Err(Error::ResourceLimit);
        auto data = Pixels::allocate(budget.clone(), count);
        if (data.is_err()) return Err(data.unwrap_err_unchecked());
        rstd::mem::memcpy(data->data_mut(), p + 16, count);
        result = Some(data.unwrap_unchecked());
    }
    return Ok(rstd::move(result));
}
auto JxlContainer::rewrite_extension(ExtensionId id, slice<u8> payload, Budget budget,
                                     const Cancellation& cancel) const -> Result<Pixels, Error> {
    const auto& s = *impl_;
    if (cancel.cancelled()) return Err(Error::Cancelled);
    auto existing = extension(id, budget.clone());
    if (existing.is_err()) return Err(existing.unwrap_err_unchecked());
    auto omitted = [&](const Impl::BoxRange& b) {
        return b.type == uuid_box &&
               rstd::mem::memcmp(
                   s.bytes.data() + (b.start + b.payload).to_primitive(), id.bytes, usize(16)) == 0;
    };
    u64 extra = payload.is_empty() ? u64() : u64(payload.len().to_primitive()) + u64(24);
    u64 total = extra;
    for (const auto& b : s.boxes) {
        if (! omitted(b) && b.type != signature_box && b.type != ftyp_box && b.type != code_box &&
            b.type != part_box && b.actual_type != jpeg_box)
            total += u64((b.size - b.payload).to_primitive());
    }
    if (total > s.limits.metadata_bytes) return Err(Error::ResourceLimit);
    if (payload.is_empty() && existing->is_none()) return Ok(s.bytes.clone());
    auto emit = [&](rstd::uint8_t* out, usize& cursor) {
        if (payload.is_empty()) return;
        auto* p = out + cursor.to_primitive();
        write_be(p, extra.to_primitive(), 4);
        write_be(p + 4, uuid_box, 4);
        rstd::mem::memcpy(p + 8, id.bytes, usize(16));
        rstd::mem::memcpy(p + 24, payload.as_raw_ptr(), payload.len());
        cursor += usize(extra.to_primitive());
    };
    return s.repack(extra, omitted, emit, rstd::move(budget), cancel);
}

#endif
} // namespace wavsen::image

namespace wavsen::image
{
auto exif_snapshot(const StillImage& image, Budget budget) -> Result<Pixels, Error> {
    auto output = Pixels::allocate(rstd::move(budget), usize(50));
    if (output.is_err()) return Err(output.unwrap_err_unchecked());
    auto* p = output->data_mut();
    p[0]    = 'M';
    p[1]    = 'M';
    write_be(p + 2, 42, 2);
    write_be(p + 4, 8, 4);
    write_be(p + 8, 3, 2);
    auto field = [&](unsigned index, unsigned tag, unsigned type, unsigned value) {
        auto* entry = p + 10 + index * 12;
        write_be(entry, tag, 2);
        write_be(entry + 2, type, 2);
        write_be(entry + 4, 1, 4);
        write_be(entry + 8, value, type == 3 ? 2 : 4);
    };
    const auto& d    = image.descriptor;
    bool        swap = d.orientation >= u32(5);
    field(0, 256, 4, (swap ? d.height : d.width).to_primitive());
    field(1, 257, 4, (swap ? d.width : d.height).to_primitive());
    field(2, 274, 3, 1);
    return output;
}
} // namespace wavsen::image

namespace wavsen::image
{
auto preview(const StillImage& input, u32 max_width, u32 max_height, Budget budget,
             const Cancellation& cancel, Limits limits) -> Result<StillImage, Error> {
    if (cancel.cancelled()) return Err(Error::Cancelled);
    if (! max_width.to_primitive() || ! max_height.to_primitive() || max_width > u32(16384) ||
        max_height > u32(16384))
        return Err(Error::InvalidInput);
    const auto& d = input.descriptor;
    if (d.orientation >= u32(5)) rstd::swap(max_width, max_height);
    unsigned w = d.width.to_primitive(), h = d.height.to_primitive();
    if (! w || ! h) return Err(Error::InvalidInput);
    double   factor = rstd::cmp::min(1.0,
                                     rstd::cmp::min(double(max_width.to_primitive()) / w,
                                                    double(max_height.to_primitive()) / h));
    unsigned dw     = rstd::cmp::max(1U, unsigned(w * factor));
    unsigned dh     = rstd::cmp::max(1U, unsigned(h * factor));
    auto     source = ffi::ffmpeg::preview_pixels(input, budget.clone(), limits);
    if (source.is_err()) return Err(source.unwrap_err_unchecked());
    auto pixels = Pixels::allocate(budget.clone(), usize(dw) * usize(dh) * usize(4));
    if (pixels.is_err()) return Err(pixels.unwrap_err_unchecked());
    resize_associated_rgba(*source, w, h, *pixels, dw, dh);
    for (usize i {}; i < pixels->size(); i += usize(4)) {
        auto* p = pixels->data_mut() + i.to_primitive();
        if (p[3])
            for (unsigned c = 0; c < 3; ++c)
                p[c] = rstd::cmp::min(255U, (unsigned(p[c]) * 255 + p[3] / 2) / p[3]);
    }
    if (cancel.cancelled()) return Err(Error::Cancelled);
    auto     format   = PixelFormat::Rgba8;
    unsigned channels = 4;
    if (static_cast<unsigned>(d.pixels) % 4 < 2) {
        channels = d.alpha ? 2 : 1;
        format   = d.alpha ? PixelFormat::GrayAlpha8 : PixelFormat::Gray8;
        for (unsigned i = 0; i < dw * dh; ++i) {
            auto* p    = pixels->data_mut();
            auto  gray = p[i * 4], alpha = p[i * 4 + 3];
            p[i * channels] = gray;
            if (d.alpha) p[i * channels + 1] = alpha;
        }
    } else if (! d.alpha) {
        channels = 3;
        format   = PixelFormat::Rgb8;
        for (unsigned i = 0; i < dw * dh; ++i) {
            auto* p = pixels->data_mut();
            auto  r = p[i * 4], g = p[i * 4 + 1], b = p[i * 4 + 2];
            p[i * 3]     = r;
            p[i * 3 + 1] = g;
            p[i * 3 + 2] = b;
        }
    }
    return Ok(StillImage { pixels.unwrap_unchecked(),
                           u32(dw * channels),
                           { u32(dw), u32(dh), format, Encoding::Png, d.orientation, d.alpha },
                           input.icc.is_some() ? Some(input.icc->clone()) : None() });
}
} // namespace wavsen::image

namespace wavsen::image
{
#if LITO_FEAT_JXL
auto verify_lossless(const StillImage& input, const Pixels& encoded, Budget budget,
                     const Cancellation& cancel, Limits limits) -> Result<empty, Error> {
    limits.encoded_bytes = limits.output_bytes;
    auto source          = Source::memory(encoded.clone(), "verify.jxl"_str);
    if (source.is_err()) return Err(source.unwrap_err_unchecked());
    auto session =
        ImageSession::open(source.unwrap_unchecked(), budget.clone(), cancel.clone(), limits);
    if (session.is_err()) return Err(session.unwrap_err_unchecked());
    auto decoded = session->prepare_still();
    if (decoded.is_err()) return Err(decoded.unwrap_err_unchecked());
    const auto& d = input.descriptor;
    unsigned    o = d.orientation.to_primitive(), w = d.width.to_primitive(),
                h = d.height.to_primitive();
    if (o < 1 || o > 8) return Err(Error::InvalidInput);
    unsigned width = o >= 5 ? h : w, height = o >= 5 ? w : h;
    if (decoded->descriptor.width != u32(width) || decoded->descriptor.height != u32(height) ||
        decoded->descriptor.pixels != d.pixels)
        return Err(Error::Encode);
    unsigned kind = static_cast<unsigned>(d.pixels);
    if (kind >= 8) return Err(Error::InvalidInput);
    unsigned count = (kind % 4 + 1) * (kind < 4 ? 1 : 2);
    if (u64(w) * u64(count) > u64(input.stride.to_primitive()) ||
        u64(input.stride.to_primitive()) * u64(h) > u64(input.pixels.size().to_primitive()))
        return Err(Error::InvalidInput);
    for (unsigned y = 0; y < height; ++y) {
        if (cancel.cancelled()) return Err(Error::Cancelled);
        for (unsigned x = 0; x < width; ++x) {
            unsigned sx = x, sy = y;
            switch (o) {
            case 2: sx = w - 1 - x; break;
            case 3:
                sx = w - 1 - x;
                sy = h - 1 - y;
                break;
            case 4: sy = h - 1 - y; break;
            case 5:
                sx = y;
                sy = x;
                break;
            case 6:
                sx = y;
                sy = h - 1 - x;
                break;
            case 7:
                sx = w - 1 - y;
                sy = h - 1 - x;
                break;
            case 8:
                sx = w - 1 - y;
                sy = x;
                break;
            }
            const auto* a = input.pixels.data() + sy * input.stride.to_primitive() + sx * count;
            const auto* b = decoded->pixels.data() + y * decoded->stride.to_primitive() + x * count;
            if (rstd::mem::memcmp(a, b, usize(count)) != 0) return Err(Error::Encode);
        }
    }
    if (input.icc.is_some() &&
        (decoded->icc.is_none() || input.icc->size() != decoded->icc->size() ||
         rstd::mem::memcmp(input.icc->data(), decoded->icc->data(), input.icc->size()) != 0))
        return Err(Error::Encode);
    return Ok(empty {});
}
#endif
} // namespace wavsen::image
