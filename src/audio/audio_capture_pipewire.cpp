module;

#include <chrono>
#include <complex>

module wavsen.audio;

import rstd;
import rstd.log;
import pipewire;
import :capture;
import wavsen.audio.capture_dsp;

namespace wavsen::audio
{

using namespace rstd::prelude;

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

    bool is_inited() const { return loop_ != nullptr && stream_ != nullptr; }

    bool snapshot(AudioSpectrum& out) const {
        for (int attempt = 0; attempt < 16; ++attempt) {
            const rstd::uint32_t s1 = seq_.load(rstd::sync::atomic::Ordering::Acquire);
            if (s1 == 0) {
                out.clear();
                return false;
            }
            if (s1 & 1u) continue;
            AudioSpectrum tmp;
            rstd::mem::memcpy(&tmp, &published_, usize(sizeof(AudioSpectrum)));
            const rstd::uint32_t s2 = seq_.load(rstd::sync::atomic::Ordering::Acquire);
            if (s1 == s2) {
                out = tmp;
                return true;
            }
        }
        out.clear();
        return false;
    }

private:
    static void on_process(void* user) {
        auto* self = static_cast<Impl*>(user);
        if (! self->stream_) return;

        pipewire_ffi::pw_buffer* b = self->api_->pw_stream_dequeue_buffer(self->stream_);
        if (! b) return;

        auto* sb = b->buffer;
        if (! sb || sb->n_datas == 0 || ! sb->datas[0].data) {
            self->api_->pw_stream_queue_buffer(self->stream_, b);
            return;
        }

        auto&      d        = sb->datas[0];
        const auto stride   = d.chunk->stride > 0
                                  ? static_cast<rstd::uint32_t>(d.chunk->stride)
                                  : kDefaultChannels * static_cast<rstd::uint32_t>(sizeof(float));
        const auto channels = stride / static_cast<rstd::uint32_t>(sizeof(float));
        const rstd::uint32_t offset = d.chunk->offset % d.maxsize;
        const rstd::uint32_t bytes  = rstd::cmp::min(d.chunk->size, d.maxsize - offset);
        const auto*          src =
            reinterpret_cast<const float*>(static_cast<const rstd::uint8_t*>(d.data) + offset);
        const rstd::uint32_t n_frames = bytes / stride;

        self->ingest(src, n_frames, channels);

        self->api_->pw_stream_queue_buffer(self->stream_, b);
    }

    static void on_state_changed(void* /*user*/, pipewire_ffi::pw_stream_state /*old*/,
                                 pipewire_ffi::pw_stream_state state, const char* error) {
        switch (state) {
        case pipewire_ffi::stream_state_error:
            rstd::log::error("wavsen::audio: capture stream ERROR{}",
                             error ? rstd::format(": {}", error) : String {});
            break;
        case pipewire_ffi::stream_state_unconnected:
            rstd::log::debug("wavsen::audio: capture stream UNCONNECTED");
            break;
        case pipewire_ffi::stream_state_connecting:
            rstd::log::debug("wavsen::audio: capture stream CONNECTING");
            break;
        case pipewire_ffi::stream_state_paused:
            rstd::log::debug("wavsen::audio: capture stream PAUSED");
            break;
        case pipewire_ffi::stream_state_streaming:
            rstd::log::debug("wavsen::audio: capture stream STREAMING");
            break;
        }
    }

    void ingest(const float* src, rstd::uint32_t n_frames, rstd::uint32_t channels) {
        for (rstd::uint32_t f = 0; f < n_frames; ++f) {
            const rstd::uint32_t base      = f * channels;
            const float          left      = channels > 0 ? src[base] : 0.f;
            const float          right     = channels > 1 ? src[base + 1] : left;
            ring_left_[usize(ring_head_)]  = left;
            ring_right_[usize(ring_head_)] = right;
            ring_head_                     = (ring_head_ + 1) % dsp::kFftSize;
            if (samples_filled_ < dsp::kFftSize) ++samples_filled_;
            ++samples_since_fft_;
        }

        if (samples_filled_ < dsp::kFftSize || samples_since_fft_ < dsp::kHopSize) return;
        samples_since_fft_ = 0;

        rstd::array<std::complex<float>, dsp::kFftSize> buf_left;
        rstd::array<std::complex<float>, dsp::kFftSize> buf_right;
        for (rstd::size_t i = 0; i < dsp::kFftSize; ++i) {
            const rstd::size_t idx = (ring_head_ + i) % dsp::kFftSize;
            const float        w   = dsp::hann_window(i, dsp::kFftSize);
            buf_left[usize(i)]     = std::complex<float>(ring_left_[usize(idx)] * w, 0.0f);
            buf_right[usize(i)]    = std::complex<float>(ring_right_[usize(idx)] * w, 0.0f);
        }

        dsp::fft_inplace(buf_left.data(), dsp::kFftSize);
        dsp::fft_inplace(buf_right.data(), dsp::kFftSize);

        const float norm = 2.0f / static_cast<float>(dsp::kFftSize);
        const auto  raw =
            dsp::analyze_stereo_spectrum(buf_left.data(), buf_right.data(), band_layout_, norm);
        const auto dt_sec = static_cast<float>(dsp::kHopSize) / static_cast<float>(kDefaultRate);
        const auto bands  = dsp::smooth_spectrum(raw, smoothed_, dt_sec);

        AudioSpectrum out {};
        for (rstd::size_t k = 0; k < dsp::kNumBins; ++k) {
            const auto index   = usize(k);
            out.left[index]    = f32(bands.left[index]);
            out.right[index]   = f32(bands.right[index]);
            out.average[index] = f32(bands.average[index]);
            out.bins[index]    = f32(bands.average[index]);
        }
        out.publish_ms = i64(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count());

        seq_.fetch_add(1, rstd::sync::atomic::Ordering::Release);
        rstd::mem::memcpy(&published_, &out, usize(sizeof(AudioSpectrum)));
        seq_.fetch_add(1, rstd::sync::atomic::Ordering::Release);
    }

    const pipewire_ffi::Api*      api_    = nullptr;
    pipewire_ffi::pw_thread_loop* loop_   = nullptr;
    pipewire_ffi::pw_stream*      stream_ = nullptr;

    rstd::array<float, dsp::kFftSize> ring_left_ {};
    rstd::array<float, dsp::kFftSize> ring_right_ {};
    rstd::size_t                      ring_head_         = 0;
    rstd::size_t                      samples_filled_    = 0;
    rstd::size_t                      samples_since_fft_ = 0;
    dsp::BandLayout                   band_layout_ { dsp::make_we_layout(kDefaultRate) };
    dsp::SpectrumBands                smoothed_ {};

    mutable rstd::sync::atomic::Atomic<rstd::uint32_t> seq_ { 0 };
    AudioSpectrum                                      published_ {};
};

AudioCapture::AudioCapture(): impl_(Box<Impl>::make()) {}
AudioCapture::~AudioCapture() = default;

bool AudioCapture::init() { return impl_->init(); }
void AudioCapture::uninit() { impl_->uninit(); }
bool AudioCapture::is_inited() const { return impl_->is_inited(); }
bool AudioCapture::snapshot(AudioSpectrum& out) const { return impl_->snapshot(out); }

} // namespace wavsen::audio
