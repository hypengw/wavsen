module wavsen.audio.capture;

import rstd;
import rstd.log;
import pipewire;
import wavsen.audio.capture_window;

using namespace rstd::prelude;

namespace wavsen::audio
{

namespace pipewire_ffi = wavsen::ffi::pipewire;

namespace
{

constexpr rstd::uint32_t kDefaultRate     = 48000;
constexpr rstd::uint32_t kDefaultChannels = 2;
constexpr rstd::uint32_t kQuantum         = 1024;

} // namespace

class AudioCapture::Impl {
public:
    ~Impl() { uninit(); }

    bool init() {
        if (is_inited()) return true;
        if (loop_) uninit();

        api_ = pipewire_ffi::initialize();
        if (! api_) {
            rstd::log::error("wavsen::audio: capture failed to load PipeWire: {}",
                             pipewire_ffi::load_error());
            return false;
        }
        loop_ = api_->pw_thread_loop_new("wavsen-capture", nullptr);
        if (! loop_) {
            rstd::log::error("wavsen::audio: capture pw_thread_loop_new failed");
            return false;
        }
        if (api_->pw_thread_loop_start(loop_) < 0) {
            rstd::log::error("wavsen::audio: capture pw_thread_loop_start failed");
            api_->pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
            return false;
        }

        static const pipewire_ffi::pw_stream_events stream_events = {
            .version       = pipewire_ffi::version_stream_events,
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

        api_->pw_thread_loop_lock(loop_);

        auto* props = api_->pw_properties_new(pipewire_ffi::key_media_type,
                                              "Audio",
                                              pipewire_ffi::key_media_category,
                                              "Capture",
                                              pipewire_ffi::key_media_role,
                                              "Music",
                                              pipewire_ffi::key_app_name,
                                              "wavsen",
                                              pipewire_ffi::key_node_name,
                                              "wavsen-capture",
                                              pipewire_ffi::key_node_description,
                                              "wavsen audio response capture",
                                              pipewire_ffi::key_stream_capture_sink,
                                              "true",
                                              nullptr);
        api_->pw_properties_setf(
            props, pipewire_ffi::key_node_latency, "%u/%u", kQuantum, kDefaultRate);

        stream_ = api_->pw_stream_new_simple(
            api_->pw_thread_loop_get_loop(loop_), "wavsen-capture", props, &stream_events, this);
        if (! stream_) {
            api_->pw_thread_loop_unlock(loop_);
            rstd::log::error("wavsen::audio: capture pw_stream_new_simple failed");
            api_->pw_thread_loop_stop(loop_);
            api_->pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
            return false;
        }

        rstd::uint8_t                 pod_buffer[1024];
        pipewire_ffi::spa_pod_builder b {};
        b.data = pod_buffer;
        b.size = sizeof(pod_buffer);

        pipewire_ffi::spa_audio_info_raw info {};
        info.format   = pipewire_ffi::audio_format_f32_le;
        info.rate     = kDefaultRate;
        info.channels = kDefaultChannels;

        const pipewire_ffi::spa_pod* params[1];
        params[0] =
            pipewire_ffi::format_audio_raw_build(&b, pipewire_ffi::param_enum_format, &info);

        const auto flags = static_cast<pipewire_ffi::pw_stream_flags>(
            pipewire_ffi::stream_flag_autoconnect | pipewire_ffi::stream_flag_map_buffers |
            pipewire_ffi::stream_flag_rt_process);

        publisher_.restart();
        if (api_->pw_stream_connect(
                stream_, pipewire_ffi::direction_input, pipewire_ffi::id_any, flags, params, 1) <
            0) {
            rstd::log::error("wavsen::audio: capture pw_stream_connect failed");
            api_->pw_stream_destroy(stream_);
            stream_ = nullptr;
            api_->pw_thread_loop_unlock(loop_);
            api_->pw_thread_loop_stop(loop_);
            api_->pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
            return false;
        }

        api_->pw_thread_loop_unlock(loop_);

        rstd::log::info("wavsen::audio: capture inited (monitor sink, "
                        "{} ch @ {} Hz)",
                        kDefaultChannels,
                        kDefaultRate);
        return true;
    }

    void uninit() {
        if (stream_) {
            api_->pw_thread_loop_lock(loop_);
            api_->pw_stream_destroy(stream_);
            stream_ = nullptr;
            api_->pw_thread_loop_unlock(loop_);
        }
        if (loop_) {
            api_->pw_thread_loop_stop(loop_);
            api_->pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
    }

    bool is_inited() const {
        return loop_ != nullptr && stream_ != nullptr &&
               ! stream_failed_.load(rstd::sync::atomic::Ordering::Acquire);
    }

    bool snapshot(AudioPcmWindow& out) {
        AudioPcmWindow candidate {};
        if (! publisher_.snapshot(candidate)) return false;
        if (candidate.generation == last_generation_ && candidate.sequence == last_sequence_) {
            return false;
        }
        last_generation_ = candidate.generation;
        last_sequence_   = candidate.sequence;
        out              = candidate;
        return true;
    }

private:
    static void on_process(void* user) {
        auto* self = static_cast<Impl*>(user);
        if (! self->stream_) return;

        pipewire_ffi::pw_buffer* b = self->api_->pw_stream_dequeue_buffer(self->stream_);
        if (! b) return;

        auto* sb = b->buffer;
        if (! sb || sb->n_datas == 0 || ! sb->datas[0].data) {
            self->publisher_.restart();
            self->api_->pw_stream_queue_buffer(self->stream_, b);
            return;
        }

        auto&      d        = sb->datas[0];
        const auto stride   = d.chunk->stride > 0
                                  ? static_cast<rstd::uint32_t>(d.chunk->stride)
                                  : kDefaultChannels * static_cast<rstd::uint32_t>(sizeof(float));
        const auto channels = stride / static_cast<rstd::uint32_t>(sizeof(float));
        if (channels == 0 || stride % static_cast<rstd::uint32_t>(sizeof(float)) != 0) {
            self->publisher_.restart();
            self->api_->pw_stream_queue_buffer(self->stream_, b);
            return;
        }
        const rstd::uint32_t offset = d.chunk->offset % d.maxsize;
        const rstd::uint32_t bytes  = rstd::cmp::min(d.chunk->size, d.maxsize - offset);
        const auto*          src =
            reinterpret_cast<const float*>(static_cast<const rstd::uint8_t*>(d.data) + offset);
        const rstd::uint32_t n_frames = bytes / stride;

        self->ingest(src, n_frames, channels);

        self->api_->pw_stream_queue_buffer(self->stream_, b);
    }

    static void on_state_changed(void* user, pipewire_ffi::pw_stream_state /*old*/,
                                 pipewire_ffi::pw_stream_state state, const char* error) {
        auto* self = static_cast<Impl*>(user);
        switch (state) {
        case pipewire_ffi::stream_state_error:
            self->stream_failed_.store(true, rstd::sync::atomic::Ordering::Release);
            self->publisher_.restart();
            rstd::log::error("wavsen::audio: capture stream ERROR{}",
                             error ? rstd::format(": {}", error) : String {});
            break;
        case pipewire_ffi::stream_state_unconnected:
            self->stream_failed_.store(true, rstd::sync::atomic::Ordering::Release);
            self->publisher_.restart();
            rstd::log::debug("wavsen::audio: capture stream UNCONNECTED");
            break;
        case pipewire_ffi::stream_state_connecting:
            rstd::log::debug("wavsen::audio: capture stream CONNECTING");
            break;
        case pipewire_ffi::stream_state_paused:
            rstd::log::debug("wavsen::audio: capture stream PAUSED");
            break;
        case pipewire_ffi::stream_state_streaming:
            self->stream_failed_.store(false, rstd::sync::atomic::Ordering::Release);
            rstd::log::debug("wavsen::audio: capture stream STREAMING");
            break;
        }
    }

    void ingest(const float* src, rstd::uint32_t n_frames, rstd::uint32_t channels) {
        publisher_.ingest(src, n_frames, channels);
    }

    const pipewire_ffi::Api*      api_    = nullptr;
    pipewire_ffi::pw_thread_loop* loop_   = nullptr;
    pipewire_ffi::pw_stream*      stream_ = nullptr;

    capture::PcmWindowPublisher      publisher_;
    rstd::uint64_t                   last_generation_ = 0;
    rstd::uint64_t                   last_sequence_   = 0;
    rstd::sync::atomic::Atomic<bool> stream_failed_ { false };
};

AudioCapture::AudioCapture(): impl_(Box<Impl>::make()) {}
AudioCapture::~AudioCapture() = default;

bool AudioCapture::init() { return impl_->init(); }
void AudioCapture::uninit() { impl_->uninit(); }
bool AudioCapture::is_inited() const { return impl_->is_inited(); }
bool AudioCapture::snapshot(AudioPcmWindow& out) { return impl_->snapshot(out); }

} // namespace wavsen::audio
