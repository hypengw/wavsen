module;

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

module wavsen.audio;

import rstd.cppstd;
import rstd;
import rstd.log;
import pipewire;
import wavsen.audio.gain;
import :core;

namespace wavsen::audio
{

using namespace rstd::prelude;

namespace pipewire_ffi = wavsen::ffi::pipewire;

namespace
{

std::once_flag g_pw_init_once;

void ensure_pw_init(const pipewire_ffi::Api& api) {
    std::call_once(g_pw_init_once, [&api] {
        api.pw_init(nullptr, nullptr);
    });
}

constexpr std::uint32_t kDefaultRate     = 48000;
constexpr std::uint32_t kDefaultChannels = 2;
constexpr std::uint32_t kQuantum         = 1024;

enum class DeviceCommandKind : std::uint8_t
{
    Apply,
    Mount,
    UnmountAll,
    Shutdown,
};

struct DeviceCommand {
    DeviceCommandKind             kind { DeviceCommandKind::Apply };
    AudioDeviceDesiredState       desired;
    std::unique_ptr<IPullChannel> channel;
    u64                           stream_revision;
};

struct CommandQueue {
    rstd::prelude::Vec<DeviceCommand> pending;
    bool                              scheduled {};
    bool                              accepting { true };
    bool                              shutdown_queued {};
};

} // namespace

class AudioDevice::Impl {
public:
    Impl()
        : commands_(CommandQueue {}),
          event_sink_(AudioDeviceEventSink {}),
          shutdown_complete_(false),
          shutdown_cv_(rstd::sync::Condvar::make()) {
        api_ = pipewire_ffi::load();
        if (! api_) {
            rstd::log::error("wavsen::audio: failed to load PipeWire: {}",
                             pipewire_ffi::load_error());
            return;
        }
        ensure_pw_init(*api_);

        loop_ = api_->pw_thread_loop_new("wavsen-audio", nullptr);
        if (! loop_) {
            rstd::log::error("wavsen::audio: pw_thread_loop_new failed");
            return;
        }
        if (api_->pw_thread_loop_start(loop_) < 0) {
            rstd::log::error("wavsen::audio: pw_thread_loop_start failed");
            api_->pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
    }

    ~Impl() {
        shutdown();
        wait_stopped();
        if (loop_) {
            api_->pw_thread_loop_stop(loop_);
            api_->pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
    }

    void set_event_sink(AudioDeviceEventSink sink) {
        auto current = event_sink_.lock().unwrap_unchecked();
        *current     = std::move(sink);
    }

    bool apply(AudioDeviceDesiredState desired) {
        return enqueue(DeviceCommand {
            .kind    = DeviceCommandKind::Apply,
            .desired = std::move(desired),
        });
    }

    bool mount(std::unique_ptr<IPullChannel> channel, u64 stream_revision) {
        if (! channel) return false;
        return enqueue(DeviceCommand {
            .kind            = DeviceCommandKind::Mount,
            .channel         = std::move(channel),
            .stream_revision = stream_revision,
        });
    }

    bool unmount_all(u64 stream_revision) {
        return enqueue(DeviceCommand {
            .kind            = DeviceCommandKind::UnmountAll,
            .stream_revision = stream_revision,
        });
    }

    void shutdown() {
        if (! loop_) {
            mark_shutdown_complete();
            return;
        }

        bool schedule = false;
        {
            auto commands = commands_.lock().unwrap_unchecked();
            if (commands->shutdown_queued) return;
            commands->shutdown_queued = true;
            commands->accepting       = false;
            commands->pending.push(DeviceCommand { .kind = DeviceCommandKind::Shutdown });
            if (! commands->scheduled) {
                commands->scheduled = true;
                schedule            = true;
            }
        }
        if (schedule && ! schedule_commands()) mark_shutdown_complete();
    }

    void wait_stopped() {
        if (! loop_) return;
        auto complete = shutdown_complete_.lock().unwrap_unchecked();
        shutdown_cv_.wait_while(complete, [](bool value) {
            return ! value;
        });
    }

    AudioDeviceState state() const { return state_.load(std::memory_order_acquire); }
    DeviceDesc       desc() const { return desc_; }
    auto             stream_position_frames() const -> u64 {
        return stream_position_frames_.load(std::memory_order_relaxed);
    }

private:
    bool enqueue(DeviceCommand command) {
        if (! loop_) return false;

        bool schedule = false;
        {
            auto commands = commands_.lock().unwrap_unchecked();
            if (! commands->accepting) return false;
            commands->pending.push(std::move(command));
            if (! commands->scheduled) {
                commands->scheduled = true;
                schedule            = true;
            }
        }
        return ! schedule || schedule_commands();
    }

    bool schedule_commands() {
        const int result = api_->pw_loop_invoke(
            api_->pw_thread_loop_get_loop(loop_), &Impl::on_commands, 0, nullptr, 0, false, this);
        if (result >= 0) return true;

        auto commands       = commands_.lock().unwrap_unchecked();
        commands->scheduled = false;
        rstd::log::error("wavsen::audio: pw_loop_invoke failed: {}", result);
        return false;
    }

    static int on_commands(::spa_loop* /*loop*/, bool /*async*/, std::uint32_t /*seq*/,
                           const void* /*data*/, std::size_t /*size*/, void* user) {
        static_cast<Impl*>(user)->drain_commands();
        return 0;
    }

    void drain_commands() {
        for (;;) {
            rstd::prelude::Vec<DeviceCommand> batch;
            {
                auto commands = commands_.lock().unwrap_unchecked();
                if (commands->pending.is_empty()) {
                    commands->scheduled = false;
                    return;
                }
                batch             = std::move(commands->pending);
                commands->pending = rstd::prelude::Vec<DeviceCommand>();
            }

            for (auto& command : batch) process(std::move(command));
        }
    }

    void process(DeviceCommand command) {
        switch (command.kind) {
        case DeviceCommandKind::Apply: apply_desired(std::move(command.desired)); break;
        case DeviceCommandKind::Mount:
            if (command.stream_revision < stream_revision_) return;
            stream_revision_ = command.stream_revision;
            if (stream_) command.channel->pass_desc(desc_);
            channels_.push_back(std::move(command.channel));
            break;
        case DeviceCommandKind::UnmountAll:
            if (command.stream_revision < stream_revision_) return;
            stream_revision_ = command.stream_revision;
            channels_.clear();
            break;
        case DeviceCommandKind::Shutdown:
            shutting_down_ = true;
            cleanup_device();
            channels_.clear();
            emit_state(AudioDeviceState::Stopped);
            mark_shutdown_complete();
            break;
        }
    }

    void apply_desired(AudioDeviceDesiredState desired) {
        if (shutting_down_) return;

        const bool generation_changed = desired.generation != desired_.generation;
        if (generation_changed) cleanup_device();
        desired_ = std::move(desired);
        apply_gain_state();

        if (! desired_.active) {
            cleanup_device();
            emit_state(AudioDeviceState::Idle);
            return;
        }
        if (! stream_) {
            start_stream();
            return;
        }
        apply_playing();
    }

    void apply_gain_state() {
        volume_ = desired_.volume;
        muted_  = desired_.muted;
        if (desired_.volume_scale_revision == volume_scale_revision_) return;

        volume_scale_revision_ = desired_.volume_scale_revision;
        volume_scale_.redirect(
            desired_.volume_scale, desc_.sample_rate, desired_.volume_scale_fade_ms);
    }

    void start_stream() {
        const auto stream_name = desired_.identity.playback_stream_name();
        if (! stream_name) {
            fail("invalid audio client component");
            return;
        }

        static const ::pw_stream_events stream_events = {
            .version       = PW_VERSION_STREAM_EVENTS,
            .destroy       = nullptr,
            .state_changed = &Impl::on_state_changed,
            .control_info  = nullptr,
            .io_changed    = nullptr,
            .param_changed = nullptr,
            .add_buffer    = nullptr,
            .remove_buffer = nullptr,
            .process       = &Impl::on_process,
            .drained       = nullptr,
            .command       = nullptr,
            .trigger_done  = nullptr,
        };

        auto* properties = api_->pw_properties_new(PW_KEY_MEDIA_TYPE,
                                                   "Audio",
                                                   PW_KEY_MEDIA_CATEGORY,
                                                   "Playback",
                                                   PW_KEY_MEDIA_ROLE,
                                                   desired_.identity.media_role.c_str(),
                                                   PW_KEY_APP_NAME,
                                                   desired_.identity.application_name.c_str(),
                                                   PW_KEY_APP_ID,
                                                   desired_.identity.application_id.c_str(),
                                                   PW_KEY_NODE_NAME,
                                                   stream_name->c_str(),
                                                   PW_KEY_NODE_DESCRIPTION,
                                                   desired_.identity.media_name.c_str(),
                                                   nullptr);
        if (! properties) {
            fail("pw_properties_new failed");
            return;
        }
        api_->pw_properties_setf(properties, PW_KEY_NODE_LATENCY, "%u/%u", kQuantum, kDefaultRate);
        api_->pw_properties_setf(properties, PW_KEY_NODE_RATE, "1/%u", kDefaultRate);

        stream_ = api_->pw_stream_new_simple(api_->pw_thread_loop_get_loop(loop_),
                                             stream_name->c_str(),
                                             properties,
                                             &stream_events,
                                             this);
        if (! stream_) {
            fail("pw_stream_new_simple failed");
            return;
        }

        std::uint8_t    pod_buffer[1024];
        spa_pod_builder builder {};
        builder.data = pod_buffer;
        builder.size = sizeof(pod_buffer);

        spa_audio_info_raw info {};
        info.format   = SPA_AUDIO_FORMAT_F32_LE;
        info.rate     = kDefaultRate;
        info.channels = kDefaultChannels;

        const spa_pod* params[] = {
            spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info),
        };
        const auto flags =
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                         PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_INACTIVE);
        if (api_->pw_stream_connect(stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1) <
            0) {
            cleanup_device();
            fail("pw_stream_connect failed");
            return;
        }

        for (auto& channel : channels_) channel->pass_desc(desc_);
        emit_state(AudioDeviceState::Connecting);
    }

