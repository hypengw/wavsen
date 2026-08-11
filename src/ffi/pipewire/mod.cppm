module;

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

export module wavsen.ffi.pipewire;
export import rstd.core;

export namespace wavsen::ffi::pipewire
{

using ::pw_buffer;
using ::pw_direction;
using ::pw_loop;
using ::pw_main_loop;
using ::pw_properties;
using ::pw_stream;
using ::pw_stream_events;
using ::pw_stream_flags;
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
using ::spa_loop;
using ::spa_pod;
using ::spa_pod_builder;

inline constexpr auto direction_input  = ::PW_DIRECTION_INPUT;
inline constexpr auto direction_output = ::PW_DIRECTION_OUTPUT;
inline constexpr auto id_any           = PW_ID_ANY;

inline constexpr auto stream_flag_autoconnect = ::PW_STREAM_FLAG_AUTOCONNECT;
inline constexpr auto stream_flag_inactive    = ::PW_STREAM_FLAG_INACTIVE;
inline constexpr auto stream_flag_map_buffers = ::PW_STREAM_FLAG_MAP_BUFFERS;
inline constexpr auto stream_flag_rt_process  = ::PW_STREAM_FLAG_RT_PROCESS;

inline constexpr auto stream_state_error       = ::PW_STREAM_STATE_ERROR;
inline constexpr auto stream_state_unconnected = ::PW_STREAM_STATE_UNCONNECTED;
inline constexpr auto stream_state_connecting  = ::PW_STREAM_STATE_CONNECTING;
inline constexpr auto stream_state_paused      = ::PW_STREAM_STATE_PAUSED;
inline constexpr auto stream_state_streaming   = ::PW_STREAM_STATE_STREAMING;

inline constexpr auto version_stream_events = PW_VERSION_STREAM_EVENTS;
inline constexpr auto audio_format_f32_le   = ::SPA_AUDIO_FORMAT_F32_LE;
inline constexpr auto param_enum_format     = ::SPA_PARAM_EnumFormat;

inline constexpr const char* key_app_name            = PW_KEY_APP_NAME;
inline constexpr const char* key_app_id              = PW_KEY_APP_ID;
inline constexpr const char* key_media_type          = PW_KEY_MEDIA_TYPE;
inline constexpr const char* key_media_category      = PW_KEY_MEDIA_CATEGORY;
inline constexpr const char* key_media_role          = PW_KEY_MEDIA_ROLE;
inline constexpr const char* key_node_name           = PW_KEY_NODE_NAME;
inline constexpr const char* key_node_description    = PW_KEY_NODE_DESCRIPTION;
inline constexpr const char* key_node_latency        = PW_KEY_NODE_LATENCY;
inline constexpr const char* key_node_rate           = PW_KEY_NODE_RATE;
inline constexpr const char* key_stream_capture_sink = PW_KEY_STREAM_CAPTURE_SINK;

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

    decltype(&::pw_loop_invoke) pw_loop_invoke {};

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

auto initialize() -> const Api*;
auto load_error() noexcept -> rstd::ref<rstd::str>;
auto format_audio_raw_build(spa_pod_builder* builder, rstd::uint32_t id,
                            const spa_audio_info_raw* info) noexcept -> spa_pod*;

} // namespace wavsen::ffi::pipewire
