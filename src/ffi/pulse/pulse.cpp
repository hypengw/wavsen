module;

#include <pulse/pulseaudio.h>

#include <dlfcn.h>

module pulse;

import rstd;
import rstd.cppstd;

namespace wavsen::ffi::pulse
{
namespace
{

constexpr const char* kSoname = "libpulse.so.0";

struct LoadState {
    bool                 attempted {};
    bool                 loaded {};
    void*                library {};
    Api                  api {};
    rstd::string::String error {};
};

rstd::sync::Mutex<LoadState> g_state { LoadState {} };

void load_once(LoadState& state) {
    state.attempted = true;
    state.library   = dlopen(kSoname, RTLD_NOW | RTLD_LOCAL);
    if (! state.library) {
        const char* error = dlerror();
        state.error       = rstd::string::String::make(
            rstd::cppstd::as_str(error ? error : "unknown error").unwrap());
        return;
    }

#define WAVSEN_LOAD(symbol)                                                                      \
    do {                                                                                         \
        dlerror();                                                                               \
        void*       address = dlsym(state.library, #symbol);                                     \
        const char* error   = dlerror();                                                         \
        if (error) {                                                                             \
            auto message = rstd::format("missing symbol {} in {}: {}", #symbol, kSoname, error); \
            state.error  = rstd::move(message);                                                  \
            return;                                                                              \
        }                                                                                        \
        static_assert(sizeof(state.api.symbol) == sizeof(address));                              \
        rstd::mem::memcpy(&state.api.symbol, &address, rstd::usize(sizeof(address)));            \
    } while (false)

    WAVSEN_LOAD(pa_mainloop_api_once);
    WAVSEN_LOAD(pa_threaded_mainloop_free);
    WAVSEN_LOAD(pa_threaded_mainloop_get_api);
    WAVSEN_LOAD(pa_threaded_mainloop_in_thread);
    WAVSEN_LOAD(pa_threaded_mainloop_lock);
    WAVSEN_LOAD(pa_threaded_mainloop_new);
    WAVSEN_LOAD(pa_threaded_mainloop_signal);
    WAVSEN_LOAD(pa_threaded_mainloop_start);
    WAVSEN_LOAD(pa_threaded_mainloop_stop);
    WAVSEN_LOAD(pa_threaded_mainloop_unlock);
    WAVSEN_LOAD(pa_threaded_mainloop_wait);
    WAVSEN_LOAD(pa_context_connect);
    WAVSEN_LOAD(pa_context_disconnect);
    WAVSEN_LOAD(pa_context_errno);
    WAVSEN_LOAD(pa_context_get_server_info);
    WAVSEN_LOAD(pa_context_get_state);
    WAVSEN_LOAD(pa_context_new);
    WAVSEN_LOAD(pa_context_new_with_proplist);
    WAVSEN_LOAD(pa_context_set_state_callback);
    WAVSEN_LOAD(pa_context_unref);
    WAVSEN_LOAD(pa_operation_unref);
    WAVSEN_LOAD(pa_proplist_free);
    WAVSEN_LOAD(pa_proplist_new);
    WAVSEN_LOAD(pa_proplist_sets);
    WAVSEN_LOAD(pa_stream_begin_write);
    WAVSEN_LOAD(pa_stream_cancel_write);
    WAVSEN_LOAD(pa_stream_connect_playback);
    WAVSEN_LOAD(pa_stream_connect_record);
    WAVSEN_LOAD(pa_stream_cork);
    WAVSEN_LOAD(pa_stream_disconnect);
    WAVSEN_LOAD(pa_stream_drop);
    WAVSEN_LOAD(pa_stream_get_latency);
    WAVSEN_LOAD(pa_stream_get_state);
    WAVSEN_LOAD(pa_stream_get_time);
    WAVSEN_LOAD(pa_stream_get_timing_info);
    WAVSEN_LOAD(pa_stream_new);
    WAVSEN_LOAD(pa_stream_new_with_proplist);
    WAVSEN_LOAD(pa_stream_peek);
    WAVSEN_LOAD(pa_stream_readable_size);
    WAVSEN_LOAD(pa_stream_set_buffer_attr);
    WAVSEN_LOAD(pa_stream_set_read_callback);
    WAVSEN_LOAD(pa_stream_set_state_callback);
    WAVSEN_LOAD(pa_stream_set_write_callback);
    WAVSEN_LOAD(pa_stream_unref);
    WAVSEN_LOAD(pa_stream_update_timing_info);
    WAVSEN_LOAD(pa_stream_writable_size);
    WAVSEN_LOAD(pa_stream_write);
    WAVSEN_LOAD(pa_channel_map_init_stereo);
    WAVSEN_LOAD(pa_strerror);

#undef WAVSEN_LOAD
    state.loaded = true;
}

} // namespace

auto load() noexcept -> const Api* {
    auto state = g_state.lock().unwrap_unchecked();
    if (! state->attempted) load_once(*state);
    return state->loaded ? &state->api : nullptr;
}

auto load_error() noexcept -> rstd::ref<rstd::str> {
    auto state = g_state.lock().unwrap_unchecked();
    if (! state->attempted) load_once(*state);
    return state->error.as_str();
}

} // namespace wavsen::ffi::pulse