    void apply_playing() {
        if (! stream_) return;
        if (api_->pw_stream_set_active(stream_, desired_.playing) < 0) {
            fail("pw_stream_set_active failed");
        }
    }

    void cleanup_device() {
        if (! stream_) return;
        api_->pw_stream_destroy(stream_);
        stream_ = nullptr;
        stream_position_frames_.store(u64(), std::memory_order_relaxed);
    }

    void fail(std::string error) {
        rstd::log::error("wavsen::audio: {}", error);
        emit_state(AudioDeviceState::Failed, std::move(error));
    }

    void emit_state(AudioDeviceState state, std::string error = {}) {
        state_.store(state, std::memory_order_release);
        AudioDeviceEventSink sink;
        {
            auto current = event_sink_.lock().unwrap_unchecked();
            sink         = *current;
        }
        if (sink) sink(AudioDeviceEvent { desired_.generation, state, std::move(error) });
    }

    void mark_shutdown_complete() {
        auto complete = shutdown_complete_.lock().unwrap_unchecked();
        if (*complete) return;
        *complete = true;
        shutdown_cv_.notify_all();
    }

    void apply_output_gain(float* output, std::uint32_t frames) {
        volume_scale_.apply(
            rstd::mut_ref<float[]>::from_raw_parts(
                output, rstd::usize(frames) * rstd::usize(desc_.channels.to_primitive())),
            desc_.channels,
            volume_);
    }

