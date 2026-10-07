import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

#if LITO_FEAT_AUDIO
namespace wavsen::tests::audio_capture_window
{
int run(int, char**);
}
namespace wavsen::tests::audio_gain
{
int run(int, char**);
}
namespace wavsen::tests::audio_backend
{
int run(int, char**);
}
namespace wavsen::tests::audio_media
{
int run(int, char**);
}
namespace wavsen::tests::audio_ownership
{
int run(int, char**);
}
namespace wavsen::tests::stream_decoder_rate
{
int run(int, char**);
}
#endif
#if LITO_FEAT_VIDEO
namespace wavsen::tests::decode_owner
{
int run(int, char**);
}
namespace wavsen::tests::presenter
{
int run(int, char**);
}
#endif
#if LITO_FEAT_IMAGE
namespace wavsen::tests::image
{
int run(int, char**);
}
namespace wavsen::tests::still
{
int run(int, char**);
}
#    if LITO_FEAT_JXL
namespace wavsen::tests::container
{
int run(int, char**);
}
namespace wavsen::tests::jpeg
{
int run(int, char**);
}
#    endif
#endif

struct Suite {
    ref<str> name;
    int (*run)(int, char**);
};

int main(int argc, char** argv) {
    auto suites = Vec<Suite>::make();
#if LITO_FEAT_AUDIO
    suites.push({ "audio-capture-window"_str, wavsen::tests::audio_capture_window::run });
    suites.push({ "audio-gain"_str, wavsen::tests::audio_gain::run });
    suites.push({ "audio-backend"_str, wavsen::tests::audio_backend::run });
    suites.push({ "audio-media"_str, wavsen::tests::audio_media::run });
    suites.push({ "audio-ownership"_str, wavsen::tests::audio_ownership::run });
    suites.push({ "stream-decoder-rate"_str, wavsen::tests::stream_decoder_rate::run });
#endif
#if LITO_FEAT_VIDEO
    suites.push({ "decode-owner"_str, wavsen::tests::decode_owner::run });
    suites.push({ "presenter"_str, wavsen::tests::presenter::run });
#endif
#if LITO_FEAT_IMAGE
    suites.push({ "image"_str, wavsen::tests::image::run });
    suites.push({ "still"_str, wavsen::tests::still::run });
#    if LITO_FEAT_JXL
    suites.push({ "container"_str, wavsen::tests::container::run });
    suites.push({ "jpeg"_str, wavsen::tests::jpeg::run });
#    endif
#endif
    if (argc > 1) {
        auto selected = rstd::ffi::CStr::from_ptr(argv[1]).to_str().unwrap();
        for (const auto& suite : suites) {
            if (suite.name != selected) continue;
            const auto result = suite.run(argc - 1, argv + 1);
            rstd::io::println(
                "{} {} ({})", result == 0 ? "PASS"_str : "FAIL"_str, suite.name, result);
            return result;
        }
        rstd::io::eprintln("Unknown or disabled suite: {}", selected);
        return 2;
    }
    int failed = 0;
    for (const auto& suite : suites) {
        const auto result = suite.run(1, argv);
        rstd::io::println("{} {} ({})", result == 0 ? "PASS"_str : "FAIL"_str, suite.name, result);
        if (result != 0) ++failed;
    }
    rstd::io::println("{} suites, {} failed", suites.len(), failed);
    return failed == 0 ? 0 : 1;
}
