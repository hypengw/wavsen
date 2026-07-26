#include <complex>

import rstd;
import wavsen.audio.capture_dsp;

namespace
{

constexpr float kSampleRate = 48000.0f;

using namespace rstd::prelude;

using Buffer = rstd::array<std::complex<float>, wavsen::audio::dsp::kFftSize>;

void fill_sine(Buffer& left, Buffer& right, float hz, float left_amp, float right_amp) {
    for (rstd::size_t i = 0; i < wavsen::audio::dsp::kFftSize; ++i) {
        const float t   = static_cast<float>(i) / kSampleRate;
        const float s   = (f32(2.0f) * f32::consts::PI * f32(hz) * f32(t)).sin().to_primitive();
        const float w   = wavsen::audio::dsp::hann_window(i, wavsen::audio::dsp::kFftSize);
        left[usize(i)]  = std::complex<float>(left_amp * s * w, 0.0f);
        right[usize(i)] = std::complex<float>(right_amp * s * w, 0.0f);
    }
    wavsen::audio::dsp::fft_inplace(left.data(), wavsen::audio::dsp::kFftSize);
    wavsen::audio::dsp::fft_inplace(right.data(), wavsen::audio::dsp::kFftSize);
}

rstd::size_t expected_band(const wavsen::audio::dsp::BandLayout& layout, float hz) {
    const auto bin = wavsen::audio::dsp::hz_to_upper_bin(hz, kSampleRate);
    for (rstd::size_t k = 0; k < wavsen::audio::dsp::kNumBins; ++k) {
        if (bin >= layout.edges[usize(k)] && bin < layout.edges[usize(k + 1)]) return k;
    }
    return wavsen::audio::dsp::kNumBins - 1;
}

rstd::size_t peak_band(const rstd::array<float, wavsen::audio::dsp::kNumBins>& values) {
    rstd::size_t band = 0;
    float        peak = values[usize()];
    for (rstd::size_t k = 1; k < wavsen::audio::dsp::kNumBins; ++k) {
        if (values[usize(k)] > peak) {
            band = k;
            peak = values[usize(k)];
        }
    }
    return band;
}

float band_center_x(rstd::size_t band) {
    return (static_cast<float>(band) + 0.5f) / static_cast<float>(wavsen::audio::dsp::kNumBins);
}

bool test_frequency_mapping_matches_we_response_samples() {
    const auto layout = wavsen::audio::dsp::make_we_layout(kSampleRate);
    struct Case {
        float hz;
        float expected_x;
        float tolerance;
    };
    const rstd::array<Case, 9> tones {
        Case { 60.0f, 0.04f, 0.03f },    Case { 125.0f, 0.08f, 0.03f },
        Case { 250.0f, 0.16f, 0.03f },   Case { 500.0f, 0.33f, 0.03f },
        Case { 1000.0f, 0.51f, 0.03f },  Case { 2000.0f, 0.60f, 0.03f },
        Case { 3000.0f, 0.72f, 0.03f },  Case { 8000.0f, 0.85f, 0.03f },
        Case { 12000.0f, 0.94f, 0.03f },
    };

    rstd::size_t previous = 0;
    bool         first    = true;
    for (const auto& tone : tones) {
        Buffer left {};
        Buffer right {};
        fill_sine(left, right, tone.hz, 0.25f, 0.25f);
        const auto raw = wavsen::audio::dsp::analyze_stereo_spectrum(
            left.data(),
            right.data(),
            layout,
            2.0f / static_cast<float>(wavsen::audio::dsp::kFftSize));
        const auto  actual   = peak_band(raw.average);
        const float actual_x = band_center_x(actual);
        if (f32(actual_x - tone.expected_x).abs() > f32(tone.tolerance)) {
            rstd::io::eprintln("tone {} Hz peaked at x {}, expected near {}",
                               f32(tone.hz),
                               f32(actual_x),
                               f32(tone.expected_x));
            return false;
        }
        if (! first && actual <= previous) {
            rstd::io::eprintln("tone bands are not increasing at {} Hz", f32(tone.hz));
            return false;
        }
        first    = false;
        previous = actual;
    }
    return true;
}

bool test_channel_split_and_average() {
    const auto layout = wavsen::audio::dsp::make_we_layout(kSampleRate);
    Buffer     left {};
    Buffer     right {};
    fill_sine(left, right, 1000.0f, 1.0f, 0.0f);
    const auto raw = wavsen::audio::dsp::analyze_stereo_spectrum(
        left.data(), right.data(), layout, 2.0f / static_cast<float>(wavsen::audio::dsp::kFftSize));
    const auto band  = peak_band(raw.left);
    const auto index = usize(band);

    if (raw.left[index] <= 0.0f || raw.left[index] > wavsen::audio::dsp::kResponseCeil) {
        rstd::io::eprintln("left response outside expected range: {}", f32(raw.left[index]));
        return false;
    }
    if (raw.right[index] != 0.0f) {
        rstd::io::eprintln("silent right channel produced {}", f32(raw.right[index]));
        return false;
    }
    const float expected_average = raw.left[index] * 0.5f;
    if (f32(raw.average[index] - expected_average).abs() > f32(1.0e-5f)) {
        rstd::io::eprintln(
            "average mismatch: {} vs {}", f32(raw.average[index]), f32(expected_average));
        return false;
    }
    return true;
}

bool test_response_cap() {
    const auto layout = wavsen::audio::dsp::make_we_layout(kSampleRate);
    Buffer     left {};
    Buffer     right {};
    fill_sine(left, right, 1000.0f, 4.0f, 4.0f);
    const auto raw = wavsen::audio::dsp::analyze_stereo_spectrum(
        left.data(), right.data(), layout, 2.0f / static_cast<float>(wavsen::audio::dsp::kFftSize));
    for (float v : raw.average) {
        if (v > wavsen::audio::dsp::kResponseCeil) {
            rstd::io::eprintln("response exceeded cap: {}", f32(v));
            return false;
        }
    }
    return true;
}

float response_for_unit(const wavsen::audio::dsp::BandLayout& layout, rstd::size_t band,
                        float unit) {
    const float db          = wavsen::audio::dsp::kDbFloor +
                              unit * (wavsen::audio::dsp::kDbCeil - wavsen::audio::dsp::kDbFloor);
    const float compensated = f32(10.0f).powf(f32(db / 20.0f)).to_primitive();
    return wavsen::audio::dsp::visual_response(
        compensated / layout.gain[usize(band)], layout, band);
}

bool test_response_contrast() {
    const auto  layout = wavsen::audio::dsp::make_we_layout(kSampleRate);
    const auto  band   = expected_band(layout, 1000.0f);
    const float low    = response_for_unit(layout, band, 0.4f);
    const float mid    = response_for_unit(layout, band, 0.5f);
    const float high   = response_for_unit(layout, band, 0.6f);

    if (low >= 0.4f) {
        rstd::io::eprintln("low response was not reduced: {}", f32(low));
        return false;
    }
    if (f32(mid - 0.5f).abs() > f32(1.0e-5f)) {
        rstd::io::eprintln("mid response moved: {}", f32(mid));
        return false;
    }
    if (high <= 0.6f || high - mid <= 0.1f) {
        rstd::io::eprintln("high response was not expanded: {}", f32(high));
        return false;
    }
    return true;
}

bool test_short_neighbor_bars_survive_response() {
    const auto layout = wavsen::audio::dsp::make_we_layout(kSampleRate);
    Buffer     left {};
    Buffer     right {};
    fill_sine(left, right, 500.0f, 0.25f, 0.25f);
    const auto raw = wavsen::audio::dsp::analyze_stereo_spectrum(
        left.data(), right.data(), layout, 2.0f / static_cast<float>(wavsen::audio::dsp::kFftSize));
    const auto band = peak_band(raw.average);
    if (band == 0) {
        rstd::io::eprintln("500 Hz peak has no left neighbor");
        return false;
    }
    const float neighbor_ratio = raw.average[usize(band - 1)] / raw.average[usize(band)];
    if (neighbor_ratio < 0.35f) {
        rstd::io::eprintln("short 500 Hz neighbor bar was suppressed: {}", f32(neighbor_ratio));
        return false;
    }
    return true;
}

bool test_wide_band_single_bin_spike_is_damped() {
    const auto         layout = wavsen::audio::dsp::make_we_layout(kSampleRate);
    Buffer             spectrum {};
    const rstd::size_t band = wavsen::audio::dsp::kNumBins - 1;
    const rstd::size_t lo   = layout.edges[usize(band)];
    const rstd::size_t hi   = layout.edges[usize(band + 1)];
    if (hi - lo < 16) {
        rstd::io::eprintln("test requires a wide high-frequency band");
        return false;
    }

    spectrum[usize(lo)] = std::complex<float>(1.0f, 0.0f);
    const float mag     = wavsen::audio::dsp::band_magnitude(spectrum.data(), layout, band, 1.0f);
    if (mag >= 0.2f) {
        rstd::io::eprintln("single-bin spike was not damped in wide band: {}", f32(mag));
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (! test_frequency_mapping_matches_we_response_samples()) return 1;
    if (! test_channel_split_and_average()) return 1;
    if (! test_response_cap()) return 1;
    if (! test_response_contrast()) return 1;
    if (! test_short_neighbor_bars_survive_response()) return 1;
    if (! test_wide_band_single_bin_spike_is_damped()) return 1;
    return 0;
}
