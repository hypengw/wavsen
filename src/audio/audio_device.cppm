export module wavsen.audio:core;

import rstd.cppstd;

export namespace wavsen::audio
{

// Negotiated stream format — wavsen always asks the backend for f32 interleaved.
struct DeviceDesc {
    std::uint32_t channels;
    std::uint32_t sample_rate;
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
    virtual ~IPullChannel()                                                 = default;
    virtual auto next_pcm(void* dst, std::uint32_t frames) -> std::uint64_t = 0;
    virtual void pass_desc(const DeviceDesc&)                               = 0;
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
    std::uint64_t    generation {};
    AudioDeviceState state { AudioDeviceState::Idle };
    std::string      error;
};

using AudioDeviceEventSink = std::function<void(AudioDeviceEvent)>;

struct AudioDeviceDesiredState {
    std::uint64_t       generation {};
    bool                active {};
    bool                playing {};
    AudioClientIdentity identity;
    float               volume { 1.0f };
    bool                muted {};
    float               volume_scale { 1.0f };
    std::uint64_t       volume_scale_revision {};
    std::uint32_t       volume_scale_fade_ms {};
};

class AudioDevice {
public:
    AudioDevice();
    ~AudioDevice();
    AudioDevice(const AudioDevice&)            = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    void set_event_sink(AudioDeviceEventSink);
    auto apply(AudioDeviceDesiredState) -> bool;
    auto mount(std::unique_ptr<IPullChannel>, std::uint64_t stream_revision) -> bool;
    auto unmount_all(std::uint64_t stream_revision) -> bool;
    void shutdown();
    void wait_stopped();

    auto state() const -> AudioDeviceState;
    auto desc() const -> DeviceDesc;

    // Frames the audio device has actually played back since the stream
    // was created. The backend thread publishes this cached value so callers
    // never enter the native audio API from another thread.
    auto stream_position_frames() const -> std::uint64_t;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wavsen::audio
