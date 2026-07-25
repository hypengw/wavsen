module wavsen.audio;

import rstd.cppstd;
import rstd;
import :core;
import :mixer;

namespace wavsen::audio
{

namespace
{

// Adapter exposing a SoundStream to AudioDevice's IPullChannel interface.
class StreamPullChannel : public IPullChannel {
public:
    explicit StreamPullChannel(std::unique_ptr<SoundStream> ss): ss_(std::move(ss)) {}

    auto next_pcm(void* dst, std::uint32_t frames) -> std::uint64_t override {
        return ss_->next_pcm(dst, frames);
    }
    void pass_desc(const DeviceDesc& d) override { ss_->pass_desc({ d.channels, d.sample_rate }); }

private:
    std::unique_ptr<SoundStream> ss_;
};

} // namespace

class SoundManager::Impl {
public:
    explicit Impl(AudioClientIdentity identity_): identity(std::move(identity_)) {}

    void apply(std::uint32_t fade_ms = 0) {
        if (! activated || shutting_down) return;
        (void)device.apply(AudioDeviceDesiredState {
            .generation            = generation,
            .active                = ! muted,
            .playing               = playing,
            .identity              = identity,
            .volume                = volume,
            .muted                 = false,
            .volume_scale          = volume_scale,
            .volume_scale_revision = volume_scale_revision,
            .volume_scale_fade_ms  = fade_ms,
        });
    }

    AudioDevice         device;
    AudioClientIdentity identity;
    AudioDeviceState    observed_state { AudioDeviceState::Idle };
    std::uint64_t       generation {};
    std::uint64_t       stream_revision {};
    std::uint64_t       volume_scale_revision {};
    float               volume { 1.0f };
    float               volume_scale { 1.0f };
    bool                muted {};
    bool                playing {};
    bool                activated {};
    bool                shutting_down {};
};

SoundManager::SoundManager(AudioClientIdentity identity)
    : impl_(std::make_unique<Impl>(std::move(identity))) {}
SoundManager::~SoundManager() = default;

void SoundManager::mount(std::unique_ptr<SoundStream> ss) {
    if (! ss) return;
    (void)impl_->device.mount(std::make_unique<StreamPullChannel>(std::move(ss)),
                              impl_->stream_revision);
}

void SoundManager::unmount_all() {
    ++impl_->stream_revision;
    (void)impl_->device.unmount_all(impl_->stream_revision);
}

void SoundManager::activate(AudioDeviceEventSink sink) {
    if (impl_->activated || impl_->shutting_down) return;
    impl_->device.set_event_sink(std::move(sink));
    impl_->activated = true;
    ++impl_->generation;
    impl_->apply();
}

void SoundManager::on_device_event(AudioDeviceEvent event) {
    if (event.generation != impl_->generation && event.state != AudioDeviceState::Stopped) return;
    impl_->observed_state = event.state;
}

void SoundManager::shutdown() {
    if (impl_->shutting_down) return;
    impl_->shutting_down = true;
    impl_->device.shutdown();
}

bool SoundManager::set_identity(AudioClientIdentity identity) {
    if (impl_->shutting_down) return false;
    impl_->identity = std::move(identity);
    if (impl_->activated) ++impl_->generation;
    impl_->apply();
    return true;
}

bool SoundManager::is_inited() const {
    return impl_->observed_state == AudioDeviceState::ReadyPaused ||
           impl_->observed_state == AudioDeviceState::ReadyPlaying;
}

void SoundManager::play() {
    impl_->playing = true;
    impl_->apply();
}
void SoundManager::pause() {
    impl_->playing = false;
    impl_->apply();
}

float SoundManager::volume() const { return impl_->volume; }
bool  SoundManager::muted() const { return impl_->muted; }
void  SoundManager::set_volume(float v) {
    impl_->volume = v;
    impl_->apply();
}
float SoundManager::volume_scale() const { return impl_->volume_scale; }
void  SoundManager::set_volume_scale(float v) { set_volume_scale(v, 0); }
void  SoundManager::set_volume_scale(float v, std::uint32_t fade_ms) {
    impl_->volume_scale = v;
    ++impl_->volume_scale_revision;
    impl_->apply(fade_ms);
}

void SoundManager::set_muted(bool m) {
    if (impl_->muted == m) return;
    impl_->muted = m;
    if (impl_->activated) ++impl_->generation;
    impl_->apply();
}

} // namespace wavsen::audio
