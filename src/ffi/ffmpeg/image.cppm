module;
extern "C" {
#include <errno.h>
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

    auto convert(const AVFrame& frame, int width, int height, uint8_t* output, int stride)
        -> Result<empty, image::Error> {
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
                                        AV_PIX_FMT_RGBA,
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

} // namespace wavsen::ffi::ffmpeg
