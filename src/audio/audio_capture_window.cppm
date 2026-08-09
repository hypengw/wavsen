module;

#include <chrono>
#include <cmath>

export module wavsen.audio.capture_window;

import rstd;
import wavsen.audio.capture;

using namespace rstd::prelude;

export namespace wavsen::audio::capture
{

class PcmWindowPublisher {
public:
    void restart() {
        generation_ += 1;
        sequence_         = 0;
        end_sample_frame_ = 0;
        head_             = 0;
        filled_           = 0;
        for (auto& sample : ring_) sample = f32();
        publication_.fetch_add(1, rstd::sync::atomic::Ordering::Release);
        published_.clear();
        publication_.fetch_add(1, rstd::sync::atomic::Ordering::Release);
    }

    void ingest(const float* source, rstd::uint32_t frame_count, rstd::uint32_t channels) {
        if (! source || channels == 0) return;
        for (rstd::uint32_t frame = 0; frame < frame_count; ++frame) {
            const auto  source_offset     = frame * channels;
            const auto  destination       = head_ * kAudioChannels;
            const float left              = finite(source[source_offset]);
            const float right             = channels > 1 ? finite(source[source_offset + 1]) : left;
            ring_[usize(destination)]     = f32(left);
            ring_[usize(destination + 1)] = f32(right);
            head_                         = (head_ + 1) % kAudioWindowFrames;
            filled_                       = rstd::cmp::min(filled_ + 1, kAudioWindowFrames);
            end_sample_frame_ += 1;
        }
        if (filled_ == kAudioWindowFrames && frame_count > 0) publish();
    }

    bool snapshot(AudioPcmWindow& out) const {
        for (int attempt = 0; attempt < 16; ++attempt) {
            const auto before = publication_.load(rstd::sync::atomic::Ordering::Acquire);
            if (before == 0 || (before & 1u) != 0) continue;
            AudioPcmWindow candidate;
            rstd::mem::memcpy(&candidate, &published_, usize(sizeof(AudioPcmWindow)));
            const auto after = publication_.load(rstd::sync::atomic::Ordering::Acquire);
            if (before == after) {
                out = candidate;
                return candidate.frames == kAudioWindowFrames;
            }
        }
        out.clear();
        return false;
    }

private:
    static float finite(float sample) { return std::isfinite(sample) ? sample : 0.0f; }

    void publish() {
        AudioPcmWindow window {};
        window.generation = generation_;
        window.sequence   = ++sequence_;
        window.captured_at_ns =
            static_cast<rstd::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count());
        window.end_sample_frame = end_sample_frame_;
        window.sample_rate_hz   = kAudioSampleRate;
        window.channels         = kAudioChannels;
        window.frames           = static_cast<rstd::uint32_t>(kAudioWindowFrames);
        for (rstd::size_t frame = 0; frame < kAudioWindowFrames; ++frame) {
            const auto source      = ((head_ + frame) % kAudioWindowFrames) * kAudioChannels;
            const auto destination = frame * kAudioChannels;
            window.samples[usize(destination)]     = ring_[usize(source)];
            window.samples[usize(destination + 1)] = ring_[usize(source + 1)];
        }
        publication_.fetch_add(1, rstd::sync::atomic::Ordering::Release);
        rstd::mem::memcpy(&published_, &window, usize(sizeof(AudioPcmWindow)));
        publication_.fetch_add(1, rstd::sync::atomic::Ordering::Release);
    }

    rstd::array<f32, kAudioSampleCount>                ring_ {};
    rstd::size_t                                       head_             = 0;
    rstd::size_t                                       filled_           = 0;
    rstd::uint64_t                                     generation_       = 0;
    rstd::uint64_t                                     sequence_         = 0;
    rstd::uint64_t                                     end_sample_frame_ = 0;
    mutable rstd::sync::atomic::Atomic<rstd::uint32_t> publication_ { 0 };
    AudioPcmWindow                                     published_ {};
};

} // namespace wavsen::audio::capture
