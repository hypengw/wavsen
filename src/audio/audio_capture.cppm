export module wavsen.audio:capture;

import rstd;

export namespace wavsen::audio
{

using namespace rstd::prelude;

// 64-bin perceptual magnitude spectrum, EMA-smoothed. Values are mapped to
// 0..1 for audio-responsive consumers. `bins` is kept as an alias of `average`.
// `publish_ms` is a steady_clock timestamp (ms since epoch) of the last
// RT-side update, used by readers to detect stale snapshots. Zero means
// "never primed".
struct AudioSpectrum {
    rstd::array<f32, 64> left;
    rstd::array<f32, 64> right;
    rstd::array<f32, 64> average;
    rstd::array<f32, 64> bins;
    i64                  publish_ms;

    void clear() {
        for (auto& value : left) value = f32();
        for (auto& value : right) value = f32();
        for (auto& value : average) value = f32();
        for (auto& value : bins) value = f32();
        publish_ms = i64();
    }
};

// Taps the system default sink's monitor source, runs a 4096-point
// Hann-windowed FFT per stereo channel on the audio thread, merges
// magnitudes into 64 calibrated WE-style bands, EMA-smooths, and publishes a
// lock-free snapshot for renderers.
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    AudioCapture(const AudioCapture&)            = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    auto init() -> bool;
    void uninit();
    auto is_inited() const -> bool;

    // Lock-free read. Returns true if at least one capture buffer has
    // been processed; out is zero-filled until then.
    bool snapshot(AudioSpectrum& out) const;

private:
    class Impl;
    Box<Impl> impl_;
};

} // namespace wavsen::audio
