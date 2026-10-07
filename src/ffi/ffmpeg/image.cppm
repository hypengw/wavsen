module;
extern "C" {
#include <errno.h>
#include <stdlib.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/buffer.h>
#include <libavutil/pixdesc.h>
#include <libavutil/version.h>
#include <libswscale/swscale.h>
}

export module wavsen.ffi.ffmpeg.image;
import wavsen.image;
import rstd;
import wavsen.ffi.ffmpeg.base.owner;

export {
    using ::av_find_input_format;
    using ::av_probe_input_buffer2;
    using ::AVInputFormat;
}

using namespace rstd::prelude;

namespace wavsen::ffi::ffmpeg
{

export class ImageScaler {
public:
    ImageScaler()                                      = default;
    ImageScaler(const ImageScaler&)                    = delete;
    auto operator=(const ImageScaler&) -> ImageScaler& = delete;
    ~ImageScaler() { sws_freeContext(context_); }

    auto convert(const AVFrame& frame, int width, int height, uint8_t* output, int stride,
                 AVPixelFormat destination = AV_PIX_FMT_RGBA) -> Result<empty, image::Error> {
        auto format = static_cast<AVPixelFormat>(frame.format);
        bool jpeg   = true;
        switch (format) {
        case AV_PIX_FMT_YUVJ420P: format = AV_PIX_FMT_YUV420P; break;
        case AV_PIX_FMT_YUVJ422P: format = AV_PIX_FMT_YUV422P; break;
        case AV_PIX_FMT_YUVJ444P: format = AV_PIX_FMT_YUV444P; break;
        case AV_PIX_FMT_YUVJ440P: format = AV_PIX_FMT_YUV440P; break;
        case AV_PIX_FMT_YUVJ411P: format = AV_PIX_FMT_YUV411P; break;
        default: jpeg = false; break;
        }
        context_ = sws_getCachedContext(context_,
                                        frame.width,
                                        frame.height,
                                        format,
                                        width,
                                        height,
                                        destination,
                                        SWS_BICUBIC,
                                        nullptr,
                                        nullptr,
                                        nullptr);
        if (! context_) return Err(image::Error::Unsupported);

        int colorspace = SWS_CS_DEFAULT;
        switch (frame.colorspace) {
        case AVCOL_SPC_BT709: colorspace = SWS_CS_ITU709; break;
        case AVCOL_SPC_FCC: colorspace = SWS_CS_FCC; break;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M: colorspace = SWS_CS_ITU601; break;
        case AVCOL_SPC_SMPTE240M: colorspace = SWS_CS_SMPTE240M; break;
        case AVCOL_SPC_BT2020_NCL: colorspace = SWS_CS_BT2020; break;
        default: break;
        }
        const auto* descriptor         = av_pix_fmt_desc_get(format);
        const bool  default_full_range = jpeg || (descriptor && descriptor->nb_components < 3);
        const int full_range = frame.color_range == AVCOL_RANGE_JPEG ||
                               (frame.color_range == AVCOL_RANGE_UNSPECIFIED && default_full_range);
        const auto* coefficients = sws_getCoefficients(colorspace);
        if (sws_setColorspaceDetails(
                context_, coefficients, full_range, coefficients, 1, 0, 1 << 16, 1 << 16) < 0)
            return Err(image::Error::Unsupported);
        uint8_t* data[4] { output, nullptr, nullptr, nullptr };
        int      strides[4] { stride, 0, 0, 0 };
        if (sws_scale(context_, frame.data, frame.linesize, 0, frame.height, data, strides) !=
            height)
            return Err(image::Error::Decode);
        return Ok(empty {});
    }

private:
    SwsContext* context_ {};
};

export bool image_has_alpha(const AVFrame* frame) {
    const auto* format = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    return format && (format->flags & AV_PIX_FMT_FLAG_ALPHA);
}

export int64_t image_frame_duration(const AVFrame* frame) {
#if LIBAVUTIL_VERSION_MAJOR >= 58
    return frame->duration;
#else
    return frame->pkt_duration;
#endif
}

void release_image_pixels(void* opaque, uint8_t*) {
    auto raw   = rstd::sync::ArcRaw<image::Pixels>::from_raw(opaque);
    auto owner = rstd::sync::Arc<image::Pixels>::from_raw(raw);
}

export int image_get_buffer(AVCodecContext* codec, AVFrame* frame, int flags) {
    if (! (codec->codec->capabilities & AV_CODEC_CAP_DR1))
        return avcodec_default_get_buffer2(codec, frame, flags);
    auto& budget = *static_cast<image::Budget*>(codec->opaque);
    int   width = frame->width, height = frame->height;
    int   align[AV_NUM_DATA_POINTERS] {};
    avcodec_align_dimensions2(codec, &width, &height, align);
    auto format = static_cast<AVPixelFormat>(frame->format);
    int  bytes  = av_image_get_buffer_size(format, width, height, 64);
    if (bytes <= 0 || bytes > INT_MAX - 64) return AVERROR(EINVAL);
    for (auto required : align)
        if (required > 64) return AVERROR(EINVAL);
    auto memory = image::Pixels::allocate(budget.clone(), usize(bytes + 64));
    if (memory.is_err()) return AVERROR(ENOMEM);
    auto owner = rstd::sync::Arc<image::Pixels>::try_make(memory.unwrap_unchecked());
    if (owner.is_err()) return AVERROR(ENOMEM);
    auto  pixels  = owner.unwrap_unchecked();
    auto* data    = pixels->data_mut();
    auto  raw     = rstd::move(pixels).into_raw();
    auto* opaque  = raw.into_raw();
    frame->buf[0] = av_buffer_create(data, bytes + 64, release_image_pixels, opaque, 0);
    if (! frame->buf[0]) {
        release_image_pixels(opaque, data);
        return AVERROR(ENOMEM);
    }
    return av_image_fill_arrays(frame->data, frame->linesize, data, format, width, height, 64) < 0
               ? AVERROR(EINVAL)
               : 0;
}

export void image_strict_decode(AVCodecContext* codec) {
    codec->err_recognition = AV_EF_CRCCHECK | AV_EF_CAREFUL | AV_EF_EXPLODE;
}

export auto image_capabilities() -> image::Capabilities {
    return {
#if LITO_FEAT_JXL
        avcodec_find_decoder(AV_CODEC_ID_JPEGXL) != nullptr,
        avcodec_find_encoder_by_name("libjxl") != nullptr,
#else
        false,
        false,
#endif
        avcodec_find_encoder(AV_CODEC_ID_PNG) != nullptr,
        u32(avcodec_version())
    };
}

auto pixel_format(image::PixelFormat format) -> AVPixelFormat {
    using F = image::PixelFormat;
    switch (format) {
    case F::Gray8: return AV_PIX_FMT_GRAY8;
    case F::GrayAlpha8: return AV_PIX_FMT_YA8;
    case F::Rgb8: return AV_PIX_FMT_RGB24;
    case F::Rgba8: return AV_PIX_FMT_RGBA;
    case F::Gray16: return AV_PIX_FMT_GRAY16LE;
    case F::GrayAlpha16: return AV_PIX_FMT_YA16LE;
    case F::Rgb16: return AV_PIX_FMT_RGB48LE;
    case F::Rgba16: return AV_PIX_FMT_RGBA64LE;
    }
    return AV_PIX_FMT_NONE;
}

export auto image_encoding(AVCodecID codec) -> image::Encoding {
    using C = image::Encoding;
    return codec == AV_CODEC_ID_MJPEG    ? C::Jpeg
           : codec == AV_CODEC_ID_PNG    ? C::Png
           : codec == AV_CODEC_ID_WEBP   ? C::Webp
           : codec == AV_CODEC_ID_JPEGXL ? C::Jxl
                                         : C::Other;
}

export auto prepare_still(const AVFrame& frame, AVCodecID codec, image::Budget budget,
                          image::Limits limits) -> Result<image::StillImage, image::Error> {
    using E = image::Error;
    using F = image::PixelFormat;
    using C = image::Encoding;
    if (frame.width <= 0 || frame.height <= 0 ||
        u64(frame.width) * u64(frame.height) > limits.source_pixels)
        return Err(E::ResourceLimit);
    C encoding = image_encoding(codec);
    if (encoding == C::Other) return Err(E::Unsupported);
    const auto* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame.format));
    if (! desc || desc->flags & (AV_PIX_FMT_FLAG_FLOAT | AV_PIX_FMT_FLAG_HWACCEL) ||
        frame.color_trc == AVCOL_TRC_SMPTE2084 || frame.color_trc == AVCOL_TRC_ARIB_STD_B67)
        return Err(E::Unsupported);
    if (! av_frame_get_side_data(&frame, AV_FRAME_DATA_ICC_PROFILE) &&
        ((frame.color_primaries != AVCOL_PRI_UNSPECIFIED &&
          frame.color_primaries != AVCOL_PRI_BT709) ||
         (frame.color_trc != AVCOL_TRC_UNSPECIFIED && frame.color_trc != AVCOL_TRC_IEC61966_2_1)))
        return Err(E::Unsupported);
    unsigned depth = 0;
    for (int i = 0; i < desc->nb_components; ++i)
        if (unsigned(desc->comp[i].depth) > depth) depth = desc->comp[i].depth;
    if (! depth || depth > 16) return Err(E::Unsupported);
    bool      alpha     = desc->flags & AV_PIX_FMT_FLAG_ALPHA;
    bool      gray      = desc->nb_components < 3 && ! (desc->flags & AV_PIX_FMT_FLAG_PAL);
    unsigned  channels  = (gray ? 1 : 3) + (alpha ? 1 : 0);
    unsigned  component = depth > 8 ? 2 : 1;
    auto      format    = gray ? (alpha ? (component == 2 ? F::GrayAlpha16 : F::GrayAlpha8)
                                        : (component == 2 ? F::Gray16 : F::Gray8))
                               : (alpha ? (component == 2 ? F::Rgba16 : F::Rgba8)
                                        : (component == 2 ? F::Rgb16 : F::Rgb8));
    const u64 row       = u64(frame.width) * u64(channels * component);
    const u64 stride    = (row + u64(63)) & ~u64(63);
    const u64 count     = stride * u64(frame.height) + u64(64);
    if (stride > u64(INT_MAX) || count > u64(usize::MAX.to_primitive()))
        return Err(E::ResourceLimit);
    auto pixels = image::Pixels::allocate(budget.clone(), usize(count.to_primitive()));
    if (pixels.is_err()) return Err(pixels.unwrap_err_unchecked());
    ImageScaler scaler;
    auto        converted = scaler.convert(frame,
                                           frame.width,
                                           frame.height,
                                           pixels->data_mut(),
                                           int(stride.to_primitive()),
                                           pixel_format(format));
    if (converted.is_err()) return Err(converted.unwrap_err_unchecked());
    image::StillImage result {
        pixels.unwrap_unchecked(),
        u32(stride.to_primitive()),
        { u32(frame.width), u32(frame.height), format, encoding, u32(1), alpha },
        None()
    };
    if (auto* icc = av_frame_get_side_data(&frame, AV_FRAME_DATA_ICC_PROFILE)) {
        if (u64(icc->size) > limits.metadata_bytes) return Err(E::ResourceLimit);
        auto data = image::Pixels::allocate(budget.clone(), usize(icc->size));
        if (data.is_err()) return Err(data.unwrap_err_unchecked());
        rstd::mem::memcpy(data->data_mut(), icc->data, usize(icc->size));
        result.icc = Some(data.unwrap_unchecked());
    }
    AVDictionaryEntry* field = nullptr;
    u64                metadata_size {};
    // TODO: Preserve raw EXIF/XMP and opaque source chunks when FFmpeg exposes them.
    // FFmpeg 7.1 does not export PNG eXIf/iTXt or WebP XMP through this path.
    while ((field = av_dict_get(frame.metadata, "", field, AV_DICT_IGNORE_SUFFIX))) {
        auto name  = rstd::ffi::CStr::from_ptr(field->key).to_str();
        auto value = rstd::ffi::CStr::from_ptr(field->value).to_str();
        if (name.is_err() || value.is_err()) return Err(E::Unsupported);
        metadata_size += u64(name->len().to_primitive()) + u64(value->len().to_primitive());
        if (metadata_size > limits.metadata_bytes || result.metadata.len() >= usize(256))
            return Err(E::ResourceLimit);
        result.metadata.push({ String::make(*name), String::make(*value) });
    }
    if (auto* orientation = av_dict_get(frame.metadata, "Orientation", nullptr, 0)) {
        char* end   = nullptr;
        auto  value = strtol(orientation->value, &end, 10);
        if (! end || *end || value < 1 || value > 8) return Err(E::Unsupported);
        result.descriptor.orientation = u32(value);
    } else if (av_frame_get_side_data(&frame, AV_FRAME_DATA_DISPLAYMATRIX)) {
        // TODO: Support display matrices without an Orientation dictionary entry.
        return Err(E::Unsupported);
    }
    return Ok(rstd::move(result));
}