    static void on_process(void* user) {
        auto* self = static_cast<Impl*>(user);
        if (! self->stream_) return;

        pw_time time {};
        if (self->api_->pw_stream_get_time_n(self->stream_, &time, sizeof(time)) >= 0 &&
            time.ticks > static_cast<std::uint64_t>(time.delay)) {
            self->stream_position_frames_.store(
                u64(time.ticks - static_cast<std::uint64_t>(time.delay)),
                std::memory_order_relaxed);
        }

        pw_buffer* buffer = self->api_->pw_stream_dequeue_buffer(self->stream_);
        if (! buffer) return;
        auto* spa_buffer = buffer->buffer;
        if (! spa_buffer || spa_buffer->n_datas == 0 || ! spa_buffer->datas[0].data) {
            self->api_->pw_stream_queue_buffer(self->stream_, buffer);
            return;
        }

        const auto    channel_count = self->desc_.channels.to_primitive();
        const auto    stride        = channel_count * static_cast<std::uint32_t>(sizeof(float));
        auto*         output        = static_cast<float*>(spa_buffer->datas[0].data);
        std::uint32_t frames        = spa_buffer->datas[0].maxsize / stride;
        if (buffer->requested != 0) {
            const auto requested = static_cast<std::uint32_t>(buffer->requested);
            if (requested < frames) frames = requested;
        }

        const auto sample_count = static_cast<std::size_t>(frames) * channel_count;
        std::memset(output, 0, sample_count * sizeof(float));
        if (! self->muted_) {
            self->scratch_.resize(sample_count);
            for (auto& channel : self->channels_) {
                std::memset(self->scratch_.data(), 0, sample_count * sizeof(float));
                const auto produced = channel->next_pcm(self->scratch_.data(), u32(frames));
                const auto produced_samples =
                    static_cast<std::size_t>(produced.to_primitive()) * channel_count;
                for (std::size_t index = 0; index < produced_samples; ++index) {
                    output[index] += self->scratch_[index];
                }
            }
            self->apply_output_gain(output, frames);
        }

        spa_buffer->datas[0].chunk->offset = 0;
        spa_buffer->datas[0].chunk->stride = static_cast<std::int32_t>(stride);
        spa_buffer->datas[0].chunk->size   = frames * stride;
        self->api_->pw_stream_queue_buffer(self->stream_, buffer);
    }

