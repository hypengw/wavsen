#include <rstd/macro.hpp>
import rstd;
import wavsen.image;
import wavsen.ffi.ffmpeg;
import wavsen.ffi.ffmpeg.image;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace wavsen::image;

void image_color_conversion(const Budget& budget) {
    auto    input  = Pixels::allocate(budget.clone(), usize(64 * 4 * 3 + 64)).unwrap();
    auto    output = Pixels::allocate(budget.clone(), usize(128 * 8 + 64)).unwrap();
    AVFrame frame {};
    frame.width      = 16;
    frame.height     = 4;
    frame.format     = AVPixelFormat::AV_PIX_FMT_YUV420P;
    frame.colorspace = AVColorSpace::AVCOL_SPC_BT470BG;
    for (int plane = 0; plane < 3; ++plane) {
        frame.data[plane]     = input.data_mut() + plane * 64 * 4;
        frame.linesize[plane] = 64;
    }
    auto fill = [&](int y, int u, int v) {
        for (int i = 0; i < 64 * 4; ++i) {
            frame.data[0][i] = static_cast<rstd::uint8_t>(y);
            frame.data[1][i] = static_cast<rstd::uint8_t>(u);
            frame.data[2][i] = static_cast<rstd::uint8_t>(v);
        }
    };
    wavsen::ffi::ffmpeg::ImageScaler scaler;
    auto check = [&](int r, int g, int b, int width = 16, int height = 4) {
        scaler.convert(frame, width, height, output.data_mut(), 128).unwrap();
        const auto* pixels = output.data();
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const auto* p = pixels + y * 128 + x * 4;
                rstd_assert(int(p[0]) >= r - 3 && int(p[0]) <= r + 3);
                rstd_assert(int(p[1]) >= g - 3 && int(p[1]) <= g + 3);
                rstd_assert(int(p[2]) >= b - 3 && int(p[2]) <= b + 3);
                rstd_assert(p[3] == 255);
            }
    };
    frame.color_range = AVColorRange::AVCOL_RANGE_MPEG;
    fill(16, 128, 128);
    check(0, 0, 0);
    fill(235, 128, 128);
    check(255, 255, 255);
    frame.color_range = AVColorRange::AVCOL_RANGE_JPEG;
    fill(16, 128, 128);
    check(16, 16, 16);
    fill(235, 128, 128);
    check(235, 235, 235);
    frame.format      = AVPixelFormat::AV_PIX_FMT_YUVJ420P;
    frame.color_range = AVColorRange::AVCOL_RANGE_UNSPECIFIED;
    check(235, 235, 235);
    frame.color_range = AVColorRange::AVCOL_RANGE_MPEG;
    check(255, 255, 255);
    frame.format = AVPixelFormat::AV_PIX_FMT_YUV420P;
    fill(100, 90, 180);
    check(181, 70, 21);
    frame.colorspace = AVColorSpace::AVCOL_SPC_BT709;
    check(191, 78, 18);
    check(191, 78, 18, 32, 8);
    frame.colorspace = AVColorSpace::AVCOL_SPC_UNSPECIFIED;
    check(181, 70, 21);
    frame.format      = AVPixelFormat::AV_PIX_FMT_GRAY8;
    frame.color_range = AVColorRange::AVCOL_RANGE_UNSPECIFIED;
    fill(16, 128, 128);
    check(16, 16, 16);
}

