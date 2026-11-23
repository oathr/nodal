#pragma once

#include <array>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>

#include "akwf.h"
#include "fft.h"

namespace nodal {

// A wavetable is kept as harmonic spectra, one per frame: harmonic k (1..kHarmonics) of
// frame f is a complex amplitude c with  x(phase) = sum |c| cos(2 pi k phase + arg c).
// Every frame is normalised to an RMS of 1, so the engine's level never depends on the
// table. Spectra make band-limiting exact (harmonics over Nyquist are simply not built)
// and let the engine imprint the input's formants harmonic by harmonic.
constexpr int kHarmonics = 512;
constexpr int kPreviewPoints = 128;  // one frame's waveform for the interface
constexpr int kMaxFrames = 256;

struct Wavetable {
    std::string name;
    int frames = 0;
    std::vector<Complex> harm;      // frames * kHarmonics
    std::vector<float> preview;     // frames * kPreviewPoints, -1..1

    const Complex* frame(int f) const { return harm.data() + size_t(std::clamp(f, 0, frames - 1)) * kHarmonics; }

    // Normalises frame f to RMS 1 (silent frames stay silent) and draws its preview.
    void finishFrame(int f, RealFft& fft, std::vector<Complex>& bins, std::vector<float>& wave) {
        Complex* c = harm.data() + size_t(f) * kHarmonics;
        float energy = 0.0f;
        for (int k = 0; k < kHarmonics; ++k) energy += 0.5f * std::norm(c[k]);
        const float g = energy > 1.0e-12f ? 1.0f / std::sqrt(energy) : 0.0f;
        for (int k = 0; k < kHarmonics; ++k) c[k] *= g;
        // Preview: the frame resynthesised at 1024 points, every 8th kept.
        const int n = fft.size();
        std::fill(bins.begin(), bins.end(), Complex{});
        for (int k = 1; k <= std::min(kHarmonics, n / 2 - 1); ++k) bins[size_t(k)] = c[k - 1] * (0.5f * float(n));
        fft.inverse(bins.data(), wave.data());
        float peak = 1.0e-6f;
        for (int i = 0; i < n; ++i) peak = std::max(peak, std::abs(wave[size_t(i)]));
        for (int i = 0; i < kPreviewPoints; ++i) preview[size_t(f) * kPreviewPoints + size_t(i)] = wave[size_t(i * n / kPreviewPoints)] / peak;
    }

    void allocate(int frameCount) {
        frames = std::clamp(frameCount, 1, kMaxFrames);
        harm.assign(size_t(frames) * kHarmonics, Complex{});
        preview.assign(size_t(frames) * kPreviewPoints, 0.0f);
    }

    // From raw samples: `frameSize` samples per frame (2048 in Serum-style files). A file
    // shorter than one frame is a single cycle of its own length. Over kMaxFrames frames,
    // frames are picked evenly. Runs off the audio thread (allocates).
    static std::unique_ptr<Wavetable> fromSamples(const float* data, size_t length, int frameSize, std::string name) {
        auto t = std::make_unique<Wavetable>();
        t->name = std::move(name);
        if (length < 8) return nullptr;
        if (frameSize <= 0 || size_t(frameSize) > length) frameSize = int(length);
        const int available = int(length / size_t(frameSize));
        t->allocate(std::min(available, kMaxFrames));
        const bool pow2 = (frameSize & (frameSize - 1)) == 0;
        RealFft fft, previewFft;
        std::vector<Complex> spectrum, bins(1024 / 2 + 1);
        std::vector<float> wave(1024);
        previewFft.prepare(1024);
        if (pow2) {
            fft.prepare(frameSize);
            spectrum.resize(size_t(fft.bins()));
        }
        const int top = std::min(kHarmonics, frameSize / 2 - 1);
        for (int f = 0; f < t->frames; ++f) {
            const int source = t->frames == available ? f : int(int64_t(f) * (available - 1) / std::max(1, t->frames - 1));
            const float* x = data + size_t(source) * size_t(frameSize);
            Complex* c = t->harm.data() + size_t(f) * kHarmonics;
            if (pow2) {
                fft.forward(x, spectrum.data());
                for (int k = 1; k <= top; ++k) c[k - 1] = spectrum[size_t(k)] * (2.0f / float(frameSize));
            } else {  // odd sizes (single cycles of any length): a direct DFT
                for (int k = 1; k <= top; ++k) {
                    double re = 0.0, im = 0.0;
                    for (int i = 0; i < frameSize; ++i) {
                        const double a = -6.283185307179586 * double(k) * double(i) / double(frameSize);
                        re += x[i] * std::cos(a);
                        im += x[i] * std::sin(a);
                    }
                    c[k - 1] = Complex(float(re), float(im)) * (2.0f / float(frameSize));
                }
            }
            t->finishFrame(f, previewFft, bins, wave);
        }
        return t;
    }
};

// The factory tables: generated once, shared by every instance (about 3 MB).
class FactoryTables {
public:
    static const FactoryTables& get() {
        static const FactoryTables instance;
        return instance;
    }
    int count() const { return int(tables_.size()); }
    const Wavetable& table(int i) const { return *tables_[size_t(std::clamp(i, 0, count() - 1))]; }
    static const std::vector<std::string>& names() {
        // In the order they are built: NODAL 0.2's twelve (made here), then the AKWF tables.
        static const std::vector<std::string> n = [] {
            std::vector<std::string> list = {"Basic", "Pulse", "Sweep", "Vowel", "Hollow", "FM", "Sync", "Fold", "Digital", "Organ", "Shards", "Choir"};
            for (const auto& t : akwf::kTables) list.push_back(t.name);
            return list;
        }();
        return n;
    }

private:
    static constexpr int kFrames = 64, kSize = 4096;
    using TimeShape = std::function<float(float t, float p)>;       // morph t, phase p -> sample
    using HarmShape = std::function<Complex(float t, int k)>;       // morph t, harmonic k -> amplitude

