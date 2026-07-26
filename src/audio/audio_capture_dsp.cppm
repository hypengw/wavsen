module;

#include <complex>

export module wavsen.audio.capture_dsp;

import rstd;

export namespace wavsen::audio::dsp
{

using namespace rstd::prelude;

inline constexpr rstd::size_t kFftSize          = 4096;
inline constexpr rstd::size_t kNumBins          = 64;
inline constexpr rstd::size_t kHalfFft          = kFftSize / 2;
inline constexpr rstd::size_t kHopSize          = 1024;
inline constexpr float        kMinFrequencyHz   = 10.0f;
inline constexpr float        kMaxFrequencyHz   = 16000.0f;
inline constexpr float        kTiltPivotHz      = 1000.0f;
inline constexpr float        kTiltExp          = 1.15f;
inline constexpr float        kDbFloor          = -100.0f;
inline constexpr float        kDbCeil           = -8.0f;
inline constexpr float        kResponseContrast = 1.6f;
inline constexpr float        kResponseScale    = 1.0f;
inline constexpr float        kResponseCeil     = 1.0f;
inline constexpr float        kAttackTimeSec    = 0.030f;
inline constexpr float        kReleaseTimeSec   = 0.140f;

struct BandLayout {
    rstd::array<rstd::size_t, kNumBins + 1> edges {};
    rstd::array<float, kNumBins>            gain {};
};

struct SpectrumBands {
    rstd::array<float, kNumBins> left {};
    rstd::array<float, kNumBins> right {};
    rstd::array<float, kNumBins> average {};
};

struct BandAnchor {
    float hz;
    float band;
};

inline constexpr rstd::array<BandAnchor, 11> kBandAnchors {
    BandAnchor { kMinFrequencyHz, 0.0f }, BandAnchor { 60.0f, 2.0f },
    BandAnchor { 125.0f, 5.0f },          BandAnchor { 250.0f, 10.0f },
    BandAnchor { 500.0f, 21.0f },         BandAnchor { 1000.0f, 32.0f },
    BandAnchor { 2000.0f, 38.0f },        BandAnchor { 3000.0f, 46.0f },
    BandAnchor { 8000.0f, 54.0f },        BandAnchor { 12000.0f, 60.0f },
    BandAnchor { 16000.0f, 64.0f },
};

inline rstd::size_t hz_to_upper_bin(float hz, float sample_rate) {
    const auto bin =
        static_cast<rstd::size_t>(f64(static_cast<double>(hz) * static_cast<double>(kFftSize) /
                                      static_cast<double>(sample_rate))
                                      .ceil()
                                      .to_primitive());
    return rstd::cmp::min(rstd::cmp::max(bin, rstd::size_t(1)), kHalfFft);
}

inline float anchor_frequency_for_band(float band) {
    if (band <= kBandAnchors[usize()].band) return kBandAnchors[usize()].hz;
    if (band >= kBandAnchors[usize(10)].band) return kBandAnchors[usize(10)].hz;

    for (rstd::size_t i = 1; i < 11; ++i) {
        const auto& hi = kBandAnchors[usize(i)];
        if (band > hi.band) continue;
        const auto& lo     = kBandAnchors[usize(i - 1)];
        const float t      = (band - lo.band) / (hi.band - lo.band);
        const auto  log_lo = f32(lo.hz).ln();
        const auto  log_hi = f32(hi.hz).ln();
        return (log_lo + (log_hi - log_lo) * f32(t)).exp().to_primitive();
    }
    return kBandAnchors[usize(10)].hz;
}

inline BandLayout make_we_layout(float sample_rate) {
    BandLayout  layout {};
    const float nyquist = sample_rate * 0.5f;
    const float max_hz  = rstd::cmp::min(kMaxFrequencyHz, nyquist);
    const auto  max_bin = hz_to_upper_bin(max_hz, sample_rate);

    for (rstd::size_t k = 0; k < kNumBins; ++k) {
        const auto  index = usize(k);
        const float hz =
            rstd::cmp::min(anchor_frequency_for_band(static_cast<float>(k) - 0.5f), max_hz);
        rstd::size_t next = hz_to_upper_bin(hz, sample_rate);
        if (k > 0 && next <= layout.edges[usize(k - 1)]) {
            next = layout.edges[usize(k - 1)] + 1;
        }
        const rstd::size_t remaining = kNumBins - k;
        if (next + remaining > max_bin) next = max_bin - remaining;
        layout.edges[index] = next;
    }
    layout.edges[usize(kNumBins)] = max_bin;
    for (rstd::size_t k = 0; k < kNumBins; ++k) {
        const auto  index    = usize(k);
        const float upper_hz = static_cast<float>(layout.edges[usize(k + 1)]) * sample_rate /
                               static_cast<float>(kFftSize);
        layout.gain[index]   = f32(upper_hz / kTiltPivotHz).powf(f32(kTiltExp)).to_primitive();
    }
    return layout;
}

inline float band_magnitude(const std::complex<float>* values, const BandLayout& layout,
                            rstd::size_t band, float norm) {
    const auto         index = usize(band);
    const rstd::size_t lo    = layout.edges[index];
    const rstd::size_t hi    = layout.edges[usize(band + 1)];
    float              sum   = 0.0f;
    for (rstd::size_t i = lo; i < hi; ++i) {
        const auto value = values[i];
        sum += f32(value.real() * value.real() + value.imag() * value.imag()).sqrt().to_primitive();
    }
    const float width = static_cast<float>(rstd::cmp::max(hi - lo, rstd::size_t(1)));
    return (sum / width) * norm;
}

inline float shape_response(float unit) {
    const auto x = f32(unit).clamp(f32(), f32(1.0f));
    if (x <= f32(0.5f)) {
        return (f32(0.5f) * (x * f32(2.0f)).powf(f32(kResponseContrast))).to_primitive();
    }
    return (f32(1.0f) - f32(0.5f) * ((f32(1.0f) - x) * f32(2.0f)).powf(f32(kResponseContrast)))
        .to_primitive();
}

inline float visual_response(float magnitude, const BandLayout& layout, rstd::size_t band) {
    const float compensated = rstd::cmp::max(magnitude * layout.gain[usize(band)], 1.0e-12f);
    const float db          = (f32(20.0f) * f32(compensated).log10()).to_primitive();
    const float unit =
        f32((db - kDbFloor) / (kDbCeil - kDbFloor)).clamp(f32(), f32(1.0f)).to_primitive();
    return rstd::cmp::min(shape_response(unit) * kResponseScale, kResponseCeil);
}

inline SpectrumBands analyze_stereo_spectrum(const std::complex<float>* left,
                                             const std::complex<float>* right,
                                             const BandLayout& layout, float norm) {
    SpectrumBands raw {};
    for (rstd::size_t k = 0; k < kNumBins; ++k) {
        const auto index   = usize(k);
        raw.left[index]    = visual_response(band_magnitude(left, layout, k, norm), layout, k);
        raw.right[index]   = visual_response(band_magnitude(right, layout, k, norm), layout, k);
        raw.average[index] = 0.5f * (raw.left[index] + raw.right[index]);
    }
    return raw;
}

inline float smooth_value(float previous, float current, float dt_seconds) {
    const float tau    = current > previous ? kAttackTimeSec : kReleaseTimeSec;
    const float amount = (f32(1.0f) - f32(-dt_seconds / tau).exp()).to_primitive();
    return previous + amount * (current - previous);
}

inline SpectrumBands smooth_spectrum(const SpectrumBands& raw, SpectrumBands& state,
                                     float dt_seconds) {
    SpectrumBands out {};
    for (rstd::size_t k = 0; k < kNumBins; ++k) {
        const auto index   = usize(k);
        state.left[index]  = smooth_value(state.left[index], raw.left[index], dt_seconds);
        state.right[index] = smooth_value(state.right[index], raw.right[index], dt_seconds);
        out.left[index]    = state.left[index];
        out.right[index]   = state.right[index];
        out.average[index] = 0.5f * (out.left[index] + out.right[index]);
    }
    return out;
}

inline void fft_inplace(std::complex<float>* data, rstd::size_t count) {
    for (rstd::size_t i = 1, j = 0; i < count; ++i) {
        rstd::size_t bit = count >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            auto value = data[i];
            data[i]    = data[j];
            data[j]    = value;
        }
    }
    for (rstd::size_t length = 2; length <= count; length <<= 1) {
        const auto angle = -f32(2.0f) * f32::consts::PI / f32(static_cast<float>(length));
        const std::complex<float> rotation(angle.cos().to_primitive(), angle.sin().to_primitive());
        for (rstd::size_t i = 0; i < count; i += length) {
            std::complex<float> value(1.0f, 0.0f);
            const rstd::size_t  half = length >> 1;
            for (rstd::size_t k = 0; k < half; ++k) {
                const auto lhs     = data[i + k];
                const auto rhs     = data[i + k + half] * value;
                data[i + k]        = lhs + rhs;
                data[i + k + half] = lhs - rhs;
                value *= rotation;
            }
        }
    }
}

inline float hann_window(rstd::size_t index, rstd::size_t count) {
    const auto angle = f32(2.0f) * f32::consts::PI * f32(static_cast<float>(index)) /
                       f32(static_cast<float>(count - 1));
    return (f32(0.5f) * (f32(1.0f) - angle.cos())).to_primitive();
}

} // namespace wavsen::audio::dsp
