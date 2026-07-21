module;

#include <pipewire/pipewire.h>

#include <dlfcn.h>

module pipewire;

import rstd;

namespace wavsen::ffi::pipewire
{
namespace
{

constexpr const char* kSoname = "libpipewire-0.3.so.0";

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
        state.error       = rstd::string::String::make(error ? error : "unknown error");
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

    WAVSEN_LOAD(pw_deinit);
    WAVSEN_LOAD(pw_init);
    WAVSEN_LOAD(pw_thread_loop_destroy);
    WAVSEN_LOAD(pw_thread_loop_get_loop);
    WAVSEN_LOAD(pw_thread_loop_lock);
    WAVSEN_LOAD(pw_thread_loop_new);
    WAVSEN_LOAD(pw_thread_loop_start);
    WAVSEN_LOAD(pw_thread_loop_stop);
    WAVSEN_LOAD(pw_thread_loop_unlock);
    WAVSEN_LOAD(pw_properties_new);
    WAVSEN_LOAD(pw_properties_set);
    WAVSEN_LOAD(pw_properties_setf);
    WAVSEN_LOAD(pw_stream_connect);
    WAVSEN_LOAD(pw_stream_dequeue_buffer);
    WAVSEN_LOAD(pw_stream_destroy);
    WAVSEN_LOAD(pw_stream_disconnect);
    WAVSEN_LOAD(pw_stream_get_time_n);
    WAVSEN_LOAD(pw_stream_new_simple);
    WAVSEN_LOAD(pw_stream_queue_buffer);
    WAVSEN_LOAD(pw_stream_set_active);
    WAVSEN_LOAD(pw_stream_state_as_string);

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

} // namespace wavsen::ffi::pipewire
