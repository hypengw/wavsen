module;

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

module pipewire;

import rstd;
import rstd.dlopn;

namespace wavsen::ffi::pipewire
{
namespace
{

constexpr const char* kSoname = "libpipewire-0.3.so.0";

struct LoadState {
    bool                               attempted {};
    bool                               loaded {};
    rstd::Option<rstd::dlopn::Library> library {};
    Api                                api {};
    rstd::string::String               error {};
};

rstd::sync::Mutex<LoadState> g_state { LoadState {} };

void load_once(LoadState& state) {
    state.attempted = true;
    auto library    = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr(kSoname));
    if (library.is_err()) {
        auto error  = rstd::move(library).unwrap_err_unchecked();
        state.error = rstd::string::String::make(error.message());
        return;
    }
    state.library.insert(rstd::move(library).unwrap_unchecked());

#define WAVSEN_LOAD(name)                                                                      \
    do {                                                                                       \
        auto loaded =                                                                          \
            state.library->symbol<decltype(state.api.name)>(rstd::ffi::CStr::from_ptr(#name)); \
        if (loaded.is_err()) {                                                                 \
            auto error = rstd::move(loaded).unwrap_err_unchecked();                            \
            state.error =                                                                      \
                rstd::format("missing symbol {} in {}: {}", #name, kSoname, error.message());  \
            return;                                                                            \
        }                                                                                      \
        state.api.name = rstd::move(loaded).unwrap_unchecked();                                \
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
    WAVSEN_LOAD(pw_loop_invoke);
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

auto format_audio_raw_build(spa_pod_builder* builder, rstd::uint32_t id,
                            const spa_audio_info_raw* info) noexcept -> spa_pod* {
    return spa_format_audio_raw_build(builder, id, info);
}

} // namespace wavsen::ffi::pipewire
