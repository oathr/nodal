#pragma once

#include <algorithm>
#include <array>

#include "fft.h"

namespace nodal {

// What the analyser knows about one moment of the input (one hop, ~5.3 ms).
constexpr int kEnvPoints = 128;  // spectral envelope, 20 Hz .. 20 kHz, log-spaced

inline float envPointHz(int i) { return 20.0f * std::pow(1000.0f, float(i) / float(kEnvPoints - 1)); }
// Fractional envelope index of a frequency (log2 domain).
inline float envIndexOfLog2(float log2Hz) {
    constexpr float lo = 4.321928f;                         // log2(20)
    constexpr float perOctave = float(kEnvPoints - 1) / 9.965784f;  // points per octave over 1000:1
    return (log2Hz - lo) * perOctave;
}

struct Frame {
    float f0 = 220.0f;      // Hz; held from the last voiced frame when unvoiced
    float confidence = 0;   // 0..1, 1 = clearly periodic
    float levelDb = -120;   // window RMS
    float brightness = 0.5f;  // 0..1, spectral centroid relative to the pitch
    float tiltA = 0, tiltB = 0;  // envelope's broad slope: dB = A + B * octaves above 1 kHz
    float midi = 57.0f;     // the pitch the synth plays (MIDI), after STEADY
    std::array<float, kEnvPoints> env{};  // dB, tilt removed, peak at 0 dB
};

// Pitch: YIN (de Cheveigné & Kawahara 2002) with its difference function computed from an
// FFT autocorrelation, O(N log N) instead of O(N^2). Window N = 2048 at 44.1/48 kHz (43 ms,
// 2 periods of 47 Hz), hop N/8. The spectral envelope is the max of the spectrum within half
// a harmonic spacing of each envelope point: it follows the harmonic peaks, not the gaps.
class Analyzer {
public:
    static constexpr int kHistory = 512;  // frames kept for SPRAY (2.7 s)
    static constexpr int kSteadyMax = 64;  // STEADY's window, in frames (2 x 120 ms at 44.1 / 48 kHz: 45)

    void prepare(float sampleRate) {
        sampleRate_ = sampleRate;
        size_ = sampleRate <= 50000.0f ? 2048 : sampleRate <= 100000.0f ? 4096 : 8192;
        hop_ = size_ / 8;
        fft_.prepare(size_);
        ring_.assign(size_t(size_), 0.0f);
        frame_.assign(size_t(size_), 0.0f);
        padded_.assign(size_t(size_), 0.0f);
        windowed_.assign(size_t(size_), 0.0f);
        window_.resize(size_t(size_));
        float sum = 0.0f;
        for (int i = 0; i < size_; ++i) sum += (window_[size_t(i)] = 0.5f - 0.5f * std::cos(kTwoPi * (float(i) + 0.5f) / float(size_)));
        windowGain_ = 2.0f / sum;
        spectrumA_.assign(size_t(fft_.bins()), Complex{});
        spectrumX_.assign(size_t(fft_.bins()), Complex{});
        corr_.assign(size_t(size_), 0.0f);
        energy_.assign(size_t(size_ + 1), 0.0f);
        cmnd_.assign(size_t(size_ / 2), 1.0f);
        magnitude_.assign(size_t(size_ / 2 + 1), 0.0f);
        history_.assign(size_t(kHistory), Frame{});
        reset();
    }
    void reset() {
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        for (auto& f : history_) f = Frame{};
        for (auto& f : history_) f.env.fill(-60.0f);
        write_ = 0;
        head_ = 0;
        count_ = 0;
        recent_[0] = recent_[1] = recent_[2] = 220.0f;
        lastF0_ = 220.0f;
        recentMidi_.fill(57.0f);
        recentPos_ = recentCount_ = 0;
        dcIn_ = dcOut_ = 0.0f;
    }

    int windowSize() const { return size_; }
    int hop() const { return hop_; }
    void setThreshold(float t) { threshold_ = t; }
    // STEADY: quick round trips of the pitch are not followed. The median of the pitches over
    // the last 2 x `seconds` says where the line is; a pitch within 2.5 semitones of it (vibrato,
    // a step, a third) passes at once, one further away is replaced by the median. A real leap
    // becomes the median after `seconds` and is followed from then on: it arrives that much
    // later; an excursion shorter than `seconds` never reaches the synth. 0: off.
    void setSteady(float seconds) { steadyFrames_ = std::clamp(int(std::lround(2.0f * seconds * sampleRate_ / float(hop_))), 0, kSteadyMax); }

    // One sample in. Returns true when a new frame was just analysed.
    bool push(float x) {
        // DC blocker (~10 Hz): an offset would read as a very low "period".
        const float y = x - dcIn_ + 0.9987f * dcOut_;
        dcIn_ = x;
        dcOut_ = y;
        ring_[size_t(write_)] = y;
        write_ = (write_ + 1) & (size_ - 1);
        if (++count_ < hop_) return false;
        count_ = 0;
        analyse();
        steady(history_[size_t(head_)]);
        return true;
    }

