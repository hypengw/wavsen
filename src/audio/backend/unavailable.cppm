export module wavsen.audio.backend.unavailable;
export import wavsen.audio.backend;
import rstd;
using namespace rstd::prelude;

export namespace wavsen::audio::backend
{
class UnavailableOutput {
public:
    explicit UnavailableOutput(OutputSink) {}
    auto dispatch(ControlFunction function, void* context) -> bool {
        function(context);
        return true;
    }
    auto open(const AudioClientIdentity&) -> Result<DeviceDesc, Error> {
        return Err(Error { ErrorKind::Unavailable });
    }
    void set_playing(bool) {}
    void flush() {}
    void close() {}
};
class UnavailableCapture {
public:
    explicit UnavailableCapture(CaptureSink) {}
    auto start() -> Result<DeviceDesc, Error> { return Err(Error { ErrorKind::Unavailable }); }
    void stop() {}
    auto is_running() const -> bool { return false; }
};
static_assert(rstd::mtp::check_trait<Output, UnavailableOutput>());
static_assert(rstd::mtp::check_trait<Capture, UnavailableCapture>());
}
