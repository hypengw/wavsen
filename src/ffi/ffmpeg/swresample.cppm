module;

extern "C" {
#include <libswresample/swresample.h>
}

export module wavsen.ffi.ffmpeg:swresample;

export {
    using ::SwrContext;

    using ::swr_alloc_set_opts2;
    using ::swr_convert;
    using ::swr_free;
    using ::swr_get_out_samples;
    using ::swr_init;
}
