export module wavsen.audio:core;

import rstd.cppstd;
import rstd;

using namespace rstd::prelude;

export namespace wavsen::audio
{

// Negotiated stream format — wavsen always asks the backend for f32 interleaved.
struct DeviceDesc {
    u32 channels;
    u32 sample_rate;
};

struct AudioClientIdentity {
    std::string application_name { "wavsen" };
    std::string application_id { "org.wavsen" };
    std::string stream_prefix { "wavsen." };
    std::string component { "audio" };
    std::string media_name { "wavsen audio output" };
    std::string media_role { "music" };

    auto playback_stream_name() const -> std::optional<std::string> {
        if (component.empty()) return std::nullopt;
        for (const char value : component) {
            const bool valid =
                (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-';
            if (! valid) return std::nullopt;
        }
        return stream_prefix + component + ".playback";
    }
};

// Pulled by the audio thread to fill an output buffer. Implementations
// must NOT block (the backend data thread is realtime). `frames` is the
// number of interleaved frames to write into `dst`.
class IPullChannel {
public:
    virtual ~IPullChannel()                             = default;
    virtual auto next_pcm(void* dst, u32 frames) -> u64 = 0;
    virtual void pass_desc(const DeviceDesc&)           = 0;
};

enum class AudioDeviceState : std::uint8_t
{
    Idle,
    Connecting,
    ReadyPaused,
    ReadyPlaying,
    Failed,
    Stopped,
};

struct AudioDeviceEvent {
    u64              generation;
    AudioDeviceState state { AudioDeviceState::Idle };
    std::string      error;
};

using AudioDeviceEventSink = std::function<void(AudioDeviceEvent)>;

struct AudioDeviceDesiredState {
    u64                 generation;
    bool                active {};
    bool                playing {};
    AudioClientIdentity identity;
    f32                 volume { f32(1.0f) };
    bool                muted {};
    f32                 volume_scale { f32(1.0f) };
    u64                 volume_scale_revision;
    u32                 volume_scale_fade_ms;
};

class AudioDevice {
public:
    AudioDevice();
    ~AudioDevice();
    AudioDevice(const AudioDevice&)            = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    void set_event_sink(AudioDeviceEventSink);
    auto apply(AudioDeviceDesiredState) -> bool;
    auto mount(std::unique_ptr<IPullChannel>, u64 stream_revision) -> bool;
    auto unmount_all(u64 stream_revision) -> bool;
    void shutdown();
    void wait_stopped();

    auto state() const -> AudioDeviceState;
    auto desc() const -> DeviceDesc;

    // Frames the audio device has actually played back since the stream
    // was created. The backend thread publishes this cached value so callers
    // never enter the native audio API from another thread.
    auto stream_position_frames() const -> u64;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wavsen::audio
