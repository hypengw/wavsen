export module wavsen.audio:av_sync;

import rstd.cppstd;
import rstd;
import :byte_stream;
import :core;

export namespace wavsen::audio
{

using namespace rstd::prelude;

// Error type for AvPlayer::open. Mirrors the small-string Error pattern
// used elsewhere in wavsen (no rich type — just a printable reason).
struct AvPlayerError {
    std::string message;
};

// Single-stream audio playback aimed at A/V sync. Owns its own
// AudioDevice (independent of any SoundManager) so it can expose a
// stable master clock published by the backend thread.
//
// Threading: open/play/pause/seek_to_start/set_volume/set_muted are all
// callable from the main thread. current_time_seconds is lock-free and
// safe to call from any thread (e.g. from inside a Presenter callback).
class AvPlayer {
public:
    static auto open(ByteStream src) -> rstd::Result<std::unique_ptr<AvPlayer>, AvPlayerError>;
    static auto open(ByteStream src, bool open_device)
        -> rstd::Result<std::unique_ptr<AvPlayer>, AvPlayerError>;
    static auto open(ByteStream src, bool open_device, AudioClientIdentity identity)
        -> rstd::Result<std::unique_ptr<AvPlayer>, AvPlayerError>;

    ~AvPlayer();
    AvPlayer(const AvPlayer&)            = delete;
    AvPlayer& operator=(const AvPlayer&) = delete;

    bool open_device();
    void close_device();
    bool is_device_open() const;

    void play();
    void pause();
    bool is_paused() const;

    // Reset playback to t=0. Call from the video plugin's loop boundary
    // after the video decoder seeks back to the start. The clock will
    // re-anchor on the next data callback.
    void seek_to_start();
    void seek_to(f64 seconds);

    // PTS in seconds of the audio sample currently being played by the
    // device. Returns NaN before the device is primed; the caller should
    // treat NaN as "fall back to wall-clock pacing".
    auto current_time_seconds() const -> f64;

    // 0..1 linear gain. Atomic; safe from any thread.
    void set_volume(f32 value);
    void set_muted(bool m);
    auto volume_scale() const -> f32;
    void set_volume_scale(f32 value);
    void set_volume_scale(f32 value, u32 fade_ms);

    // True once the decoder reached EOF *and* the device has had time
    // to drain the last enqueued frames.
    bool is_eof() const;

private:
    explicit AvPlayer(AudioClientIdentity identity);
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wavsen::audio
