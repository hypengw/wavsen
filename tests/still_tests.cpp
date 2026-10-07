#if LITO_FEAT_IMAGE
#    include <rstd/macro.hpp>
import wavsen.image;

namespace wavsen::tests::still
{
using namespace rstd::prelude;
using namespace rstd::literals;
using namespace wavsen::image;

void compare(const StillImage& a, const StillImage& b, unsigned bytes) {
    rstd_assert(a.descriptor.width == b.descriptor.width);
    rstd_assert(a.descriptor.height == b.descriptor.height);
    rstd_assert(a.descriptor.pixels == b.descriptor.pixels);
    for (unsigned y = 0; y < a.descriptor.height.to_primitive(); ++y)
        for (unsigned x = 0; x < a.descriptor.width.to_primitive() * bytes; ++x)
            rstd_assert(a.pixels.data()[y * a.stride.to_primitive() + x] ==
                        b.pixels.data()[y * b.stride.to_primitive() + x]);
}

auto decoded(Pixels bytes, const Budget& budget, ref<str> name) -> StillImage {
    auto source = Source::memory(rstd::move(bytes), name).unwrap();
    auto session =
        ImageSession::open(rstd::move(source), budget.clone(), Cancellation::make().unwrap())
            .unwrap();
    auto image = session.prepare_still().unwrap();
    rstd_assert(session.prepare_still().is_err());
    return image;
}

int run(int, char**) {
    auto caps = capabilities();
    rstd_assert(caps.png_encode);
#    if LITO_FEAT_JXL
    rstd_assert(caps.jxl_decode && caps.jxl_encode && caps.jpeg_reconstruction);
#    else
    rstd_assert(! caps.jxl_decode && ! caps.jxl_encode && ! caps.jpeg_reconstruction);
    rstd_assert(caps.jxl_version == u32());
#    endif
    auto budget = Budget::try_make(usize(256 * 1024 * 1024)).unwrap();
    auto cancel = Cancellation::make().unwrap();
    for (unsigned kind = 0; kind < 8; ++kind) {
        unsigned channels = kind % 4 + 1, depth = kind < 4 ? 1 : 2;
        unsigned pixel_bytes = channels * depth, stride = 17 * pixel_bytes;
        auto     memory = Pixels::allocate(budget.clone(), usize(stride * 11)).unwrap();
        for (unsigned i = 0; i < stride * 11; ++i) memory.data_mut()[i] = (i * 29 + i / 7) % 256;
        if (channels == 2 || channels == 4)
            for (unsigned y = 0; y < 11; ++y)
                for (unsigned byte = 0; byte < depth; ++byte)
                    memory.data_mut()[y * stride + pixel_bytes - depth + byte] = 0;
        StillImage original { rstd::move(memory),
                              u32(stride),
                              { u32(17),
                                u32(11),
                                static_cast<PixelFormat>(kind),
                                Encoding::Png,
                                u32(1),
                                channels % 2 == 0 },
                              None() };
        auto       png = encode(original, { Encoding::Png }, budget.clone(), cancel).unwrap();
        if (kind == 3) {
            auto cut = Pixels::allocate(budget.clone(), png.size() / usize(2)).unwrap();
            rstd::mem::memcpy(cut.data_mut(), png.data(), cut.size());
            auto source = Source::memory(rstd::move(cut), "truncated.png"_str).unwrap();
            auto opened = ImageSession::open(rstd::move(source), budget.clone(), cancel.clone());
            rstd_assert(opened.is_err() || opened->prepare_still().is_err());
        }
        auto prepared = decoded(rstd::move(png), budget, "sample.png"_str);
        compare(original, prepared, pixel_bytes);
        rstd_assert(prepared.descriptor.alpha == original.descriptor.alpha);
#    if LITO_FEAT_JXL
        const auto xmp    = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">source</x:xmpmeta>"_str;
        auto       packet = Pixels::allocate(budget.clone(), xmp.len()).unwrap();
        rstd::mem::memcpy(packet.data_mut(), xmp.as_bytes().as_raw_ptr(), packet.size());
        prepared.source_xmp = Some(rstd::move(packet));
        auto master         = archive(prepared, budget.clone(), cancel).unwrap();
        rstd_assert(master.fidelity == Fidelity::SampleLossless);
        auto jxl = rstd::move(master.bytes);
        rstd_assert(verify_lossless(prepared, jxl, budget.clone(), cancel).is_ok());
        auto restored = decoded(rstd::move(jxl), budget, "sample.jxl"_str);
        compare(prepared, restored, pixel_bytes);
#    else
        auto unsupported = encode(prepared, { Encoding::Jxl }, budget.clone(), cancel);
        rstd_assert(unsupported.is_err() && unsupported.unwrap_err() == Error::Unsupported);
#    endif
        auto thumb    = preview(prepared, u32(8), u32(8), budget.clone(), cancel).unwrap();
        auto rendered = encode(thumb, { Encoding::Png }, budget.clone(), cancel).unwrap();
        auto display  = decoded(rstd::move(rendered), budget, "preview.png"_str);
        rstd_assert(display.descriptor.width == u32(8) && display.descriptor.height == u32(5));
        auto small         = Limits {};
        small.output_bytes = u64(1);
        rstd_assert(encode(prepared, { Encoding::Png }, budget.clone(), cancel, small).is_err());
        rstd::io::println("PASS: format {} retained every sample including transparent color",
                          kind);
    }
    cancel.cancel();
    auto       input = Pixels::allocate(budget.clone(), usize(4)).unwrap();
    StillImage image {
        rstd::move(input), u32(4), { u32(1), u32(1), PixelFormat::Rgba8, Encoding::Png }, None()
    };
    auto cancelled = encode(image, { Encoding::Png }, budget.clone(), cancel);
    rstd_assert(cancelled.is_err() && cancelled.unwrap_err() == Error::Cancelled);
    return 0;
}

} // namespace wavsen::tests::still
#endif
