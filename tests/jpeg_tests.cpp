#if LITO_FEAT_IMAGE && LITO_FEAT_JXL
#    include <rstd/macro.hpp>
import wavsen.image;

namespace wavsen::tests::jpeg
{
using namespace rstd::prelude;
using namespace rstd::literals;
using namespace wavsen::image;

void check(Source source, const Budget& budget) {
    auto   cancel = Cancellation::make().unwrap();
    Limits limits;
    limits.operation_ms = u64(120000);
    auto compressed     = recompress_jpeg(source, u32(3), budget.clone(), cancel, limits);
    if (compressed.is_err())
        rstd::io::println("JPEG recompression rejected {}: error {}",
                          source.name(),
                          static_cast<unsigned>(compressed.unwrap_err()));
    auto encoded = rstd::move(compressed).unwrap();
    verify_jpeg(source, encoded, budget.clone(), cancel, limits).unwrap();
    auto session =
        ImageSession::open(source.clone(), budget.clone(), cancel.clone(), limits).unwrap();
    auto prepared = session.prepare_still().unwrap();
    auto master   = archive(prepared, budget.clone(), cancel, limits).unwrap();
    rstd_assert(master.fidelity == Fidelity::JpegReconstruction);
    verify_jpeg(source, master.bytes, budget.clone(), cancel, limits).unwrap();
    auto decoded     = ImageSession::open(Source::memory(encoded.clone(), "image.jxl"_str).unwrap(),
                                          budget.clone(),
                                          cancel.clone(),
                                          limits)
                           .unwrap();
    auto image       = decoded.prepare_still().unwrap();
    bool source_swap = prepared.descriptor.orientation >= u32(5);
    bool master_swap = image.descriptor.orientation >= u32(5);
    rstd_assert((source_swap ? prepared.descriptor.height : prepared.descriptor.width) ==
                (master_swap ? image.descriptor.height : image.descriptor.width));
    rstd_assert((source_swap ? prepared.descriptor.width : prepared.descriptor.height) ==
                (master_swap ? image.descriptor.width : image.descriptor.height));
    if (prepared.icc.is_some()) {
        rstd_assert(image.icc.is_some() && prepared.icc->size() == image.icc->size());
        rstd_assert(
            rstd::mem::memcmp(prepared.icc->data(), image.icc->data(), prepared.icc->size()) == 0);
    }
    auto container = JxlContainer::open(encoded.clone(), limits).unwrap();
    rstd_assert(
        container
            .rewrite_metadata({ None(), Some("<changed/>"_str.as_bytes()) }, budget.clone(), cancel)
            .is_err());
    auto tiny         = limits;
    tiny.output_bytes = u64(1);
    rstd_assert(recompress_jpeg(source, u32(3), budget.clone(), cancel, tiny).is_err());
    tiny               = limits;
    tiny.encoded_bytes = u64(1);
    rstd_assert(recompress_jpeg(source, u32(3), budget.clone(), cancel, tiny).is_err());
    tiny               = limits;
    tiny.source_pixels = u64(1);
    rstd_assert(reconstruct_jpeg(encoded, budget.clone(), cancel, tiny).is_err());
    auto cut = Pixels::allocate(budget.clone(), encoded.size() / usize(2)).unwrap();
    rstd::mem::memcpy(cut.data_mut(), encoded.data(), cut.size());
    rstd_assert(reconstruct_jpeg(cut, budget.clone(), cancel, limits).is_err());
    auto pixel_encoded = encode(image, {}, budget.clone(), cancel, limits).unwrap();
    rstd_assert(reconstruct_jpeg(pixel_encoded, budget.clone(), cancel, limits).is_err());
    auto tiny_budget = Budget::try_make(usize(128)).unwrap();
    rstd_assert(recompress_jpeg(source, u32(3), tiny_budget.clone(), cancel, limits).is_err());
    cancel.cancel();
    rstd_assert(recompress_jpeg(source, u32(3), budget.clone(), cancel, limits).unwrap_err() ==
                Error::Cancelled);
    rstd_assert(reconstruct_jpeg(encoded, budget.clone(), cancel, limits).unwrap_err() ==
                Error::Cancelled);
    rstd::io::println(
        "PASS jpeg {} -> {} bytes, exact reconstruction and bounds", source.size(), encoded.size());
}

int run(int argc, char** argv) {
    rstd_assert(capabilities().jpeg_reconstruction);
    auto                budget = Budget::try_make(usize(1024 * 1024 * 1024)).unwrap();
    const rstd::uint8_t jpeg[] = {
        255, 216, 255, 224, 0,   16,  74,  70,  73,  70,  0,   1,   1,   0,   0,   1,   0,   1,
        0,   0,   255, 219, 0,   67,  0,   4,   3,   3,   4,   3,   3,   4,   4,   3,   4,   5,
        4,   4,   5,   6,   10,  7,   6,   6,   6,   6,   13,  9,   10,  8,   10,  15,  13,  16,
        16,  15,  13,  15,  14,  17,  19,  24,  20,  17,  18,  23,  18,  14,  15,  21,  28,  21,
        23,  25,  25,  27,  27,  27,  16,  20,  29,  31,  29,  26,  31,  24,  26,  27,  26,  255,
        219, 0,   67,  1,   4,   5,   5,   6,   5,   6,   12,  7,   7,   12,  26,  17,  15,  17,
        26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
        26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,
        26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  26,  255, 192, 0,   17,
        8,   0,   5,   0,   7,   3,   1,   34,  0,   2,   17,  1,   3,   17,  1,   255, 196, 0,
        31,  0,   0,   1,   5,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,   0,
        0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  255, 196, 0,   181, 16,  0,
        2,   1,   3,   3,   2,   4,   3,   5,   5,   4,   4,   0,   0,   1,   125, 1,   2,   3,
        0,   4,   17,  5,   18,  33,  49,  65,  6,   19,  81,  97,  7,   34,  113, 20,  50,  129,
        145, 161, 8,   35,  66,  177, 193, 21,  82,  209, 240, 36,  51,  98,  114, 130, 9,   10,
        22,  23,  24,  25,  26,  37,  38,  39,  40,  41,  42,  52,  53,  54,  55,  56,  57,  58,
        67,  68,  69,  70,  71,  72,  73,  74,  83,  84,  85,  86,  87,  88,  89,  90,  99,  100,
        101, 102, 103, 104, 105, 106, 115, 116, 117, 118, 119, 120, 121, 122, 131, 132, 133, 134,
        135, 136, 137, 138, 146, 147, 148, 149, 150, 151, 152, 153, 154, 162, 163, 164, 165, 166,
        167, 168, 169, 170, 178, 179, 180, 181, 182, 183, 184, 185, 186, 194, 195, 196, 197, 198,
        199, 200, 201, 202, 210, 211, 212, 213, 214, 215, 216, 217, 218, 225, 226, 227, 228, 229,
        230, 231, 232, 233, 234, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 255, 196, 0,
        31,  1,   0,   3,   1,   1,   1,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,
        0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  255, 196, 0,   181, 17,  0,
        2,   1,   2,   4,   4,   3,   4,   7,   5,   4,   4,   0,   1,   2,   119, 0,   1,   2,
        3,   17,  4,   5,   33,  49,  6,   18,  65,  81,  7,   97,  113, 19,  34,  50,  129, 8,
        20,  66,  145, 161, 177, 193, 9,   35,  51,  82,  240, 21,  98,  114, 209, 10,  22,  36,
        52,  225, 37,  241, 23,  24,  25,  26,  38,  39,  40,  41,  42,  53,  54,  55,  56,  57,
        58,  67,  68,  69,  70,  71,  72,  73,  74,  83,  84,  85,  86,  87,  88,  89,  90,  99,
        100, 101, 102, 103, 104, 105, 106, 115, 116, 117, 118, 119, 120, 121, 122, 130, 131, 132,
        133, 134, 135, 136, 137, 138, 146, 147, 148, 149, 150, 151, 152, 153, 154, 162, 163, 164,
        165, 166, 167, 168, 169, 170, 178, 179, 180, 181, 182, 183, 184, 185, 186, 194, 195, 196,
        197, 198, 199, 200, 201, 202, 210, 211, 212, 213, 214, 215, 216, 217, 218, 226, 227, 228,
        229, 230, 231, 232, 233, 234, 242, 243, 244, 245, 246, 247, 248, 249, 250, 255, 218, 0,
        12,  3,   1,   0,   2,   17,  3,   17,  0,   63,  0,   242, 26,  40,  162, 190, 4,   251,
        3,   255, 217,
    };
    auto memory = Pixels::allocate(budget.clone(), usize(sizeof(jpeg))).unwrap();
    rstd::mem::memcpy(memory.data_mut(), jpeg, memory.size());
    check(Source::memory(rstd::move(memory), "sample.jpg"_str).unwrap(), budget);
    for (int i = 1; i < argc; ++i) {
        auto path = rstd::path::PathBuf::from(rstd::ffi::CStr::from_ptr(argv[i]).to_str().unwrap());
        check(Source::file(path.as_path()).unwrap(), budget);
    }
    return 0;
}

} // namespace wavsen::tests::jpeg
#endif