export auto preview_pixels(const image::StillImage& input, image::Budget budget,
                           image::Limits limits) -> Result<image::Pixels, image::Error> {
    const auto& d      = input.descriptor;
    auto        format = pixel_format(d.pixels);
    const auto* desc   = av_pix_fmt_desc_get(format);
    if (! desc || ! d.width.to_primitive() || ! d.height.to_primitive() ||
        d.width > u32(INT_MAX / 8) || d.height > u32(INT_MAX) ||
        u64(d.width.to_primitive()) * u64(d.height.to_primitive()) > limits.source_pixels ||
        u64(input.stride.to_primitive()) * u64(d.height.to_primitive()) >
            u64(input.pixels.size().to_primitive()) ||
        input.stride > u32(INT_MAX) ||
        input.stride < d.width * u32(av_get_bits_per_pixel(desc) / 8))
        return Err(image::Error::ResourceLimit);
    FramePtr frame(av_frame_alloc());
    if (! frame) return Err(image::Error::ResourceLimit);
    frame->width       = d.width.to_primitive();
    frame->height      = d.height.to_primitive();
    frame->format      = format;
    frame->color_range = AVCOL_RANGE_JPEG;
    frame->data[0]     = const_cast<uint8_t*>(input.pixels.data());
    frame->linesize[0] = input.stride.to_primitive();
    auto stride        = (d.width.to_primitive() * 4 + 63) & ~63U;
    auto pixels        = image::Pixels::allocate(
        budget.clone(), usize(stride) * usize(d.height.to_primitive()) + usize(64));
    if (pixels.is_err()) return Err(pixels.unwrap_err_unchecked());
    ImageScaler scaler;
    auto        converted =
        scaler.convert(*frame.get(), frame->width, frame->height, pixels->data_mut(), stride);
    if (converted.is_err()) return Err(converted.unwrap_err_unchecked());
    for (unsigned y = 0; y < d.height.to_primitive(); ++y) {
        const auto* in  = pixels->data() + y * stride;
        auto*       out = pixels->data_mut() + y * d.width.to_primitive() * 4;
        for (unsigned x = 0; x < d.width.to_primitive(); ++x) {
            auto alpha = in[3];
            for (unsigned c = 0; c < 3; ++c) out[c] = (unsigned(in[c]) * alpha + 127) / 255;
            out[3] = alpha;
            in += 4;
            out += 4;
        }
    }
    return pixels;
}

