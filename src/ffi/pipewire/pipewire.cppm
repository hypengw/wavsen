module;

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

export module pipewire;
export import rstd.core;

export {
    using ::pw_buffer;
    using ::pw_direction;
    using ::pw_loop;
    using ::pw_main_loop;
    using ::pw_properties;
    using ::pw_stream;
    using ::pw_stream_events;
    using ::pw_stream_state;
    using ::pw_thread_loop;
    using ::pw_time;

    using ::spa_audio_channel;
    using ::spa_audio_format;
    using ::spa_audio_info_raw;
    using ::spa_buffer;
    using ::spa_chunk;
    using ::spa_data;
    using ::spa_hook;
    using ::spa_pod;
    using ::spa_pod_builder;

    using ::PW_DIRECTION_INPUT;
    using ::PW_DIRECTION_OUTPUT;
    using ::SPA_AUDIO_FORMAT_F32_LE;
}

export namespace wavsen::ffi::pipewire
{

using ::pw_buffer;
using ::pw_direction;
using ::pw_loop;
using ::pw_main_loop;
using ::pw_properties;
using ::pw_stream;
using ::pw_stream_events;
using ::pw_stream_state;
using ::pw_thread_loop;
using ::pw_time;

using ::spa_audio_channel;
using ::spa_audio_format;
using ::spa_audio_info_raw;
using ::spa_buffer;
using ::spa_chunk;
using ::spa_data;
using ::spa_hook;
using ::spa_pod;
using ::spa_pod_builder;

struct Api {
    decltype(&::pw_deinit) pw_deinit {};
    decltype(&::pw_init)   pw_init {};

    decltype(&::pw_thread_loop_destroy)  pw_thread_loop_destroy {};
    decltype(&::pw_thread_loop_get_loop) pw_thread_loop_get_loop {};
    decltype(&::pw_thread_loop_lock)     pw_thread_loop_lock {};
    decltype(&::pw_thread_loop_new)      pw_thread_loop_new {};
    decltype(&::pw_thread_loop_start)    pw_thread_loop_start {};
    decltype(&::pw_thread_loop_stop)     pw_thread_loop_stop {};
    decltype(&::pw_thread_loop_unlock)   pw_thread_loop_unlock {};

    decltype(&::pw_properties_new)  pw_properties_new {};
    decltype(&::pw_properties_set)  pw_properties_set {};
    decltype(&::pw_properties_setf) pw_properties_setf {};

    decltype(&::pw_stream_connect)         pw_stream_connect {};
    decltype(&::pw_stream_dequeue_buffer)  pw_stream_dequeue_buffer {};
    decltype(&::pw_stream_destroy)         pw_stream_destroy {};
    decltype(&::pw_stream_disconnect)      pw_stream_disconnect {};
    decltype(&::pw_stream_get_time_n)      pw_stream_get_time_n {};
    decltype(&::pw_stream_new_simple)      pw_stream_new_simple {};
    decltype(&::pw_stream_queue_buffer)    pw_stream_queue_buffer {};
    decltype(&::pw_stream_set_active)      pw_stream_set_active {};
    decltype(&::pw_stream_state_as_string) pw_stream_state_as_string {};
};

auto load() noexcept -> const Api*;
auto load_error() noexcept -> rstd::ref<rstd::str>;

} // namespace wavsen::ffi::pipewire
