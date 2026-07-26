module;

module wavsen.audio;

import rstd.cppstd;
import rstd;
import rstd.log;
import pulse;
import wavsen.audio.gain;
import :core;

namespace wavsen::audio
{

using namespace rstd::prelude;

namespace pulse_ffi = wavsen::ffi::pulse;

namespace
{

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
        api_ = pulse_ffi::load();
        if (! api_) {
            rstd::log::error("wavsen::audio: failed to load PulseAudio: {}",
                             pulse_ffi::load_error());
            return;
        }

        loop_ = api_->pa_threaded_mainloop_new();
        if (! loop_) {
            rstd::log::error("wavsen::audio: pa_threaded_mainloop_new failed");
            return;
        }
        if (api_->pa_threaded_mainloop_start(loop_) < 0) {
            rstd::log::error("wavsen::audio: pa_threaded_mainloop_start failed");
            api_->pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return;
        }
    }

    ~Impl() {
        shutdown();
        wait_stopped();
        if (loop_) {
            api_->pa_threaded_mainloop_stop(loop_);
            api_->pa_threaded_mainloop_free(loop_);
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
        if (schedule) schedule_commands();
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
        if (schedule) schedule_commands();
        return true;
    }

    void schedule_commands() {
        api_->pa_threaded_mainloop_lock(loop_);
        api_->pa_mainloop_api_once(
            api_->pa_threaded_mainloop_get_api(loop_), &Impl::on_commands, this);
        api_->pa_threaded_mainloop_unlock(loop_);
    }

    static void on_commands(pulse_ffi::pa_mainloop_api* /*api*/, void* user) {
        static_cast<Impl*>(user)->drain_commands();
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

            for (auto& command : batch) {
                process(std::move(command));
            }
        }
    }

