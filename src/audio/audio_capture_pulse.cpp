module;

#include <chrono>
#include <complex>

module wavsen.audio;

import rstd;
import rstd.log;
import pulse;
import :capture;
import wavsen.audio.capture_dsp;

namespace wavsen::audio
{

using namespace rstd::prelude;

namespace pulse_ffi = wavsen::ffi::pulse;

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

        api_ = pulse_ffi::load();
        if (! api_) {
            rstd::log::error("wavsen::audio: capture failed to load PulseAudio: {}",
                             pulse_ffi::load_error());
            return false;
        }

        loop_ = api_->pa_threaded_mainloop_new();
        if (! loop_) {
            rstd::log::error("wavsen::audio: capture pa_threaded_mainloop_new failed");
            return false;
        }
        if (api_->pa_threaded_mainloop_start(loop_) < 0) {
            rstd::log::error("wavsen::audio: capture pa_threaded_mainloop_start failed");
            api_->pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }

        api_->pa_threaded_mainloop_lock(loop_);

        ctx_ = api_->pa_context_new(api_->pa_threaded_mainloop_get_api(loop_), "wavsen-capture");
        if (! ctx_) {
            api_->pa_threaded_mainloop_unlock(loop_);
            rstd::log::error("wavsen::audio: capture pa_context_new failed");
            api_->pa_threaded_mainloop_stop(loop_);
            api_->pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }
        api_->pa_context_set_state_callback(ctx_, &Impl::on_context_state, this);

        if (api_->pa_context_connect(ctx_, nullptr, pulse_ffi::context_noflags, nullptr) < 0) {
            rstd::log::error("wavsen::audio: capture pa_context_connect failed: {}",
                             api_->pa_strerror(api_->pa_context_errno(ctx_)));
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }

        for (;;) {
            const auto st = api_->pa_context_get_state(ctx_);
            if (st == pulse_ffi::context_ready) break;
            if (! pulse_ffi::context_is_good(st)) {
                rstd::log::error("wavsen::audio: capture pa_context failed: {}",
                                 api_->pa_strerror(api_->pa_context_errno(ctx_)));
                destroy_locked();
                api_->pa_threaded_mainloop_unlock(loop_);
                shutdown_loop();
                return false;
            }
            api_->pa_threaded_mainloop_wait(loop_);
        }