    // Frame `lag` hops ago (0 = newest).
    const Frame& frame(int lag) const { return history_[size_t((head_ - std::clamp(lag, 0, kHistory - 1) + kHistory) % kHistory)]; }

private:
    void steady(Frame& f) {
        const float x = hzToMidi(f.f0);
        recentMidi_[size_t(recentPos_)] = x;
        recentPos_ = (recentPos_ + 1) % kSteadyMax;
        recentCount_ = std::min(recentCount_ + 1, kSteadyMax);
        const int n = std::min(steadyFrames_, recentCount_);
        if (n <= 2) { f.midi = x; return; }
        for (int i = 0; i < n; ++i) scratch_[size_t(i)] = recentMidi_[size_t((recentPos_ - 1 - i + kSteadyMax) % kSteadyMax)];
        std::nth_element(scratch_.begin(), scratch_.begin() + n / 2, scratch_.begin() + n);
        const float median = scratch_[size_t(n / 2)];
        f.midi = std::abs(x - median) <= 2.5f ? x : median;
    }

    void analyse() {
        for (int i = 0; i < size_; ++i) frame_[size_t(i)] = ring_[size_t((write_ + i) & (size_ - 1))];
        head_ = (head_ + 1) % kHistory;
        Frame& out = history_[size_t(head_)];
        const Frame& previous = history_[size_t((head_ - 1 + kHistory) % kHistory)];

        double power = 0.0;
        for (int i = 0; i < size_; ++i) power += double(frame_[size_t(i)]) * frame_[size_t(i)];
        out.levelDb = 10.0f * std::log10(float(power / size_) + 1.0e-12f);
        if (out.levelDb < -90.0f) {  // silence: nothing to measure, keep the last timbre
            const float level = out.levelDb;
            out = previous;
            out.levelDb = level;
            out.confidence = 0.0f;
            return;
        }

        // --- Pitch (YIN) -------------------------------------------------------------
        const int w = size_ / 2;  // integration window, and the longest lag
        std::fill(padded_.begin(), padded_.end(), 0.0f);
        std::copy(frame_.begin(), frame_.begin() + w, padded_.begin());
        fft_.forward(padded_.data(), spectrumA_.data());
        fft_.forward(frame_.data(), spectrumX_.data());
        for (size_t k = 0; k < spectrumA_.size(); ++k) {
            const Complex a = std::conj(spectrumA_[k]), x = spectrumX_[k];
            spectrumA_[k] = {a.real() * x.real() - a.imag() * x.imag(), a.real() * x.imag() + a.imag() * x.real()};
        }
        fft_.inverse(spectrumA_.data(), corr_.data());  // corr[tau] = sum_{j<w} x[j] x[j+tau]
        energy_[0] = 0.0f;
        for (int i = 0; i < size_; ++i) energy_[size_t(i + 1)] = energy_[size_t(i)] + frame_[size_t(i)] * frame_[size_t(i)];
        const float e0 = energy_[size_t(w)];
        const int tauMin = std::max(2, int(sampleRate_ / 4000.0f)), tauMax = w - 1;
        float running = 0.0f;
        cmnd_[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau) {
            const float d = std::max(0.0f, e0 + (energy_[size_t(tau + w)] - energy_[size_t(tau)]) - 2.0f * corr_[size_t(tau)]);
            running += d;
            cmnd_[size_t(tau)] = running > 0.0f ? d * float(tau) / running : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau) {
            if (cmnd_[size_t(tau)] < threshold_) {
                while (tau + 1 < tauMax && cmnd_[size_t(tau + 1)] < cmnd_[size_t(tau)]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) {  // no dip under the threshold: the deepest one, as a weak guess
            best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (cmnd_[size_t(tau)] < cmnd_[size_t(best)]) best = tau;
        }
        float tau = float(best);
        if (best > tauMin && best < tauMax - 1) {  // parabolic refinement, on the raw difference
            const auto diff = [&](int t) { return e0 + (energy_[size_t(t + w)] - energy_[size_t(t)]) - 2.0f * corr_[size_t(t)]; };
            const float a = diff(best - 1), b = diff(best), c = diff(best + 1);
            const float denom = a - 2.0f * b + c;
            if (std::abs(denom) > 1.0e-9f) tau += std::clamp(0.5f * (a - c) / denom, -0.5f, 0.5f);
        }
        const float dip = cmnd_[size_t(best)];
        out.confidence = std::clamp(1.0f - dip, 0.0f, 1.0f);
        const bool voiced = dip < threshold_ && out.levelDb > -75.0f;
        if (voiced) {
            // Median of the last three voiced estimates: one-frame octave slips vanish.
            recent_[0] = recent_[1];
            recent_[1] = recent_[2];
            recent_[2] = sampleRate_ / tau;
            const float a = recent_[0], b = recent_[1], c = recent_[2];
            lastF0_ = std::max(std::min(a, b), std::min(std::max(a, b), c));
        } else {
            out.confidence *= 0.5f;
        }
        out.f0 = lastF0_;

        // --- Spectrum, envelope, brightness ------------------------------------------
        for (int i = 0; i < size_; ++i) windowed_[size_t(i)] = frame_[size_t(i)] * window_[size_t(i)];
        fft_.forward(windowed_.data(), spectrumX_.data());
        const int bins = size_ / 2;
        const float binHz = sampleRate_ / float(size_);
        double centroidNum = 0.0, centroidDen = 0.0;
        for (int k = 0; k <= bins; ++k) {
            const Complex z = spectrumX_[size_t(k)];
            const float m = std::sqrt(z.real() * z.real() + z.imag() * z.imag()) * windowGain_;
            magnitude_[size_t(k)] = m;
            centroidNum += double(m) * k;
            centroidDen += m;
        }
        // High notes: YIN's lag has few samples per period (15 at 3 kHz), so the pitch is
        // refined on the fundamental's spectral peak (Hann, log-parabolic); under 1.5 kHz YIN is the finer.
        if (voiced && out.f0 > 1500.0f) {
            const int centre = int(std::lround(out.f0 / binHz));
            int peakBin = centre;
            for (int k = std::max(2, centre - 2); k <= std::min(bins - 2, centre + 2); ++k)
                if (magnitude_[size_t(k)] > magnitude_[size_t(peakBin)]) peakBin = k;
            if (peakBin >= 2 && peakBin <= bins - 2) {
                const float a = std::log(magnitude_[size_t(peakBin - 1)] + 1.0e-9f), b = std::log(magnitude_[size_t(peakBin)] + 1.0e-9f),
                            c = std::log(magnitude_[size_t(peakBin + 1)] + 1.0e-9f);
                const float denom = a - 2.0f * b + c;
                const float refined = (float(peakBin) + (denom < 0.0f ? std::clamp(0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f)) * binHz;
                if (std::abs(refined / out.f0 - 1.0f) < 0.03f) out.f0 = lastF0_ = refined;
            }
        }
        const float centroidHz = centroidDen > 0.0 ? float(centroidNum / centroidDen) * binHz : 1000.0f;
        const float reference = voiced ? out.f0 : 200.0f;
        out.brightness = std::clamp(std::log2(std::max(centroidHz, 1.0f) / reference) / 5.0f, 0.0f, 1.0f);

        const float halfWidth = voiced ? 0.5f * out.f0 : 60.0f;
        std::array<float, kEnvPoints> raw{};
        for (int i = 0; i < kEnvPoints; ++i) {
            const float hz = envPointHz(i);
            const int lo = std::clamp(int((hz * 0.97f - halfWidth) / binHz), 1, bins - 1);
            const int hi = std::clamp(int(std::ceil((hz * 1.03f + halfWidth) / binHz)), lo, bins - 1);
            float m = magnitude_[size_t(std::clamp(int(std::lround(hz / binHz)), 1, bins - 1))];
            for (int k = lo; k <= hi; ++k) m = std::max(m, magnitude_[size_t(k)]);
            raw[size_t(i)] = 20.0f * std::log10(m + 1.0e-7f);
        }
        // Light smoothing across neighbours, then the broad tilt (least squares, 100 Hz ..
        // 8 kHz) is taken out so formants can be imprinted without darkening the table.
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        for (int i = 0; i < kEnvPoints; ++i) {
            const float a = raw[size_t(std::max(0, i - 1))], b = raw[size_t(i)], c = raw[size_t(std::min(kEnvPoints - 1, i + 1))];
            out.env[size_t(i)] = 0.25f * a + 0.5f * b + 0.25f * c;
            const float hz = envPointHz(i);
            if (hz >= 100.0f && hz <= 8000.0f) {
                const double x = std::log2(hz / 1000.0f), y = out.env[size_t(i)];
                sx += x, sy += y, sxx += x * x, sxy += x * y, ++n;
            }
        }
        const double denom = n * sxx - sx * sx;
        out.tiltB = denom != 0.0 ? float((n * sxy - sx * sy) / denom) : 0.0f;
        out.tiltA = float((sy - out.tiltB * sx) / std::max(1, n));
        float peak = -200.0f;
        for (int i = 0; i < kEnvPoints; ++i) {
            out.env[size_t(i)] -= out.tiltA + out.tiltB * std::log2(envPointHz(i) / 1000.0f);
            peak = std::max(peak, out.env[size_t(i)]);
        }
        for (auto& e : out.env) e = std::max(e - peak, -60.0f);
    }

    float sampleRate_ = 48000.0f, threshold_ = 0.15f, windowGain_ = 1.0f;
    std::array<float, kSteadyMax> recentMidi_{}, scratch_{};
    int recentPos_ = 0, recentCount_ = 0, steadyFrames_ = 0;
    int size_ = 2048, hop_ = 256, write_ = 0, head_ = 0, count_ = 0;
    RealFft fft_;
    std::vector<float> ring_, frame_, padded_, windowed_, window_, corr_, energy_, cmnd_, magnitude_;
    std::vector<Complex> spectrumA_, spectrumX_;
    std::vector<Frame> history_;
    float recent_[3] = {220.0f, 220.0f, 220.0f};
    float lastF0_ = 220.0f, dcIn_ = 0.0f, dcOut_ = 0.0f;
};

}  // namespace nodal
