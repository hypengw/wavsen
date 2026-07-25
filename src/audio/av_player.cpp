module wavsen.audio;

import rstd.cppstd;
import rstd;
import rstd.log;
import :byte_stream;
import :core; // AudioDevice, IPullChannel, DeviceDesc
import :file; // StreamDecoder
import :av_sync;

namespace wavsen::audio
{

namespace
{

// Internal pull channel for AvPlayer. Calls into StreamDecoder and
// stamps the master-clock anchor on the first frame after open / seek.
class AvPullChannel : public IPullChannel {
public:
    AvPullChannel(StreamDecoder* decoder, std::atomic<double>* pts_at_anchor,
                  std::atomic<std::uint64_t>* device_pos_at_anchor,
                  std::atomic<bool>* needs_reanchor, std::atomic<bool>* anchored,
                  std::function<std::uint64_t()> device_pos_now)
        : decoder_(decoder),
          pts_at_anchor_(pts_at_anchor),
          device_pos_at_anchor_(device_pos_at_anchor),
          needs_reanchor_(needs_reanchor),
          anchored_(anchored),
          device_pos_now_(std::move(device_pos_now)) {}

    auto next_pcm(void* dst, std::uint32_t frames) -> std::uint64_t override {
        const auto produced = decoder_->next_pcm(dst, frames);
        if (produced > 0 && needs_reanchor_->load(std::memory_order_acquire)) {
            // Anchor: at the moment this batch enters the device, the
            // decoder's most-recent PTS corresponds to the device's
            // current playback position.
            pts_at_anchor_->store(decoder_->current_pts_seconds(), std::memory_order_relaxed);
            device_pos_at_anchor_->store(device_pos_now_(), std::memory_order_relaxed);
            anchored_->store(true, std::memory_order_release);
            needs_reanchor_->store(false, std::memory_order_release);
        }
        return produced;
    }

    void pass_desc(const DeviceDesc& d) override { decoder_->retarget(d); }

private:
    StreamDecoder*                 decoder_;
    std::atomic<double>*           pts_at_anchor_;
    std::atomic<std::uint64_t>*    device_pos_at_anchor_;
    std::atomic<bool>*             needs_reanchor_;
    std::atomic<bool>*             anchored_;
    std::function<std::uint64_t()> device_pos_now_;
};

} // namespace

class AvPlayer::Impl {
public:
    explicit Impl(AudioClientIdentity identity_) { desired.identity = std::move(identity_); }

    ~Impl() {
        // The audio callback borrows decoder_ptr, so it must stop before the decoder is destroyed.
        device.shutdown();
        device.wait_stopped();
        decoder_ptr = nullptr;
        decoder_storage.reset();
    }

    AudioDevice                    device;
    AudioDeviceDesiredState        desired;
    std::uint64_t                  stream_revision {};
    StreamDecoder*                 decoder_ptr = nullptr;
    std::unique_ptr<StreamDecoder> decoder_storage;