void jpeg_output_bounds(const Budget& budget) {
    // 24x8 red YUV420 JPEG: its 96-byte RGBA row triggers the SIMD tail case.
    constexpr rstd::uint8_t jpeg[] {
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x02, 0x00, 0x00,
        0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xfe, 0x00, 0x10, 0x4c, 0x61, 0x76, 0x63, 0x36, 0x32,
        0x2e, 0x31, 0x31, 0x2e, 0x31, 0x30, 0x30, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x08, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x07,
        0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x08, 0x08,
        0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0c, 0x0c, 0x0b, 0x0b, 0x0e, 0x0e, 0x0e, 0x11,
        0x11, 0x14, 0xff, 0xc4, 0x00, 0x4d, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01, 0x01, 0x01, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x07,
        0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x08, 0x00, 0x18,
        0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00, 0xff, 0xda, 0x00, 0x0c, 0x03,
        0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0x8b, 0x01, 0x26, 0xdf, 0xc0, 0x01,
        0xff, 0xd9
    };
    auto encoded = Pixels::allocate(budget.clone(), usize(sizeof(jpeg))).unwrap();
    rstd::mem::memcpy(encoded.data_mut(), jpeg, usize(sizeof(jpeg)));
    auto source = Source::memory(rstd::move(encoded), "red.jpg"_str).unwrap();
    for (int repeat = 0; repeat < 16; ++repeat) {
        auto session =
            ImageSession::open(source.clone(), budget.clone(), Cancellation::make().unwrap())
                .unwrap();
        auto frame = session.next().unwrap().unwrap();
        rstd_assert(frame.width == u32(24) && frame.height == u32(8));
        rstd_assert(frame.stride == u32(96));
        rstd_assert(frame.pixels.size() >= usize(128 * 8 + 64));
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 24; ++x) {
                auto* pixel = frame.pixels.data() + y * 96 + x * 4;
                rstd_assert(pixel[0] > 240 && pixel[1] < 10 && pixel[2] < 10 && pixel[3] == 255);
            }
        rstd_assert(session.next().unwrap().is_none());
    }
}

void rgba_resize(const Budget& budget) {
    auto data = Vec<u8>::from(
        "P7\nWIDTH 17\nHEIGHT 11\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n"_str
            .as_bytes());
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 17; ++x) {
            data.push(u8((x * 43 + y * 17) % 256));
            data.push(u8((x * 11 + y * 53) % 256));
            data.push(u8((x * 29 + y * 7) % 256));
            data.push(u8(x % 3 == 0 ? 0 : x % 3 == 1 ? 127 : 255));
        }
    auto encoded = Pixels::allocate(budget.clone(), data.len()).unwrap();
    rstd::mem::memcpy(encoded.data_mut(), data.data(), data.len());
    auto source = Source::memory(rstd::move(encoded), "resize.pam"_str).unwrap();
    auto session =
        ImageSession::open(source.clone(), budget.clone(), Cancellation::make().unwrap()).unwrap();
    auto        original = session.next().unwrap().unwrap();
    const auto* pixels   = original.pixels.data();
    for (auto size : array<array<int, 2>, 5> { array<int, 2> { 1, 1 },
                                               array<int, 2> { 8, 7 },
                                               array<int, 2> { 32, 25 },
                                               array<int, 2> { 1, 29 },
                                               array<int, 2> { 37, 1 } }) {
        session.reset().unwrap();
        const int   w = size[usize()], h = size[usize(1)];
        auto        resized = session.next({ u32(w), u32(h) }).unwrap().unwrap();
        const auto* output  = resized.pixels.data();
        for (int y = 0; y < h; ++y) {
            const double sy = rstd::cmp::max(0.0, (double(y) + 0.5) * 11 / h - 0.5);
            const int    y0 = int(sy), y1 = rstd::cmp::min(y0 + 1, 10);
            const double fy = sy - y0;
            for (int x = 0; x < w; ++x) {
                const double sx = rstd::cmp::max(0.0, (double(x) + 0.5) * 17 / w - 0.5);
                const int    x0 = int(sx), x1 = rstd::cmp::min(x0 + 1, 16);
                const double fx = sx - x0;
                for (int c = 0; c < 4; ++c) {
                    const double top    = pixels[(y0 * 17 + x0) * 4 + c] * (1 - fx) +
                                          pixels[(y0 * 17 + x1) * 4 + c] * fx;
                    const double bottom = pixels[(y1 * 17 + x0) * 4 + c] * (1 - fx) +
                                          pixels[(y1 * 17 + x1) * 4 + c] * fx;
                    rstd_assert(output[(y * w + x) * 4 + c] ==
                                static_cast<rstd::uint8_t>(top * (1 - fy) + bottom * fy + 0.5));
                }
                for (int c = 0; c < 3; ++c)
                    rstd_assert(output[(y * w + x) * 4 + c] <= output[(y * w + x) * 4 + 3]);
            }
        }
    }
    for (auto bounds : array<array<int, 4>, 6> { array<int, 4> { 512, 512, 17, 11 },
                                                 array<int, 4> { 8, 8, 8, 5 },
                                                 array<int, 4> { 16, 4, 6, 4 },
                                                 array<int, 4> { 8, 0, 8, 5 },
                                                 array<int, 4> { 0, 4, 6, 4 },
                                                 array<int, 4> { 1, 1, 1, 1 } }) {
        session.reset().unwrap();
        DecodeRequest request;
        request.max_width  = u32(bounds[usize()]);
        request.max_height = u32(bounds[usize(1)]);
        auto resized       = session.next(request).unwrap().unwrap();
        rstd_assert(resized.width == u32(bounds[usize(2)]));
        rstd_assert(resized.height == u32(bounds[usize(3)]));
        if (resized.width == original.width && resized.height == original.height) {
            const auto* output = resized.pixels.data();
            for (int i = 0; i < 17 * 11 * 4; ++i) rstd_assert(output[i] == pixels[i]);
        }
    }
    session.reset().unwrap();
    auto bounded =
        session.next({ u32(34), u32(22), ResizeMode::Cover, u32(8), u32(8) }).unwrap().unwrap();
    rstd_assert(bounded.width == u32(8) && bounded.height == u32(5));
    for (int side : array<int, 2> { 162, 324 }) {
        u64 micros;
        for (int repeat = 0; repeat < 30; ++repeat) {
            session.reset().unwrap();
            auto started = rstd::time::Instant::now();
            auto resized =
                session.next({ u32(side), u32(side), ResizeMode::Cover }).unwrap().unwrap();
            micros += started.elapsed().as_micros();
            rstd_assert(resized.height == u32(side));
        }
        rstd::io::eprintln(
            "alpha resize 17x11 -> cover {}x{}: {} us / 30 frames", side, side, micros);
    }
}