export auto encode_still(const image::StillImage& input, image::EncodeSpec spec,
                         image::Budget budget, image::Limits limits)
    -> Result<image::Pixels, image::Error> {
    using E       = image::Error;
    const auto& d = input.descriptor;
    if (spec.encoding != image::Encoding::Jxl && spec.encoding != image::Encoding::Png)
        return Err(E::Unsupported);
#if ! LITO_FEAT_JXL
    if (spec.encoding == image::Encoding::Jxl) return Err(E::Unsupported);
#endif
    const bool jxl = spec.encoding == image::Encoding::Jxl;
    auto*      encoder =
        jxl ? avcodec_find_encoder_by_name("libjxl") : avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (! encoder) return Err(E::Unsupported);
    if (spec.effort < u32(1) || spec.effort > u32(9) || d.orientation < u32(1) ||
        d.orientation > u32(8))
        return Err(E::InvalidInput);
    auto        format = pixel_format(d.pixels);
    const auto* desc   = av_pix_fmt_desc_get(format);
    if (! desc || d.width == u32() || d.height == u32() || d.width > u32(INT_MAX) ||
        d.height > u32(INT_MAX))
        return Err(E::InvalidInput);
    const auto bytes_per_pixel = unsigned(av_get_bits_per_pixel(desc) / 8);
    if (u64(d.width.to_primitive()) * u64(d.height.to_primitive()) > limits.source_pixels ||
        u64(d.width.to_primitive()) * u64(bytes_per_pixel) > u64(input.stride.to_primitive()) ||
        u64(input.stride.to_primitive()) * u64(d.height.to_primitive()) >
            u64(input.pixels.size().to_primitive()))
        return Err(E::ResourceLimit);
    int      w = int(d.width.to_primitive()), h = int(d.height.to_primitive());
    unsigned orientation = d.orientation.to_primitive();
    int      width = orientation >= 5 ? h : w, height = orientation >= 5 ? w : h;
    auto     stride = usize(width) * usize(bytes_per_pixel);
    auto     data   = image::Pixels::allocate(budget.clone(), stride * usize(height));
    if (data.is_err()) return Err(data.unwrap_err_unchecked());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            int sx = x, sy = y;
            switch (orientation) {
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
            auto*       out = data->data_mut() + usize(y).to_primitive() * stride.to_primitive() +
                              x * bytes_per_pixel;
            const auto* in =
                input.pixels.data() + sy * input.stride.to_primitive() + sx * bytes_per_pixel;
            rstd::mem::memcpy(out, in, usize(bytes_per_pixel));
        }
    if (! jxl && desc->comp[0].depth == 16) {
        for (usize i {}; i < data->size(); i += usize(2)) {
            auto* p = data->data_mut() + i.to_primitive();
            auto  v = p[0];
            p[0]    = p[1];
            p[1]    = v;
        }
        if (format == AV_PIX_FMT_GRAY16LE) format = AV_PIX_FMT_GRAY16BE;
        if (format == AV_PIX_FMT_YA16LE) format = AV_PIX_FMT_YA16BE;
        if (format == AV_PIX_FMT_RGB48LE) format = AV_PIX_FMT_RGB48BE;
        if (format == AV_PIX_FMT_RGBA64LE) format = AV_PIX_FMT_RGBA64BE;
    }
    CodecCtxPtr codec(avcodec_alloc_context3(encoder));
    FramePtr    frame(av_frame_alloc());
    PacketPtr   packet(av_packet_alloc());
    if (! codec || ! frame || ! packet) return Err(E::ResourceLimit);
    codec->width           = width;
    codec->height          = height;
    codec->pix_fmt         = format;
    codec->time_base       = { 1, 1 };
    codec->thread_count    = 1;
    codec->color_range     = AVCOL_RANGE_JPEG;
    codec->color_primaries = AVCOL_PRI_BT709;
    codec->color_trc       = AVCOL_TRC_IEC61966_2_1;
    if (jxl && (av_opt_set_double(codec->priv_data, "distance", 0.0, 0) < 0 ||
                av_opt_set_int(codec->priv_data, "effort", spec.effort.to_primitive(), 0) < 0))
        return Err(E::Unsupported);
    if (avcodec_open2(codec.get(), encoder, nullptr) < 0) return Err(E::Encode);
    frame->width           = width;
    frame->height          = height;
    frame->format          = format;
    frame->pts             = 0;
    frame->color_range     = codec->color_range;
    frame->color_primaries = codec->color_primaries;
    frame->color_trc       = codec->color_trc;
    frame->data[0]         = data->data_mut();
    frame->linesize[0]     = int(stride.to_primitive());
    if (input.icc.is_some()) {
        auto size = input.icc->size();
        if (u64(size.to_primitive()) > limits.metadata_bytes) return Err(E::ResourceLimit);
        auto* sd =
            av_frame_new_side_data(frame.get(), AV_FRAME_DATA_ICC_PROFILE, size.to_primitive());
        if (! sd) return Err(E::ResourceLimit);
        rstd::mem::memcpy(sd->data, input.icc->data(), size);
    }
    if (avcodec_send_frame(codec.get(), frame.get()) < 0) return Err(E::Encode);
    if (avcodec_send_frame(codec.get(), nullptr) < 0) return Err(E::Encode);
    if (avcodec_receive_packet(codec.get(), packet.get()) < 0) return Err(E::Encode);
    if (packet->size <= 0 || u64(packet->size) > limits.output_bytes) return Err(E::ResourceLimit);
    auto result = image::Pixels::allocate(budget.clone(), usize(packet->size));
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    rstd::mem::memcpy(result->data_mut(), packet->data, usize(packet->size));
    av_packet_unref(packet.get());
    if (avcodec_receive_packet(codec.get(), packet.get()) != AVERROR_EOF) return Err(E::Encode);
    return result;
}

} // namespace wavsen::ffi::ffmpeg
