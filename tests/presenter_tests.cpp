import rstd;
import wavsen.video;

using namespace rstd::prelude;

int main() {
    wavsen::video::Presenter presenter(rstd::time::Duration::from_millis(u64(100)),
                                       rstd::time::Duration::from_secs(u64(1)));
    presenter.set_external_clock([] {
        return f64(1.15);
    });

    if (presenter.present_frame(f64(1.0))) return 1;
    if (! presenter.set_playback_rate(f64(2.0))) return 2;
    if (! presenter.present_frame(f64(1.0))) return 3;
    if (presenter.playback_rate() != f64(2.0)) return 4;
    if (presenter.set_playback_rate(f64())) return 5;
    if (presenter.set_playback_rate(f64::NAN_)) return 6;
    if (presenter.set_playback_rate(f64(-1.0))) return 7;
    if (presenter.set_playback_rate(f64::INFINITY_)) return 8;
    if (presenter.playback_rate() != f64(2.0)) return 9;
    return 0;
}