int main(int argc, char** argv) {
    auto budget = Budget::try_make(usize(256 * 1024 * 1024)).unwrap();
    image_color_conversion(budget);
    jpeg_output_bounds(budget);
    rgba_resize(budget);
    rstd_assert(budget.statistics().live_bytes == usize());
    {
        auto dir  = rstd::fs::TempDir::make("wavsen-image"_str).unwrap();
        auto path = rstd::path::PathBuf::from(dir.path()).join("image.ppm"_str);
        auto data = Vec<u8>::from("P6\n2 1\n255\n"_str.as_bytes());
        for (auto b : array<u8, 6> { u8(255), u8(), u8(), u8(), u8(255), u8() }) data.push(b);
        rstd::fs::write(path.as_path(), data.as_slice()).unwrap();
        auto cancel  = Cancellation::make().unwrap();
        auto session = ImageSession::open(
                           Source::file(path.as_path()).unwrap(), budget.clone(), cancel.clone())
                           .unwrap();
        auto result  = session.next().unwrap().unwrap();
        rstd_assert(result.width == u32(2) && result.height == u32(1));
        rstd_assert(result.pixels.data()[0] == 255 && result.pixels.data()[5] == 255);
        rstd_assert(session.next().unwrap().is_none());
        session.reset().unwrap();
        auto scaled = session.next({ u32(4), u32() }).unwrap().unwrap();
        rstd_assert(scaled.width == u32(4) && scaled.height == u32(2));
        session.reset().unwrap();
        auto fit = session.next({ u32(4), u32(4), ResizeMode::Fit }).unwrap().unwrap();
        rstd_assert(fit.width == u32(4) && fit.height == u32(2));
        session.reset().unwrap();
        auto cover = session.next({ u32(4), u32(4), ResizeMode::Cover }).unwrap().unwrap();
        rstd_assert(cover.width == u32(8) && cover.height == u32(4));
        cancel.cancel();
        rstd_assert(session.next().unwrap_err() == Error::Cancelled);
        auto tiny = Budget::try_make(usize(1)).unwrap();
        rstd_assert(Pixels::allocate(tiny.clone(), usize(4)).is_err());
    }
    rstd_assert(budget.statistics().live_bytes == usize());
    {
        auto data = Vec<u8>::from(
            "P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n"_str
                .as_bytes());
        for (auto b : array<u8, 8> { u8(255), u8(), u8(), u8(), u8(), u8(255), u8(), u8(255) })
            data.push(b);
        auto encoded = Pixels::allocate(budget.clone(), data.len()).unwrap();
        rstd::mem::memcpy(encoded.data_mut(), data.data(), data.len());
        auto source = Source::memory(rstd::move(encoded), "alpha.pam"_str).unwrap();
        auto session =
            ImageSession::open(source.clone(), budget.clone(), Cancellation::make().unwrap())
                .unwrap();
        auto original = session.next().unwrap().unwrap();
        rstd_assert(original.pixels.data()[0] == 0 && original.pixels.data()[3] == 0);
        rstd_assert(original.pixels.data()[5] == 255 && original.pixels.data()[7] == 255);
        session.reset().unwrap();
        auto resized = session.next({ u32(1), u32(1) }).unwrap().unwrap();
        rstd_assert(resized.pixels.data()[0] == 0);
        rstd_assert(resized.pixels.data()[1] == 128 && resized.pixels.data()[3] == 128);
        Limits limits;
        limits.encoded_bytes = u64(1);
        rstd_assert(ImageSession::open(
                        source.clone(), budget.clone(), Cancellation::make().unwrap(), limits)
                        .is_err());
        auto tiny = Budget::try_make(usize(1)).unwrap();
        auto limited =
            ImageSession::open(source.clone(), tiny.clone(), Cancellation::make().unwrap())
                .unwrap();
        rstd_assert(limited.next().is_err());
    }
    rstd_assert(budget.statistics().live_bytes == usize());
    for (int argument = 1; argument < argc; ++argument) {
        auto path = rstd::ffi::CStr::from_ptr(argv[argument]).to_str().unwrap();
        rstd::io::eprintln("decoding {}", path);
        auto path_buf = rstd::path::PathBuf::from(path);
        auto session  = ImageSession::open(Source::file(path_buf.as_path()).unwrap(),
                                           budget.clone(),
                                           Cancellation::make().unwrap())
                            .unwrap();
        u64  count;
        auto hashes   = Vec<u64>::make();
        auto checksum = [](const Pixels& pixels) {
            u64 value { 14695981039346656037ULL };
            for (usize i; i < pixels.size(); ++i)
                value = (value ^ u64(pixels.data()[i.to_primitive()]))
                            .wrapping_mul(u64(1099511628211ULL));
            return value;
        };
        for (;;) {
            auto frame = session.next().unwrap();
            if (frame.is_none()) break;
            rstd_assert(frame->index == count++);
            rstd_assert(frame->duration_us > u64());
            hashes.push(checksum(frame->pixels));
        }
        rstd_assert(count >= u64(session.info().animated ? 3 : 1));
        for (int loop = 0; loop < 10; ++loop) {
            session.reset().unwrap();
            for (u64 i; i < count; ++i) {
                auto frame = session.next().unwrap().unwrap();
                rstd_assert(checksum(frame.pixels) == hashes[usize(i.to_primitive())]);
            }
            rstd_assert(session.next().unwrap().is_none());
        }
    }
    rstd_assert(budget.statistics().live_bytes == usize());
}