    void process(DeviceCommand command) {
        switch (command.kind) {
        case DeviceCommandKind::Apply: apply_desired(std::move(command.desired)); break;
        case DeviceCommandKind::Mount:
            if (command.stream_revision < stream_revision_) return;
            stream_revision_ = command.stream_revision;
            if (stream_ && api_->pa_stream_get_state(stream_) == pulse_ffi::stream_ready) {
                command.channel->pass_desc(desc_);
            }
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
        if (! ctx_) {
            start_context();
            return;
        }
        if (stream_ && api_->pa_stream_get_state(stream_) == pulse_ffi::stream_ready)
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

    void start_context() {
        const auto stream_name = desired_.identity.playback_stream_name();
        if (! stream_name) {
            fail("invalid audio client component");
            return;
        }

        auto* properties = api_->pa_proplist_new();
        if (! properties ||
            api_->pa_proplist_sets(properties,
                                   pulse_ffi::prop_application_name,
                                   desired_.identity.application_name.c_str()) < 0 ||
            api_->pa_proplist_sets(properties,
                                   pulse_ffi::prop_application_id,
                                   desired_.identity.application_id.c_str()) < 0) {
            if (properties) api_->pa_proplist_free(properties);
            fail("failed to build PulseAudio context properties");
            return;
        }

        ctx_ = api_->pa_context_new_with_proplist(api_->pa_threaded_mainloop_get_api(loop_),
                                                  desired_.identity.application_name.c_str(),
                                                  properties);
        api_->pa_proplist_free(properties);
        if (! ctx_) {
            fail("pa_context_new failed");
            return;
        }
        api_->pa_context_set_state_callback(ctx_, &Impl::on_context_state, this);
        if (api_->pa_context_connect(ctx_, nullptr, pulse_ffi::context_noflags, nullptr) < 0) {
            std::string error = "pa_context_connect failed: ";
            error += api_->pa_strerror(api_->pa_context_errno(ctx_));
            cleanup_device();
            fail(std::move(error));
            return;
        }
        emit_state(AudioDeviceState::Connecting);
    }

    void start_stream() {
        if (stream_ || ! ctx_ || ! desired_.active) return;
        const auto stream_name = desired_.identity.playback_stream_name();
        if (! stream_name) {
            fail("invalid audio client component");
            return;
        }

        pulse_ffi::pa_sample_spec sample_spec {};
        sample_spec.format   = pulse_ffi::sample_float32le;
        sample_spec.rate     = kDefaultRate;
        sample_spec.channels = static_cast<std::uint8_t>(kDefaultChannels);

        pulse_ffi::pa_channel_map channel_map {};
        api_->pa_channel_map_init_stereo(&channel_map);

        auto* properties = api_->pa_proplist_new();
        if (! properties ||
            api_->pa_proplist_sets(properties,
                                   pulse_ffi::prop_application_name,
                                   desired_.identity.application_name.c_str()) < 0 ||
            api_->pa_proplist_sets(properties,
                                   pulse_ffi::prop_application_id,
                                   desired_.identity.application_id.c_str()) < 0 ||
            api_->pa_proplist_sets(
                properties, pulse_ffi::prop_media_name, desired_.identity.media_name.c_str()) < 0 ||
            api_->pa_proplist_sets(
                properties, pulse_ffi::prop_media_role, desired_.identity.media_role.c_str()) < 0) {
            if (properties) api_->pa_proplist_free(properties);
            fail("failed to build PulseAudio stream properties");
            return;
        }

        stream_ = api_->pa_stream_new_with_proplist(
            ctx_, stream_name->c_str(), &sample_spec, &channel_map, properties);
        api_->pa_proplist_free(properties);
        if (! stream_) {
            std::string error = "pa_stream_new failed: ";
            error += api_->pa_strerror(api_->pa_context_errno(ctx_));
            fail(std::move(error));
            return;
        }

        api_->pa_stream_set_state_callback(stream_, &Impl::on_stream_state, this);
        api_->pa_stream_set_write_callback(stream_, &Impl::on_write, this);

        const auto frame_bytes = kDefaultChannels * static_cast<std::uint32_t>(sizeof(float));
        pulse_ffi::pa_buffer_attr buffer_attr {};
        buffer_attr.maxlength = static_cast<std::uint32_t>(-1);
        buffer_attr.tlength   = kQuantum * frame_bytes * 4;
        buffer_attr.prebuf    = static_cast<std::uint32_t>(-1);
        buffer_attr.minreq    = kQuantum * frame_bytes;
        buffer_attr.fragsize  = static_cast<std::uint32_t>(-1);

        const auto flags = static_cast<pulse_ffi::pa_stream_flags_t>(
            pulse_ffi::stream_adjust_latency | pulse_ffi::stream_auto_timing_update |
            pulse_ffi::stream_interpolate_timing | pulse_ffi::stream_start_corked);
        if (api_->pa_stream_connect_playback(
                stream_, nullptr, &buffer_attr, flags, nullptr, nullptr) < 0) {
            std::string error = "pa_stream_connect_playback failed: ";
            error += api_->pa_strerror(api_->pa_context_errno(ctx_));
            cleanup_stream();
            fail(std::move(error));
        }
    }

    void apply_playing() {
        if (! stream_ || api_->pa_stream_get_state(stream_) != pulse_ffi::stream_ready) return;
        auto* operation = api_->pa_stream_cork(stream_, desired_.playing ? 0 : 1, nullptr, nullptr);
        if (! operation) {
            std::string error = "pa_stream_cork failed: ";
            error += api_->pa_strerror(api_->pa_context_errno(ctx_));
            fail(std::move(error));
            return;
        }
        api_->pa_operation_unref(operation);
        emit_state(desired_.playing ? AudioDeviceState::ReadyPlaying
                                    : AudioDeviceState::ReadyPaused);
    }

    void cleanup_stream() {
        if (! stream_) return;
        api_->pa_stream_set_state_callback(stream_, nullptr, nullptr);
        api_->pa_stream_set_write_callback(stream_, nullptr, nullptr);
        api_->pa_stream_disconnect(stream_);
        api_->pa_stream_unref(stream_);
        stream_ = nullptr;
        stream_position_frames_.store(u64(), std::memory_order_relaxed);
    }

    void cleanup_device() {
        cleanup_stream();
        if (! ctx_) return;
        api_->pa_context_set_state_callback(ctx_, nullptr, nullptr);
        api_->pa_context_disconnect(ctx_);
        api_->pa_context_unref(ctx_);
        ctx_ = nullptr;
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

    static void on_context_state(pulse_ffi::pa_context* context, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (context != self->ctx_) return;
        switch (self->api_->pa_context_get_state(context)) {
        case pulse_ffi::context_ready: self->start_stream(); break;
        case pulse_ffi::context_failed: {
            std::string error = "PulseAudio context failed: ";
            error += self->api_->pa_strerror(self->api_->pa_context_errno(context));
            self->fail(std::move(error));
            break;
        }
        case pulse_ffi::context_terminated:
            if (! self->shutting_down_ && self->desired_.active)
                self->fail("PulseAudio context terminated");
            break;
        default: break;
        }
    }

    static void on_stream_state(pulse_ffi::pa_stream* stream, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (stream != self->stream_) return;
        switch (self->api_->pa_stream_get_state(stream)) {
        case pulse_ffi::stream_ready:
            for (auto& channel : self->channels_) channel->pass_desc(self->desc_);
            self->apply_playing();
            rstd::log::info("wavsen::audio: pulse device ready ({} ch @ {} Hz)",
                            self->desc_.channels,
                            self->desc_.sample_rate);
            break;
        case pulse_ffi::stream_failed: {
            std::string error = "PulseAudio stream failed: ";
            error += self->api_->pa_strerror(self->api_->pa_context_errno(self->ctx_));
            self->fail(std::move(error));
            break;
        }
        case pulse_ffi::stream_terminated:
            if (! self->shutting_down_ && self->desired_.active)
                self->fail("PulseAudio stream terminated");
            break;
        default: break;
        }
    }

    static void on_write(pulse_ffi::pa_stream* stream, size_t bytes, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (bytes == 0 || self->desc_.channels == u32()) return;

        pulse_ffi::pa_usec_t usec = 0;
        if (self->api_->pa_stream_get_time(stream, &usec) >= 0) {
            self->stream_position_frames_.store(
                u64(usec) * u64(self->desc_.sample_rate.to_primitive()) / u64(1'000'000),
                std::memory_order_relaxed);
        }

        const auto channel_count = self->desc_.channels.to_primitive();
        const auto stride        = channel_count * static_cast<std::uint32_t>(sizeof(float));
        size_t     remaining     = bytes;
        while (remaining > 0) {
            void*  buffer = nullptr;
            size_t wanted = remaining;
            if (self->api_->pa_stream_begin_write(stream, &buffer, &wanted) < 0 || ! buffer ||
                wanted == 0) {
                return;
            }

            const auto frames       = static_cast<std::uint32_t>(wanted / stride);
            const auto sample_count = static_cast<std::size_t>(frames) * channel_count;
            auto*      output       = static_cast<float*>(buffer);
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

            const size_t written = static_cast<size_t>(frames) * stride;
            if (self->api_->pa_stream_write(
                    stream, buffer, written, nullptr, 0, pulse_ffi::seek_relative) < 0) {
                return;
            }
            if (written == 0) return;
            remaining = remaining > written ? remaining - written : 0;
        }
    }

    const pulse_ffi::Api*            api_    = nullptr;
    pulse_ffi::pa_threaded_mainloop* loop_   = nullptr;
    pulse_ffi::pa_context*           ctx_    = nullptr;
    pulse_ffi::pa_stream*            stream_ = nullptr;

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