    std::atomic<double>        pts_at_anchor { 0.0 };
    std::atomic<std::uint64_t> device_pos_at_anchor { 0 };
    std::atomic<bool>          needs_reanchor { true };
    std::atomic<bool>          anchored { false };
    std::atomic<bool>          paused { true };
};

AvPlayer::AvPlayer(AudioClientIdentity identity)
    : impl_(std::make_unique<Impl>(std::move(identity))) {}
AvPlayer::~AvPlayer() = default;

auto AvPlayer::open(ByteStream src) -> rstd::Result<std::unique_ptr<AvPlayer>, AvPlayerError> {
    return open(std::move(src), true, {});
}

auto AvPlayer::open(ByteStream src, bool open_device)
    -> rstd::Result<std::unique_ptr<AvPlayer>, AvPlayerError> {
    return open(std::move(src), open_device, {});
}

auto AvPlayer::open(ByteStream src, bool open_device, AudioClientIdentity identity)
    -> rstd::Result<std::unique_ptr<AvPlayer>, AvPlayerError> {
    auto p = std::unique_ptr<AvPlayer>(new AvPlayer(std::move(identity)));

    DeviceDesc desc {
        .channels    = 2,
        .sample_rate = 48000,
    };
    if (open_device && ! p->open_device()) {
        return rstd::Err(AvPlayerError { "audio device init failed" });
    }

    p->impl_->decoder_storage = std::make_unique<StreamDecoder>();
    if (! p->impl_->decoder_storage->open(std::move(src), desc)) {
        return rstd::Err(AvPlayerError { "audio decoder open failed" });
    }
    p->impl_->decoder_ptr = p->impl_->decoder_storage.get();

    rstd::log::info("wavsen::audio::AvPlayer: opened ({} ch @ {} Hz target, source {} ch @ {} Hz)",
                    desc.channels,
                    desc.sample_rate,
                    p->impl_->decoder_ptr->channels(),
                    p->impl_->decoder_ptr->sample_rate());

    auto* dev     = &p->impl_->device;
    auto  channel = std::make_unique<AvPullChannel>(p->impl_->decoder_ptr,
                                                    &p->impl_->pts_at_anchor,
                                                    &p->impl_->device_pos_at_anchor,
                                                    &p->impl_->needs_reanchor,
                                                    &p->impl_->anchored,
                                                    [dev]() {
                                                       return dev->stream_position_frames();
                                                    });
    if (! p->impl_->device.mount(std::move(channel), p->impl_->stream_revision)) {
        return rstd::Err(AvPlayerError { "audio device stream mount failed" });
    }

    return rstd::Ok(std::move(p));
}

bool AvPlayer::open_device() {
    if (impl_->desired.active) return true;
    ++impl_->desired.generation;
    impl_->desired.active = true;
    if (! impl_->device.apply(impl_->desired)) return false;
    impl_->anchored.store(false, std::memory_order_release);
    impl_->needs_reanchor.store(true, std::memory_order_release);
    impl_->device_pos_at_anchor.store(0, std::memory_order_relaxed);
    return true;
}

void AvPlayer::close_device() {
    if (! impl_->desired.active) return;
    ++impl_->desired.generation;
    impl_->desired.active = false;
    (void)impl_->device.apply(impl_->desired);
    impl_->anchored.store(false, std::memory_order_release);
    impl_->needs_reanchor.store(true, std::memory_order_release);
    impl_->device_pos_at_anchor.store(0, std::memory_order_relaxed);
}

bool AvPlayer::is_device_open() const { return impl_->desired.active; }

void AvPlayer::play() {
    impl_->paused.store(false, std::memory_order_relaxed);
    impl_->desired.playing = true;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired);
}

void AvPlayer::pause() {
    impl_->paused.store(true, std::memory_order_relaxed);
    impl_->desired.playing = false;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired);
}

bool AvPlayer::is_paused() const { return impl_->paused.load(std::memory_order_relaxed); }

void AvPlayer::seek_to_start() { seek_to(0.0); }

void AvPlayer::seek_to(double seconds) {
    const bool was_playing = ! is_paused();
    impl_->desired.playing = false;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired);
    if (impl_->decoder_ptr) {
        impl_->decoder_ptr->seek_to(seconds);
    }
    impl_->anchored.store(false, std::memory_order_release);
    impl_->needs_reanchor.store(true, std::memory_order_release);
    impl_->device_pos_at_anchor.store(0, std::memory_order_relaxed);
    if (was_playing && impl_->desired.active) {
        impl_->desired.playing = true;
        (void)impl_->device.apply(impl_->desired);
    }
}

double AvPlayer::current_time_seconds() const {
    if (impl_->device.state() != AudioDeviceState::ReadyPaused &&
        impl_->device.state() != AudioDeviceState::ReadyPlaying) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (! impl_->anchored.load(std::memory_order_acquire)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto sr = impl_->device.desc().sample_rate;
    if (sr == 0) return std::numeric_limits<double>::quiet_NaN();
    const auto played = impl_->device.stream_position_frames();
    const auto base   = impl_->device_pos_at_anchor.load(std::memory_order_relaxed);
    const auto pts0   = impl_->pts_at_anchor.load(std::memory_order_relaxed);
    // played may legally drop below base across some backends after stop/start;
    // saturate to anchor pts in that case.
    if (played < base) return pts0;
    return pts0 + static_cast<double>(played - base) / static_cast<double>(sr);
}

void AvPlayer::set_volume(float v) {
    impl_->desired.volume = v;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired);
}
void AvPlayer::set_muted(bool m) {
    impl_->desired.muted = m;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired);
}
float AvPlayer::volume_scale() const { return impl_->desired.volume_scale; }
void  AvPlayer::set_volume_scale(float v) { set_volume_scale(v, 0); }
void  AvPlayer::set_volume_scale(float v, std::uint32_t fade_ms) {
    impl_->desired.volume_scale = v;
    ++impl_->desired.volume_scale_revision;
    impl_->desired.volume_scale_fade_ms = fade_ms;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired);
}

bool AvPlayer::is_eof() const { return impl_->decoder_ptr && impl_->decoder_ptr->is_eof(); }

} // namespace wavsen::audio