        // Resolve default sink → "<sink>.monitor" source name.
        default_sink_.clear();
        server_info_done_ = false;
        auto* op          = api_->pa_context_get_server_info(ctx_, &Impl::on_server_info, this);
        if (! op) {
            rstd::log::error("wavsen::audio: capture pa_context_get_server_info failed");
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        while (! server_info_done_) {
            api_->pa_threaded_mainloop_wait(loop_);
        }
        if (default_sink_.is_empty()) {
            rstd::log::error("wavsen::audio: capture could not resolve default sink");
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        auto monitor_name   = rstd::format("{}.monitor", default_sink_);
        auto monitor_name_c = rstd::ffi::CString::make(monitor_name.clone()).unwrap();

        pulse_ffi::pa_sample_spec ss {};
        ss.format   = pulse_ffi::sample_float32le;
        ss.rate     = kDefaultRate;
        ss.channels = static_cast<rstd::uint8_t>(kDefaultChannels);

        pulse_ffi::pa_channel_map cm {};
        api_->pa_channel_map_init_stereo(&cm);

        stream_ = api_->pa_stream_new(ctx_, "wavsen-capture", &ss, &cm);
        if (! stream_) {
            rstd::log::error("wavsen::audio: capture pa_stream_new failed: {}",
                             api_->pa_strerror(api_->pa_context_errno(ctx_)));
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        api_->pa_stream_set_state_callback(stream_, &Impl::on_stream_state, this);
        api_->pa_stream_set_read_callback(stream_, &Impl::on_read, this);

        const auto frame_bytes = kDefaultChannels * static_cast<rstd::uint32_t>(sizeof(float));
        pulse_ffi::pa_buffer_attr ba {};
        ba.maxlength = static_cast<rstd::uint32_t>(-1);
        ba.tlength   = static_cast<rstd::uint32_t>(-1);
        ba.prebuf    = static_cast<rstd::uint32_t>(-1);
        ba.minreq    = static_cast<rstd::uint32_t>(-1);
        ba.fragsize  = kQuantum * frame_bytes;

        const auto flags =
            static_cast<pulse_ffi::pa_stream_flags_t>(pulse_ffi::stream_adjust_latency);

        if (api_->pa_stream_connect_record(stream_, monitor_name_c.as_ptr(), &ba, flags) < 0) {
            rstd::log::error("wavsen::audio: capture pa_stream_connect_record failed: {}",
                             api_->pa_strerror(api_->pa_context_errno(ctx_)));
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }

        for (;;) {
            const auto st = api_->pa_stream_get_state(stream_);
            if (st == pulse_ffi::stream_ready) break;
            if (! pulse_ffi::stream_is_good(st)) {
                rstd::log::error("wavsen::audio: capture pa_stream failed: {}",
                                 api_->pa_strerror(api_->pa_context_errno(ctx_)));
                destroy_locked();
                api_->pa_threaded_mainloop_unlock(loop_);
                shutdown_loop();
                return false;
            }
            api_->pa_threaded_mainloop_wait(loop_);
        }

        api_->pa_threaded_mainloop_unlock(loop_);

        rstd::log::info("wavsen::audio: capture inited (pulse monitor '{}', "
                        "{} ch @ {} Hz)",
                        monitor_name,
                        kDefaultChannels,
                        kDefaultRate);
        return true;
    }

    void uninit() {
        if (loop_) {
            api_->pa_threaded_mainloop_lock(loop_);
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
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
    void destroy_locked() {
        if (stream_) {
            api_->pa_stream_set_state_callback(stream_, nullptr, nullptr);
            api_->pa_stream_set_read_callback(stream_, nullptr, nullptr);
            api_->pa_stream_disconnect(stream_);
            api_->pa_stream_unref(stream_);
            stream_ = nullptr;
        }
        if (ctx_) {
            api_->pa_context_set_state_callback(ctx_, nullptr, nullptr);
            api_->pa_context_disconnect(ctx_);
            api_->pa_context_unref(ctx_);
            ctx_ = nullptr;
        }
    }
    void shutdown_loop() {
        if (loop_) {
            api_->pa_threaded_mainloop_stop(loop_);
            api_->pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
        }
    }

    static void on_context_state(pulse_ffi::pa_context* /*c*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->api_->pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_stream_state(pulse_ffi::pa_stream* /*s*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->api_->pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_server_info(pulse_ffi::pa_context* /*c*/, const pulse_ffi::pa_server_info* info,
                               void* user) {
        auto* self = static_cast<Impl*>(user);
        if (info && info->default_sink_name) {
            self->default_sink_ =
                String::make(rstd::ffi::CStr::from_ptr(info->default_sink_name).to_str().unwrap());
        }
        self->server_info_done_ = true;
        self->api_->pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_read(pulse_ffi::pa_stream* s, size_t /*nbytes*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        while (self->api_->pa_stream_readable_size(s) > 0) {
            const void* data = nullptr;
            size_t      sz   = 0;
            if (self->api_->pa_stream_peek(s, &data, &sz) < 0) return;
            if (sz == 0) return;
            // A hole: data==nullptr means dropped samples; advance and continue.
            if (! data) {
                self->api_->pa_stream_drop(s);
                continue;
            }
            constexpr rstd::uint32_t channels = kDefaultChannels;
            constexpr rstd::uint32_t stride   = channels * sizeof(float);
            const auto*              src      = static_cast<const float*>(data);
            const rstd::uint32_t     n_frames = static_cast<rstd::uint32_t>(sz / stride);
            self->ingest(src, n_frames, channels);
            self->api_->pa_stream_drop(s);
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

    const pulse_ffi::Api*            api_    = nullptr;
    pulse_ffi::pa_threaded_mainloop* loop_   = nullptr;
    pulse_ffi::pa_context*           ctx_    = nullptr;
    pulse_ffi::pa_stream*            stream_ = nullptr;
    String                           default_sink_;
    bool                             server_info_done_ = false;

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
