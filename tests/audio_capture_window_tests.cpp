#include <limits>

import rstd;
import wavsen.audio.capture;
import wavsen.audio.capture_window;

using namespace rstd::prelude;

int main() {
    wavsen::audio::capture::PcmWindowPublisher publisher;
    publisher.restart();
    rstd::array<float, wavsen::audio::kAudioSampleCount> samples {};
    for (rstd::size_t frame = 0; frame < wavsen::audio::kAudioWindowFrames; ++frame) {
        samples[usize(frame * 2)]     = static_cast<float>(frame);
        samples[usize(frame * 2 + 1)] = -static_cast<float>(frame);
    }
    samples[usize(10)] = std::numeric_limits<float>::quiet_NaN();
    publisher.ingest(samples.data(),
                     static_cast<rstd::uint32_t>(wavsen::audio::kAudioWindowFrames - 1),
                     wavsen::audio::kAudioChannels);
    wavsen::audio::AudioPcmWindow window {};
    if (publisher.snapshot(window)) return 1;
    publisher.ingest(samples.data() + (wavsen::audio::kAudioWindowFrames - 1) * 2,
                     1,
                     wavsen::audio::kAudioChannels);
    if (! publisher.snapshot(window)) return 2;
    if (window.generation != 1 || window.sequence != 1 ||
        window.end_sample_frame != wavsen::audio::kAudioWindowFrames)
        return 3;
    if (window.samples[usize(10)].to_primitive() != 0.0f) return 4;

    publisher.restart();
    publisher.ingest(samples.data(),
                     static_cast<rstd::uint32_t>(wavsen::audio::kAudioWindowFrames),
                     wavsen::audio::kAudioChannels);
    if (! publisher.snapshot(window) || window.generation != 2 || window.sequence != 1) return 5;

    constexpr rstd::size_t                                                     extra_frames = 17;
    rstd::array<float, (wavsen::audio::kAudioWindowFrames + extra_frames) * 2> clean {};
    for (rstd::size_t frame = 0; frame < wavsen::audio::kAudioWindowFrames + extra_frames;
         ++frame) {
        clean[usize(frame * 2)]     = static_cast<float>(frame);
        clean[usize(frame * 2 + 1)] = -static_cast<float>(frame);
    }
    wavsen::audio::capture::PcmWindowPublisher whole;
    whole.restart();
    whole.ingest(clean.data(),
                 static_cast<rstd::uint32_t>(wavsen::audio::kAudioWindowFrames + extra_frames),
                 wavsen::audio::kAudioChannels);
    wavsen::audio::AudioPcmWindow whole_window {};
    if (! whole.snapshot(whole_window)) return 6;

    wavsen::audio::capture::PcmWindowPublisher chunked;
    chunked.restart();
    rstd::size_t consumed = 0;
    while (consumed < wavsen::audio::kAudioWindowFrames + extra_frames) {
        const auto count = rstd::cmp::min(
            rstd::size_t(137), wavsen::audio::kAudioWindowFrames + extra_frames - consumed);
        chunked.ingest(clean.data() + consumed * 2,
                       static_cast<rstd::uint32_t>(count),
                       wavsen::audio::kAudioChannels);
        consumed += count;
    }
    wavsen::audio::AudioPcmWindow chunked_window {};
    if (! chunked.snapshot(chunked_window)) return 7;
    if (whole_window.end_sample_frame != chunked_window.end_sample_frame) return 8;
    for (rstd::size_t index = 0; index < wavsen::audio::kAudioSampleCount; ++index) {
        if (whole_window.samples[usize(index)] != chunked_window.samples[usize(index)]) return 9;
    }
    return 0;
}
