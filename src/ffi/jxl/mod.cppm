module;
#include <rstd/macro.hpp>
#include <jxl/encode.h>
#include <jxl/decode.h>
export module wavsen.ffi.jxl;
import wavsen.image;
import rstd;

export auto wavsen_jxl_version() -> unsigned { return JxlEncoderVersion(); }

using namespace rstd::prelude;
using namespace wavsen::image;

namespace wavsen::ffi::jxl {
struct Memory {
    Budget budget;
    bool exhausted {};
    static auto allocate(void* context, size_t bytes) -> void* {
        auto& self = *static_cast<Memory*>(context);
        if (bytes > usize::MAX.to_primitive() - 64) {
            self.exhausted = true;
            return nullptr;
        }
        auto layout = rstd::alloc::Layout::from_size_align(usize(bytes + 64), usize(64));
        if (layout.is_none()) {
            self.exhausted = true;
            return nullptr;
        }
        auto allocated = rstd::as<rstd::alloc::Allocator>(self.budget).allocate(*layout);
        if (allocated.is_err()) {
            self.exhausted = true;
            return nullptr;
        }
        auto* start = static_cast<rstd::uint8_t*>(allocated->pointer);
        *reinterpret_cast<size_t*>(start) = bytes + 64;
        return start + 64;
    }
    static void release(void* context, void* pointer) {
        if (!pointer) return;
        auto& self = *static_cast<Memory*>(context);
        auto* start = static_cast<rstd::uint8_t*>(pointer) - 64;
        auto layout = rstd::alloc::Layout::from_size_align(
            usize(*reinterpret_cast<size_t*>(start)), usize(64)).unwrap();
        rstd::as<rstd::alloc::Allocator>(self.budget).deallocate(start, layout);
    }
    auto manager() -> JxlMemoryManager { return {this, allocate, release}; }
};
struct Encoder {
    JxlEncoder* value;
    ~Encoder() { JxlEncoderDestroy(value); }
};
struct Decoder {
    JxlDecoder* value;
    ~Decoder() { JxlDecoderDestroy(value); }
};
struct Operation {
    const Cancellation& cancel;
    Limits limits;
    rstd::time::Instant started = rstd::time::Instant::now();
    auto check() const -> Result<empty, Error> {
        if (cancel.cancelled()) return Err(Error::Cancelled);
        if (started.elapsed().as_millis() > limits.operation_ms) return Err(Error::ResourceLimit);
        return Ok(empty {});
    }
};
auto read(const Source& source, Budget budget, const Operation& operation) -> Result<Pixels, Error> {
    if (source.size() == u64() || source.size() > operation.limits.encoded_bytes ||
        source.size() > u64(usize::MAX.to_primitive())) return Err(Error::ResourceLimit);
    auto input = source.reader();
    auto bytes = rstd_try(Pixels::allocate(rstd::move(budget), usize(source.size().to_primitive())));
    usize offset {};
    while (offset < bytes.size()) {
        rstd_try(operation.check());
        auto count = input.read(mut_ref<u8[]>::from_raw_parts(
            reinterpret_cast<rstd::byte*>(bytes.data_mut() + offset.to_primitive()),
            (bytes.size() - offset).min(usize(65536))));
        if (count.is_err() || *count == usize()) return Err(Error::Io);
        offset += *count;
    }
    return Ok(rstd::move(bytes));
}
auto grow(Pixels& bytes, usize used, Budget budget, u64 limit) -> Result<empty, Error> {
    if (u64(bytes.size().to_primitive()) >= limit) return Err(Error::ResourceLimit);
    auto next = (u64(bytes.size().to_primitive()) * u64(2)).min(limit);
    auto larger = rstd_try(Pixels::allocate(rstd::move(budget), usize(next.to_primitive())));
    rstd::mem::memcpy(larger.data_mut(), bytes.data(), used);
    bytes = rstd::move(larger);
    return Ok(empty {});
}
auto exact(const Pixels& bytes, usize used, Budget budget) -> Result<Pixels, Error> {
    auto result = rstd_try(Pixels::allocate(rstd::move(budget), used));
    rstd::mem::memcpy(result.data_mut(), bytes.data(), used);
    return Ok(rstd::move(result));
}
auto encoder_error(JxlEncoder* encoder, const Memory& memory) -> Error {
    if (memory.exhausted || JxlEncoderGetError(encoder) == JXL_ENC_ERR_OOM)
        return Error::ResourceLimit;
    auto error = JxlEncoderGetError(encoder);
    if (error == JXL_ENC_ERR_NOT_SUPPORTED || error == JXL_ENC_ERR_JBRD)
        return Error::Unsupported;
    return Error::Encode;
}
export auto recompress(const Source& source, u32 effort, Budget budget,
                       const Cancellation& cancel, Limits limits) -> Result<Pixels, Error> {
    Operation operation {cancel, limits};
    rstd_try(operation.check());
    if (effort < u32(1) || effort > u32(9) || limits.output_bytes == u64() ||
        limits.output_bytes > u64(usize::MAX.to_primitive())) return Err(Error::InvalidInput);
    auto input = rstd_try(read(source, budget.clone(), operation));
    Memory memory {budget.clone()};
    auto manager = memory.manager();
    Encoder encoder {JxlEncoderCreate(&manager)};
    if (!encoder.value) return Err(Error::ResourceLimit);
    auto* settings = JxlEncoderFrameSettingsCreate(encoder.value, nullptr);
    if (!settings) return Err(Error::ResourceLimit);
    if (JxlEncoderStoreJPEGMetadata(encoder.value, JXL_TRUE) != JXL_ENC_SUCCESS ||
        JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT,
                                         effort.to_primitive()) != JXL_ENC_SUCCESS ||
        JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_JPEG_COMPRESS_BOXES, 0) != JXL_ENC_SUCCESS ||
        JxlEncoderAddJPEGFrame(settings, input.data(), input.size().to_primitive()) != JXL_ENC_SUCCESS)
        return Err(encoder_error(encoder.value, memory));
    JxlEncoderCloseInput(encoder.value);
    auto output = rstd_try(Pixels::allocate(budget.clone(),
        usize(limits.output_bytes.min(u64(65536)).to_primitive())));
    usize used {};
    for (;;) {
        rstd_try(operation.check());
        auto* next = output.data_mut() + used.to_primitive();
        auto available = (output.size() - used).to_primitive();
        auto status = JxlEncoderProcessOutput(encoder.value, &next, &available);
        used = output.size() - usize(available);
        rstd_try(operation.check());
        if (status == JXL_ENC_SUCCESS) return exact(output, used, rstd::move(budget));
        if (status != JXL_ENC_NEED_MORE_OUTPUT) return Err(encoder_error(encoder.value, memory));
        rstd_try(grow(output, used, budget.clone(), limits.output_bytes));
    }
}
export auto reconstruct(const Pixels& input, Budget budget, const Cancellation& cancel,
                        Limits limits) -> Result<Pixels, Error> {
    Operation operation {cancel, limits};
    rstd_try(operation.check());
    if (u64(input.size().to_primitive()) > limits.output_bytes || limits.output_bytes == u64() ||
        limits.output_bytes > u64(usize::MAX.to_primitive())) return Err(Error::ResourceLimit);
    Memory memory {budget.clone()};
    auto manager = memory.manager();
    Decoder decoder {JxlDecoderCreate(&manager)};
    if (!decoder.value) return Err(Error::ResourceLimit);
    if (JxlDecoderSubscribeEvents(decoder.value,
            JXL_DEC_BASIC_INFO | JXL_DEC_JPEG_RECONSTRUCTION | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS ||
        JxlDecoderSetInput(decoder.value, input.data(), input.size().to_primitive()) != JXL_DEC_SUCCESS)
        return Err(Error::Decode);
    JxlDecoderCloseInput(decoder.value);
    auto output = rstd_try(Pixels::allocate(budget.clone(),
        usize(limits.output_bytes.min(u64(65536)).to_primitive())));
    usize used {};
    bool reconstruction = false, complete = false, bound = false;
    for (;;) {
        rstd_try(operation.check());
        auto status = JxlDecoderProcessInput(decoder.value);
        rstd_try(operation.check());
        if (status == JXL_DEC_BASIC_INFO) {
            JxlBasicInfo info {};
            if (JxlDecoderGetBasicInfo(decoder.value, &info) != JXL_DEC_SUCCESS) return Err(Error::Decode);
            if (info.have_animation || u64(info.xsize) * u64(info.ysize) > limits.source_pixels)
                return Err(Error::ResourceLimit);
        } else if (status == JXL_DEC_JPEG_RECONSTRUCTION || status == JXL_DEC_JPEG_NEED_MORE_OUTPUT) {
            if (bound) {
                used = output.size() - usize(JxlDecoderReleaseJPEGBuffer(decoder.value));
                rstd_try(grow(output, used, budget.clone(), limits.output_bytes));
            }
            reconstruction = true;
            if (JxlDecoderSetJPEGBuffer(decoder.value, output.data_mut() + used.to_primitive(),
                    (output.size() - used).to_primitive()) != JXL_DEC_SUCCESS) return Err(Error::Decode);
            bound = true;
        } else if (status == JXL_DEC_FULL_IMAGE) {
            complete = true;
        } else if (status == JXL_DEC_SUCCESS) {
            if (!reconstruction) return Err(Error::Unsupported);
            if (!complete || !bound) return Err(Error::Decode);
            used = output.size() - usize(JxlDecoderReleaseJPEGBuffer(decoder.value));
            return exact(output, used, rstd::move(budget));
        } else {
            return Err(memory.exhausted ? Error::ResourceLimit :
                (!reconstruction && status == JXL_DEC_NEED_IMAGE_OUT_BUFFER ? Error::Unsupported : Error::Decode));
        }
    }
}
}
