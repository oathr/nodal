// nodal_render: offline renders and the DSP measurements.
//   nodal_render --analyse                       measurements (pitch, level, aliasing, CPU)
//   nodal_render in.wav out.wav [--preset N] [--set id=value ...]
//   nodal_render --all in.wav outdir             every preset, one file each
// Values given with --set are the parameter's plain value (0..1, or a menu index).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "../../plugin/Source/Presets.h"
#include "wav.h"

using namespace nodal;

namespace {

struct Setup {
    std::vector<float> values;
    explicit Setup(int preset = 0) { values = presets::valuesOf(presets::all()[size_t(preset)]); }
    Setup& set(const char* id, float v) {
        values[size_t(params::indexOf(id))] = v;
        return *this;
    }
    Settings settings() const { return params::toSettings(values.data()); }
};

void render(Engine& e, Stereo& audio, int block = 256) {
    for (size_t at = 0; at < audio.size(); at += size_t(block)) {
        const int n = int(std::min<size_t>(size_t(block), audio.size() - at));
        e.process(audio.left.data() + at, audio.right.data() + at, n);
    }
}

// A voice-like test source: harmonics with a 1/k slope through two formants, vibrato,
// breath noise, note by note (with silences between phrases).
Stereo voiceLike(float rate, const std::vector<float>& notes, float noteS, float levelDb, uint32_t seed = 7) {
    Stereo a;
    a.sampleRate = rate;
    a.resize(size_t(noteS * rate * float(notes.size())));
    Rng rng;
    rng.state = seed;
    double phase = 0.0;
    float noiseLp = 0.0f;
    const float gain = dbToGain(levelDb);
    for (size_t i = 0; i < a.size(); ++i) {
        const float t = float(i) / rate;
        const size_t index = std::min(notes.size() - 1, size_t(t / noteS));
        const float local = t - float(index) * noteS;
        const float midi = notes[index];
        if (midi <= 0.0f) continue;
        const float vib = 0.25f * std::sin(kTwoPi * 5.5f * t) * std::min(1.0f, local / 0.3f);
        const float f = midiToHz(midi + vib);
        phase += double(f / rate);
        phase -= std::floor(phase);
        float s = 0.0f;
        for (int k = 1; k * f < 0.45f * rate && k <= 40; ++k) {
            const float hz = k * f;
            const float formant = 1.0f / (1.0f + std::pow((hz - 700.0f) / 250.0f, 2.0f)) + 0.5f / (1.0f + std::pow((hz - 1200.0f) / 300.0f, 2.0f));
            s += std::sin(kTwoPi * float(k) * float(phase)) * formant / float(k);
        }
        noiseLp += 0.3f * (rng.bipolar() - noiseLp);
        const float env = std::min(1.0f, local / 0.04f) * std::min(1.0f, (noteS - local) / 0.05f);
        const float x = (s * 0.9f + 0.05f * noiseLp) * env * gain;
        a.left[i] = a.right[i] = x;
    }
    return a;
}

double rms(const std::vector<float>& x, size_t from, size_t to) {
    double p = 0.0;
    for (size_t i = from; i < to; ++i) p += double(x[i]) * x[i];
    return std::sqrt(p / double(std::max<size_t>(1, to - from)));
}

void measurePitch() {
    std::printf("\n== Pitch tracking (harmonic tone, 0.6 s per note, error after 80 ms) ==\n");
    for (const float rate : {44100.0f, 48000.0f, 96000.0f}) {
        double worst = 0.0;
        int octaveErrors = 0, notes = 0;
        for (float midi = 31.0f; midi <= 103.0f; midi += 1.0f) {
            Analyzer an;
            an.prepare(rate);
            const float f = midiToHz(midi);
            double phase = 0.0;
            double sumCents = 0.0;
            int frames = 0;
            for (int i = 0; i < int(0.6f * rate); ++i) {
                phase += double(f / rate);
                phase -= std::floor(phase);
                float s = 0.0f;
                for (int k = 1; k <= 12 && k * f < 0.45f * rate; ++k) s += std::sin(kTwoPi * float(k) * float(phase)) / float(k);
                if (an.push(0.3f * s) && i > int(0.08f * rate)) {
                    const float cents = 1200.0f * std::log2(an.frame(0).f0 / f);
                    if (std::abs(cents) > 600.0f) ++octaveErrors;
                    else sumCents += std::abs(cents), ++frames;
                }
            }
            ++notes;
            worst = std::max(worst, frames ? sumCents / frames : 9999.0);
        }
        std::printf("  %6.0f Hz: %d notes (G1..G7), worst mean error %.2f cents, %d frames off by an octave or more\n", rate, notes, worst,
                    octaveErrors);
    }
}

void measureLevel() {
    std::printf("\n== Level match (wet only, voice-like source) ==\n");
    for (const float db : {-30.0f, -18.0f, -6.0f}) {
        for (const int voices : {0, 3, 7}) {
            Stereo in = voiceLike(48000.0f, {57, 60, 64, 67}, 0.8f, db);
            Stereo out = in;
            Engine e;
            e.prepare(48000.0f);
            e.setSettings(Setup(0).set("voices", float(voices)).set("breath", 0.0f).settings());
            render(e, out);
            const double a = rms(in.left, 9600, in.size()), b = 0.5 * (rms(out.left, 9600, out.size()) + rms(out.right, 9600, out.size()));
            std::printf("  input %+5.0f dBFS, %d voice(s): wet %+5.1f dB vs input\n", db, voices + 1, 20.0 * std::log10(b / a));
        }
    }
}

// Energy away from the synth's own harmonics: a steady note, every voice in unison.
void measureAliasing() {
    std::printf("\n== Aliasing (steady note, 1 voice, Basic saw / Sync / Digital, energy off the harmonics) ==\n");
    const float rate = 48000.0f;
    for (const auto& [table, pos, name] : {std::tuple{0, 0.66f, "Basic saw"}, std::tuple{6, 1.0f, "Sync x8"}, std::tuple{8, 1.0f, "Digital 3 steps"}}) {
        for (const float midi : {60.0f, 84.0f, 96.0f, 103.0f}) {
            const float f = midiToHz(midi);
            Stereo in;
            in.sampleRate = rate;
            in.resize(size_t(rate));
            double phase = 0.0;
            for (size_t i = 0; i < in.size(); ++i) {
                phase += double(f / rate);
                phase -= std::floor(phase);
                float s = 0.0f;
                for (int k = 1; k <= 6 && k * f < 0.45f * rate; ++k) s += std::sin(kTwoPi * float(k) * float(phase)) / float(k);
                in.left[i] = in.right[i] = 0.25f * s;
            }
            Stereo out = in;
            Engine e;
            e.prepare(rate);
            e.setSettings(Setup(0).set("table", float(table)).set("position", pos).set("voices", 0).set("formant", 0.0f).set("breath", 0.0f)
                              .set("glide", 0.0f).settings());
            render(e, out);
            const int n = 32768;
            RealFft fft;
            fft.prepare(n);
            std::vector<float> x(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i)
                x[size_t(i)] = out.left[size_t(int(rate) - n + i)] * (0.5f - 0.5f * std::cos(kTwoPi * float(i) / float(n)));
            std::vector<Complex> X(size_t(fft.bins()));
            fft.forward(x.data(), X.data());
            double onHarm = 0.0, off = 0.0;
            const float binHz = rate / float(n);
            for (int k = 1; k < n / 2; ++k) {
                const float hz = float(k) * binHz;
                const float h = hz / f;
                const double p = std::norm(X[size_t(k)]);
                if (std::abs(h - std::round(h)) * f < 6.0f * binHz && std::round(h) >= 1.0f) onHarm += p;
                else off += p;
            }
            std::printf("  %-16s %s (%5.0f Hz): off-harmonic energy %6.1f dB\n", name, params::noteName(midi).c_str(), f,
                        10.0 * std::log10(off / onHarm + 1e-30));
        }
    }
}

void measureCpu() {
    std::printf("\n== CPU (real-time share of one core) ==\n");
    struct Case {
        const char* name;
        Setup setup;
    };
    std::vector<Case> cases = {{"1 voice, no formant, no breath", Setup(0).set("voices", 0).set("formant", 0.0f).set("breath", 0.0f)},
                               {"1 voice, no formant, breath", Setup(0).set("voices", 0).set("formant", 0.0f)},
                               {"preset 0 (2 voices)", Setup(0)},
                               {"8 voices, formant, breath, spread", Setup(0).set("voices", 7).set("formant", 1.0f).set("breath", 1.0f).set("spread", 0.7f)},
                               {"8 voices + LADDER filter, res 90 %", Setup(0).set("voices", 7).set("formant", 1.0f).set("breath", 1.0f).set("filter_type", 1.0f).set("filter_res", 0.9f)
                                                                       .set("filter_drive", 0.5f).set("drive", 0.5f).set("tone", 0.6f).set("sub", 0.8f)}};
    {
        Stereo in = voiceLike(48000.0f, {48, 55, 60, 62, 67, 72, 0, 64}, 1.25f, -12.0f);
        Analyzer an;
        an.prepare(48000.0f);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < in.size(); ++i) an.push(in.left[i]);
        const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("   48000 Hz, %-38s %5.2f %%\n", "analysis alone", 100.0 * used / (double(in.size()) / 48000.0));
    }
    for (const float rate : {48000.0f, 96000.0f}) {
        for (auto& c : cases) {
            const Stereo source = voiceLike(rate, {48, 55, 60, 62, 67, 72, 0, 64}, 1.25f, -12.0f);
            double used = 1.0e9;
            for (int run = 0; run < 5; ++run) {  // the best of five: other programs only add time
                Stereo in = source;
                Engine e;
                e.prepare(rate);
                e.setSettings(c.setup.settings());
                const auto t0 = std::chrono::steady_clock::now();
                render(e, in, 64);
                used = std::min(used, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            }
            const double length = double(source.size()) / rate;
            std::printf("  %6.0f Hz, %-38s %5.2f %%\n", rate, c.name, 100.0 * used / length);
        }
    }
}

// BREATH alone (a render with it minus the same render without: the synth is deterministic),
// its spectral centroid and its share above 4 kHz, per mode.
void measureBreath() {
    std::printf("\n== Breath colour (A4 voice-like source, breath alone) ==\n");
    const char* names[] = {"Flute", "Air", "Whisper", "Input"};
    for (int mode = 0; mode < 4; ++mode) {
        const Stereo source = voiceLike(48000.0f, {69, 69}, 1.0f, -14.0f);
        Stereo with = source, without = source;
        for (Stereo* s : {&with, &without}) {
            Engine e;
            e.prepare(48000.0f);
            e.setSettings(Setup(0).set("breath", s == &with ? 1.0f : 0.0f).set("breath_mode", float(mode)).set("voices", 0).settings());
            render(e, *s);
        }
        const int n = 32768;
        RealFft fft;
        fft.prepare(n);
        std::vector<float> x(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            x[size_t(i)] = (with.left[size_t(48000 + i)] - without.left[size_t(48000 + i)]) * (0.5f - 0.5f * std::cos(kTwoPi * float(i) / float(n)));
        std::vector<Complex> X(size_t(fft.bins()));
        fft.forward(x.data(), X.data());
        double num = 0, den = 0, high = 0, harm = 0;
        for (int k = 1; k < n / 2; ++k) {
            const double p = std::norm(X[size_t(k)]), hz = double(k) * 48000.0 / n;
            num += p * hz, den += p;
            if (hz > 4000.0) high += p;
            const double h = hz / 440.0;
            if (std::abs(h - std::round(h)) < 0.05 && h > 0.5 && hz < 9000.0) harm += p;
        }
        // Level against the synth, at full BREATH and at 15 %.
        Stereo low = source;
        {
            Engine e;
            e.prepare(48000.0f);
            e.setSettings(Setup(0).set("breath", 0.15f).set("breath_mode", float(mode)).set("voices", 0).settings());
            render(e, low);
        }
        double synth = 0, full = 0, part = 0;
        for (size_t i = 48000; i < 96000; ++i) {
            synth += double(without.left[i]) * without.left[i];
            full += std::pow(double(with.left[i]) - without.left[i], 2.0);
            part += std::pow(double(low.left[i]) - without.left[i], 2.0);
        }
        std::printf("  %-8s centroid %5.0f Hz, above 4 kHz %4.1f %%, on the harmonics %4.1f %%; level vs synth: %+5.1f dB at 100 %%, %+5.1f dB at 15 %%\n",
                    names[mode], num / den, 100.0 * high / den, 100.0 * harm / den, 10.0 * std::log10(full / synth), 10.0 * std::log10(part / synth));
    }
}

// Each preset's breath against its synth (the preset rendered with and without BREATH).
void measurePresetBreath() {
    std::printf("\n== Breath level in the presets (voice-like phrase) ==\n");
    const Stereo source = voiceLike(48000.0f, {57, 60, 64, 62, 67, 64}, 0.6f, -14.0f);
    int with = 0;
    for (size_t p = 0; p < presets::all().size(); ++p) {
        Setup s{int(p)};
        if (s.values[size_t(params::indexOf("breath"))] <= 0.0f) continue;
        ++with;
        Stereo a = source, b = source;
        for (Stereo* x : {&a, &b}) {
            Setup t = s;
            t.set("mix", 1.0f);
            if (x == &b) t.set("breath", 0.0f);
            Engine e;
            e.prepare(48000.0f);
            e.setSettings(t.settings());
            render(e, *x);
        }
        double synth = 0, breath = 0;
        for (size_t i = 4800; i < source.size(); ++i) synth += double(b.left[i]) * b.left[i], breath += std::pow(double(a.left[i]) - b.left[i], 2.0);
        std::printf("  %2zu %-24s %+6.1f dB\n", p, presets::all()[p].name, 10.0 * std::log10(breath / std::max(synth, 1e-12)));
    }
    std::printf("  %d of %zu presets have breath\n", with, presets::all().size());
}

void measurePresets() {
    std::printf("\n== Presets ==\n");
    int unknown = 0;
    for (const auto& p : presets::all())
        for (const auto& [id, v] : p.values)
            if (params::indexOf(id) < 0) ++unknown, std::printf("  %s: unknown parameter %s\n", p.name, id);
    std::printf("  %zu presets, %d unknown parameter(s)\n", presets::all().size(), unknown);
}

void measureSafety() {
    std::printf("\n== Safety (white noise 0 dBFS, silence, every preset) ==\n");
    int bad = 0;
    for (size_t p = 0; p < presets::all().size(); ++p) {
        Stereo in;
        in.sampleRate = 48000.0f;
        in.resize(48000 * 9);  // 1 s of noise, then 8 s of silence (the longest release reaches -60 dB in 6 s)
        Rng rng;
        for (size_t i = 0; i < 48000; ++i) in.left[i] = rng.bipolar(), in.right[i] = rng.bipolar();
        Engine e;
        e.prepare(48000.0f);
        e.setSettings(Setup(int(p)).settings());
        render(e, in, 333);
        float peak = 0.0f, tail = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < in.size(); ++i) {
            for (const float s : {in.left[i], in.right[i]}) {
                finite &= std::isfinite(s);
                peak = std::max(peak, std::abs(s));
                if (i > in.size() * 8 / 9) tail = std::max(tail, std::abs(s));
            }
        }
        if (!finite || peak > 1.0f || tail > 1.0e-3f) {
            ++bad;
            std::printf("  preset %zu %s: finite %d, peak %.3f, tail after 7 s of silence %.5f\n", p, presets::all()[p].name, int(finite), peak, tail);
        }
    }
    std::printf("  %s\n", bad ? "FAILURES above" : "all presets finite, under 0 dBFS, silent after the input stops");
}

// FILTER: every model and mode on white noise at full resonance with the cutoff swept up to
// the top (finite, bounded); the low-pass response at 1 kHz (what passes at 100 Hz, what is
// left at 8 kHz); the cost.
void measureFilter() {
    std::printf("\n== FILTER (48 kHz) ==\n");
    const char* names[] = {"", "LADDER", "DIODE", "MS-20", "SEM", "OTA", "DIGITAL", "FORMANT", "COMB", "PHASER"};
    const float rate = 48000.0f;
    const auto toneThrough = [&](int model, float hz) {  // RMS out / RMS in, steady state, LP at 1 kHz, RES 0
        Filter f;
        f.prepare(rate);
        f.set(Filter::Model(model), Filter::Mode::Lp, 1000.0f, 0.0f, 0.0f);
        double in = 0.0, out = 0.0;
        for (int i = 0; i < int(rate); ++i) {
            const float x = 0.1f * std::sin(kTwoPi * hz * float(i) / rate), y = f.process(0, x);
            if (i > int(rate) / 2) in += double(x) * x, out += double(y) * y;
        }
        return 10.0 * std::log10(out / in);
    };
    for (int model = 1; model < Filter::kModels; ++model) {
        float peak = 0.0f;
        bool finite = true;
        double seconds = 0.0;
        for (int mode = 0; mode < 4; ++mode) {
            Filter f;
            f.prepare(rate);
            Rng rng;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < int(2.0f * rate); ++i) {
                if (i % 16 == 0) f.set(Filter::Model(model), Filter::Mode(mode), 20.0f * std::pow(1000.0f, float(i) / (2.0f * rate)), 1.0f, 1.0f);
                const float y = f.process(0, 0.5f * rng.bipolar());
                finite = finite && std::isfinite(y);
                peak = std::max(peak, std::abs(y));
            }
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        std::printf("  %-8s noise, RES 100 %%, DRIVE 100 %%, cutoff 20 Hz -> 20 kHz, 4 modes: %s, peak %5.2f;  LP 1 kHz: 100 Hz %+5.1f dB, 8 kHz %+6.1f dB;  %.2f %% of a core per channel\n",
                    names[model], finite ? "finite" : "NOT FINITE", peak, toneThrough(model, 100.0f), toneThrough(model, 8000.0f), 100.0 * seconds / 8.0);
    }
}

// STEADY: a sung line with quick round trips (an octave, a fifth, 10 .. 45 ms) and real leaps.
// Counts the round trips that reach the synth and how late the real leaps arrive.
void measureSteady() {
    std::printf("\n== STEADY (round trips of 10..45 ms, real leaps; synth note read every 64 samples) ==\n");
    const float rate = 48000.0f;
    struct Span { float from, to, semitones; };
    const std::vector<Span> notes = {{0, .45f, 57}, {.45f, .95f, 69}, {.95f, 1.45f, 66}, {1.45f, 1.95f, 64}, {1.95f, 2.5f, 59}, {2.5f, 3.0f, 64}};
    const std::vector<Span> trips = {{.20f, .215f, 12}, {.60f, .63f, 7}, {1.10f, 1.145f, -12}, {1.65f, 1.67f, 12}, {2.15f, 2.18f, -7}};
    const std::vector<float> leaps = {.45f, 1.95f, 2.5f};
    const auto pitchAt = [&](float t) {
        float m = notes.back().semitones;
        for (const auto& n : notes) if (t >= n.from && t < n.to) m = n.semitones;
        for (const auto& x : trips) if (t >= x.from && t < x.to) m += x.semitones;
        return m;
    };
    Stereo in;
    in.sampleRate = rate;
    in.resize(size_t(3.0f * rate));
    double phase = 0.0;
    for (size_t i = 0; i < in.size(); ++i) {
        const float t = float(i) / rate, f = midiToHz(pitchAt(t) + .2f * std::sin(kTwoPi * 5.5f * t));
        phase += double(f / rate);
        phase -= std::floor(phase);
        float s = 0.0f;
        for (int k = 1; k <= 12 && k * f < .45f * rate; ++k) s += std::sin(kTwoPi * float(k) * float(phase)) / float(k);
        in.left[i] = in.right[i] = .25f * s;
    }
    for (const float ms : {0.0f, 40.0f, 80.0f}) {
        Engine e;
        e.prepare(rate);
        e.setSettings(Setup(0).set("steady", ms / 120.0f).set("glide", 0.0f).set("latency", 1.0f).settings());
        Stereo out = in;
        std::vector<std::pair<float, float>> track;  // (time, synth note)
        for (size_t at = 0; at + 64 <= out.size(); at += 64) {
            e.process(out.left.data() + at, out.right.data() + at, 64);
            track.push_back({float(at + 64) / rate, e.readout().midi[0].load()});
        }
        int heard = 0;
        for (const auto& x : trips) {
            const float wrong = pitchAt(.5f * (x.from + x.to));
            bool reached = false;
            for (const auto& [t, m] : track) if (t >= x.from && t < x.to + .08f && std::abs(m - wrong) < 1.5f) reached = true;
            heard += reached;
        }
        // A leap's delay, against the same leap with STEADY off (the analysis has its own).
        float late = 0.0f;
        for (const float at : leaps) {
            const float target = pitchAt(at + .3f);  // clear of the round trips
            for (const auto& [t, m] : track) if (t >= at && std::abs(m - target) < 1.0f) { late = std::max(late, t - at); break; }
        }
        std::printf("  STEADY %3.0f ms: %d of %zu round trips reach the synth, real leaps followed after %3.0f ms at most\n", ms, heard, trips.size(), 1000.0 * late);
    }
}

// RANGE: a line over four octaves with octave leaps; where the synth's notes land.
void measureRange() {
    std::printf("\n== RANGE (line C2..C6 with octave leaps, synth note read every 256 samples) ==\n");
    const std::vector<float> line = {36, 48, 60, 72, 84, 72, 60, 64, 67, 79, 55, 43, 62, 74, 50, 69, 81, 57, 45, 60};
    struct Case { const char* name; float lo, hi; };
    for (const Case c : {Case{"off", 12, 108}, Case{"C3-C4 (1 oct)", 48, 60}, Case{"C3-C5 (2 oct)", 48, 72}, Case{"A2-A5 (3 oct)", 45, 81}}) {
        Stereo in = voiceLike(48000.0f, line, 0.5f, -12.0f);
        Engine e;
        e.prepare(48000.0f);
        e.setSettings(Setup(0).set("range_low", params::tRange(c.lo)).set("range_high", params::tRange(c.hi)).set("glide", 0.0f).settings());
        float lowest = 200.0f, highest = -200.0f, last = -1.0f;
        int leaps = 0, outside = 0, reads = 0;
        for (size_t at = 0; at + 256 <= in.size(); at += 256) {
            e.process(in.left.data() + at, in.right.data() + at, 256);
            if (at < 9600 || e.readout().confidence.load() < 0.6f) continue;
            const float m = e.readout().midi[0].load();
            lowest = std::min(lowest, m), highest = std::max(highest, m);
            if (m < c.lo - 1.01f || m > c.hi + 1.01f) ++outside;
            if (last > 0.0f && std::abs(m - last) > 9.0f) ++leaps;
            last = m;
            ++reads;
        }
        std::printf("  %-14s synth from %5.1f to %5.1f, %3d leaps of 9+ semitones, %d of %d reads outside\n", c.name, lowest, highest, leaps, outside, reads);
    }
}

int renderFile(const std::string& inPath, const std::string& outPath, const Setup& setup) {
    Stereo audio;
    std::string error;
    if (!readWav(inPath, audio, error)) return std::printf("%s\n", error.c_str()), 1;
    Engine e;
    e.prepare(audio.sampleRate);
    e.setSettings(setup.settings());
    const size_t length = audio.size();
    audio.left.resize(length + size_t(audio.sampleRate)), audio.right.resize(length + size_t(audio.sampleRate));  // 1 s of tail
    render(e, audio);
    // Aligned mode: drop the reported latency so the file lines up with its source.
    const size_t latency = size_t(e.latencySamples());
    audio.left.erase(audio.left.begin(), audio.left.begin() + long(latency));
    audio.right.erase(audio.right.begin(), audio.right.begin() + long(latency));
    if (!writeWav(outPath, audio)) return std::printf("cannot write %s\n", outPath.c_str()), 1;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "--analyse")) {
        const auto t0 = std::chrono::steady_clock::now();
        FactoryTables::get();
        std::printf("factory tables: %.0f ms to build\n", 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        {  // every table: finite, never silent, its preview within -1 .. 1, the menu's names in step
            const auto& ft = FactoryTables::get();
            int bad = 0;
            for (int i = 0; i < ft.count(); ++i) {
                const auto& w = ft.table(i);
                bool finite = true;
                double lowest = 1e9;
                for (int f = 0; f < w.frames; ++f) {
                    double power = 0.0;
                    for (int k = 0; k < kHarmonics; ++k) {
                        const auto c = w.harm[size_t(f) * kHarmonics + size_t(k)];
                        finite = finite && std::isfinite(c.real()) && std::isfinite(c.imag());
                        power += double(std::norm(c));
                    }
                    lowest = std::min(lowest, power);
                }
                float peak = 0.0f;
                for (const float v : w.preview) peak = std::max(peak, std::abs(v));
                const bool ok = finite && lowest > 1e-6 && peak <= 1.0001f && w.name == FactoryTables::names()[size_t(i)];
                if (!ok) { ++bad; std::printf("  table %d %s: finite %d, quietest frame %.2g, preview peak %.2f\n", i, w.name.c_str(), finite, lowest, peak); }
            }
            std::printf("factory tables: %d, %d with a problem\n", ft.count(), bad);
        }
        measurePitch();
        measureLevel();
        measureAliasing();
        measureCpu();
        measureBreath();
        measurePresets();
        measurePresetBreath();
        measureFilter();
        measureSteady();
        measureRange();
        measureSafety();
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "--stress")) {  // random automation; reports the slow blocks
        const float rate = 96000.0f;
        const int block = 512;
        Engine e;
        e.prepare(rate);
        Setup setup(0);
        Rng rng;
        std::vector<float> l(block), r(block);
        const auto& specs = params::all();
        for (int b = 0; b < int(30.0f * rate / block); ++b) {
            std::string changed;
            if (rng.uniform() < 0.2f) {
                const size_t i = size_t(rng.next() % specs.size());
                float v = rng.uniform();
                if (specs[i].kind != params::Kind::Float) v = float(rng.next() % std::max<size_t>(2, specs[i].choices.size()));
                setup.values[i] = v;
                changed = specs[i].id + "=" + std::to_string(v);
            }
            for (int k = 0; k < block; ++k) l[size_t(k)] = r[size_t(k)] = rng.bipolar() * 0.5f;
            e.setSettings(setup.settings());
            const auto t0 = std::chrono::steady_clock::now();
            e.process(l.data(), r.data(), block);
            const double ms = 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (ms > 1.0) std::printf("block %5d: %6.2f ms  %s\n", b, ms, changed.c_str());
        }
        return 0;
    }
    if (argc >= 3 && !std::strcmp(argv[1], "--source")) {  // the voice-like test phrase, as a file
        Stereo s = voiceLike(48000.0f, {57, 60, 64, 62, 67, 64, 60, 0, 52, 55, 59, 57}, 0.9f, -14.0f);
        return writeWav(argv[2], s) ? 0 : 1;
    }
    if (argc >= 4 && !std::strcmp(argv[1], "--all")) {
        std::filesystem::create_directories(argv[3]);
        for (size_t p = 0; p < presets::all().size(); ++p) {
            const auto& pr = presets::all()[p];
            char name[256];
            std::snprintf(name, sizeof name, "%s/%02zu %s - %s.wav", argv[3], p, pr.category, pr.name);
            if (renderFile(argv[2], name, Setup(int(p)))) return 1;
            std::printf("%s\n", name);
        }
        return 0;
    }
    if (argc < 3) {
        std::printf("usage: nodal_render --analyse | in.wav out.wav [--preset N] [--set id=value ...] | --all in.wav outdir\n");
        return 2;
    }
    Setup setup(0);
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--preset")) setup = Setup(std::atoi(argv[i + 1]));
        else if (!std::strcmp(argv[i], "--set")) {
            const std::string kv = argv[i + 1];
            const auto eq = kv.find('=');
            setup.set(kv.substr(0, eq).c_str(), std::stof(kv.substr(eq + 1)));
        }
    }
    return renderFile(argv[1], argv[2], setup);
}