    static float fract(float x) { return x - std::floor(x); }
    static Complex sine(float a) { return {0.0f, -a}; }  // a sine: |c| = a, phase -pi/2

    static std::unique_ptr<Wavetable> fromTime(const std::string& name, const TimeShape& shape) {
        std::vector<float> samples(size_t(kFrames) * kSize);
        for (int f = 0; f < kFrames; ++f) {
            const float t = float(f) / float(kFrames - 1);
            for (int i = 0; i < kSize; ++i) samples[size_t(f) * kSize + size_t(i)] = shape(t, float(i) / float(kSize));
        }
        return Wavetable::fromSamples(samples.data(), samples.size(), kSize, name);
    }
    static std::unique_ptr<Wavetable> fromHarmonics(const std::string& name, const HarmShape& shape) {
        auto w = std::make_unique<Wavetable>();
        w->name = name;
        w->allocate(kFrames);
        RealFft fft;
        fft.prepare(1024);
        std::vector<Complex> bins(513);
        std::vector<float> wave(1024);
        for (int f = 0; f < kFrames; ++f) {
            const float t = float(f) / float(kFrames - 1);
            for (int k = 1; k <= kHarmonics; ++k) w->harm[size_t(f) * kHarmonics + size_t(k - 1)] = shape(t, k);
            w->finishFrame(f, fft, bins, wave);
        }
        return w;
    }
    static float hash(int k) {  // deterministic 0..1 per harmonic
        uint32_t x = uint32_t(k) * 2654435761u;
        x ^= x >> 15;
        x *= 2246822519u;
        x ^= x >> 13;
        return float(x & 0xFFFFFF) / 16777216.0f;
    }
    // Formant peaks (Hz, dB) of five vowels, for a voice at 130 Hz.
    static float vowelGain(float t, float hz) {
        static const float f[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {270, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};
        static const float a[5][3] = {{0, -6, -32}, {0, -24, -30}, {0, -24, -28}, {0, -9, -40}, {0, -12, -42}};
        const float s = t * 4.0f;
        const int i = std::min(3, int(s));
        const float u = s - float(i);
        float g = 0.0f;
        for (int j = 0; j < 3; ++j) {
            const float fc = f[i][j] + u * (f[i + 1][j] - f[i][j]);
            const float db = a[i][j] + u * (a[i + 1][j] - a[i][j]);
            const float bw = 60.0f + 0.08f * fc;
            const float x = (hz - fc) / bw;
            g += dbToGain(db) / (1.0f + x * x);
        }
        return g;
    }

    // Two cycles' harmonics, A to B over the frames: the levels blend, each harmonic keeps the
    // phase of the nearer cycle (a straight mix of the two would cancel some harmonics).
    static std::unique_ptr<Wavetable> fromCycles(const akwf::Table& t) {
        auto w = std::make_unique<Wavetable>();
        w->name = t.name;
        w->allocate(kFrames);
        RealFft fft;
        fft.prepare(1024);
        std::vector<Complex> bins(513);
        std::vector<float> wave(1024);
        for (int f = 0; f < kFrames; ++f) {
            const float u = float(f) / float(kFrames - 1);
            Complex* c = w->harm.data() + size_t(f) * kHarmonics;
            for (int k = 0; k < std::min(akwf::kHarm, kHarmonics); ++k) {
                const Complex a(t.harm[2 * k], t.harm[2 * k + 1]), b(t.harm[2 * (akwf::kHarm + k)], t.harm[2 * (akwf::kHarm + k) + 1]);
                c[k] = std::polar((1.0f - u) * std::abs(a) + u * std::abs(b), std::arg(u < 0.5f ? a : b));
            }
            w->finishFrame(f, fft, bins, wave);
        }
        return w;
    }

    FactoryTables() {
        // Basic: sine -> triangle -> saw -> square.
        tables_.push_back(fromTime("Basic", [](float t, float p) {
            const auto shape = [p](int s) {
                switch (s) {
                    case 0: return std::sin(kTwoPi * p);
                    case 1: return 1.0f - 4.0f * std::abs(fract(p + 0.25f) - 0.5f);
                    case 2: return 2.0f * fract(p + 0.5f) - 1.0f;
                    default: return p < 0.5f ? 0.8f : -0.8f;
                }
            };
            const float s = t * 3.0f;
            const int i = std::min(2, int(s));
            const float u = s - float(i);
            return shape(i) * (1.0f - u) + shape(i + 1) * u;
        }));
        // Pulse: width 50 % -> 3 %.
        tables_.push_back(fromTime("Pulse", [](float t, float p) { return p < 0.5f - 0.47f * t ? 1.0f : -1.0f; }));
        // Sweep: a resonant peak climbing from harmonic 2 to 128 over a soft saw.
        tables_.push_back(fromHarmonics("Sweep", [](float t, int k) {
            const float centre = 2.0f * std::pow(64.0f, t), width = 0.18f * centre + 0.8f;
            const float x = (float(k) - centre) / width;
            return sine((0.25f / float(k)) + 1.2f * std::exp(-x * x) / std::sqrt(float(k)));
        }));
        // Vowel: A E I O U, formants of a voice at 130 Hz.
        tables_.push_back(fromHarmonics("Vowel", [](float t, int k) { return sine(vowelGain(t, 130.0f * float(k)) / std::sqrt(float(k))); }));
        // Hollow: odd harmonics only, from soft to bright.
        tables_.push_back(fromHarmonics("Hollow", [](float t, int k) {
            return (k & 1) ? sine(std::pow(float(k), -(2.2f - 1.6f * t))) : Complex{};
        }));
        // FM: carrier and a modulator an octave up, index 0 -> 8.
        tables_.push_back(fromTime("FM", [](float t, float p) { return std::sin(kTwoPi * p + 8.0f * t * t * std::sin(kTwoPi * 2.0f * p)); }));
        // Sync: a saw hard-synced at ratio 1 -> 8, faded over the master period.
        tables_.push_back(fromTime("Sync", [](float t, float p) {
            const float r = 1.0f + 7.0f * t;
            return (2.0f * fract(p * r) - 1.0f) * (1.0f - p);
        }));
        // Fold: a sine driven into a triangle folder, gain 1 -> 10.
        tables_.push_back(fromTime("Fold", [](float t, float p) {
            const float x = std::sin(kTwoPi * p) * (1.0f + 9.0f * t);
            return std::asin(std::sin(x * kPi * 0.5f)) * (2.0f / kPi);
        }));
        // Digital: a sine read through fewer and fewer phase steps (48 -> 3).
        tables_.push_back(fromTime("Digital", [](float t, float p) {
            const float steps = std::round(48.0f * (1.0f - t) + 3.0f * t);
            return std::sin(kTwoPi * std::floor(p * steps) / steps);
        }));
        // Organ: five drawbar registrations, morphed.
        tables_.push_back(fromHarmonics("Organ", [](float t, int k) {
            static const int bars[9] = {1, 2, 3, 4, 6, 8, 10, 12, 16};
            static const float reg[5][9] = {{8, 8, 6, 0, 0, 0, 0, 0, 0}, {8, 6, 8, 4, 0, 2, 0, 0, 0}, {6, 8, 8, 6, 4, 3, 0, 2, 0},
                                            {8, 0, 8, 0, 6, 0, 4, 0, 3}, {8, 8, 8, 8, 8, 8, 8, 8, 8}};
            const float s = t * 4.0f;
            const int i = std::min(3, int(s));
            const float u = s - float(i);
            for (int b = 0; b < 9; ++b)
                if (bars[b] == k) return sine((reg[i][b] * (1.0f - u) + reg[i + 1][b] * u) / 8.0f);
            return Complex{};
        }));
        // Shards: a sparse random set of harmonics, more and more of them.
        tables_.push_back(fromHarmonics("Shards", [](float t, int k) {
            if (k == 1) return sine(0.6f);
            const float keep = 0.04f + 0.5f * t;
            return hash(k) < keep && k < 160 ? Complex(std::polar(1.0f / std::pow(float(k), 0.6f), kTwoPi * hash(k + 7919))) : Complex{};
        }));
        // Choir: soft vowels O -> A with scattered phases (smoother, less buzzy than Vowel).
        tables_.push_back(fromHarmonics("Choir", [](float t, int k) {
            const float g = vowelGain(0.75f - 0.75f * t, 130.0f * float(k)) / float(k);
            return Complex(std::polar(g, kTwoPi * hash(k * 31 + 5)));
        }));
        // The Adventure Kid Waveforms (CC0): 40 tables, each from two single cycles of one
        // family, POSITION morphing from the first to the second.
        for (int i = 0; i < akwf::kCount; ++i) tables_.push_back(fromCycles(akwf::kTables[i]));
    }

    std::vector<std::unique_ptr<Wavetable>> tables_;
};

}  // namespace nodal