    static void on_state_changed(void*             user, ::pw_stream_state /*old_state*/,
                                 ::pw_stream_state state, const char* error) {
        auto* self = static_cast<Impl*>(user);
        switch (state) {
        case PW_STREAM_STATE_ERROR:
            self->fail(error ? std::string(error) : std::string("PipeWire stream failed"));
            break;
        case PW_STREAM_STATE_CONNECTING: self->emit_state(AudioDeviceState::Connecting); break;
        case PW_STREAM_STATE_PAUSED:
            if (! self->desired_.active) break;
            if (self->desired_.playing)
                self->apply_playing();
            else
                self->emit_state(AudioDeviceState::ReadyPaused);
            break;
        case PW_STREAM_STATE_STREAMING:
            if (! self->desired_.active) break;
            if (! self->desired_.playing)
                self->apply_playing();
            else
                self->emit_state(AudioDeviceState::ReadyPlaying);
            break;
        case PW_STREAM_STATE_UNCONNECTED: break;
        }
    }

    const pipewire_ffi::Api* api_    = nullptr;
    ::pw_thread_loop*        loop_   = nullptr;
    ::pw_stream*             stream_ = nullptr;

    AudioDeviceDesiredState desired_;
    DeviceDesc              desc_ { u32(kDefaultChannels), u32(kDefaultRate) };
    u64                     stream_revision_;
    u64                     volume_scale_revision_;
    bool                    shutting_down_ {};

    std::vector<std::unique_ptr<IPullChannel>> channels_;
    std::vector<float>                         scratch_;

    f32                     volume_ { f32(1.0f) };
    detail::VolumeScaleRamp volume_scale_;
    bool                    muted_ {};

    rstd::sync::Mutex<CommandQueue>         commands_;
    rstd::sync::Mutex<AudioDeviceEventSink> event_sink_;
    rstd::sync::Mutex<bool>                 shutdown_complete_;
    rstd::sync::Condvar                     shutdown_cv_;
    std::atomic<AudioDeviceState>           state_ { AudioDeviceState::Idle };
    std::atomic<u64>                        stream_position_frames_ { u64() };
};

AudioDevice::AudioDevice(): impl_(std::make_unique<Impl>()) {}
AudioDevice::~AudioDevice() = default;

void AudioDevice::set_event_sink(AudioDeviceEventSink sink) {
    impl_->set_event_sink(std::move(sink));
}
bool AudioDevice::apply(AudioDeviceDesiredState desired) {
    return impl_->apply(std::move(desired));
}
bool AudioDevice::mount(std::unique_ptr<IPullChannel> channel, u64 stream_revision) {
    return impl_->mount(std::move(channel), stream_revision);
}
bool AudioDevice::unmount_all(u64 stream_revision) { return impl_->unmount_all(stream_revision); }
void AudioDevice::shutdown() { impl_->shutdown(); }
void AudioDevice::wait_stopped() { impl_->wait_stopped(); }
AudioDeviceState AudioDevice::state() const { return impl_->state(); }
DeviceDesc       AudioDevice::desc() const { return impl_->desc(); }
auto AudioDevice::stream_position_frames() const -> u64 { return impl_->stream_position_frames(); }

} // namespace wavsen::audio
