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
                               {"8 voices + spray 2 s, grain 20 ms", Setup(0).set("voices", 7).set("formant", 1.0f).set("breath", 1.0f).set("spray", 1.0f)
                                                                       .set("grain", 0.0f).set("drive", 0.5f).set("tone", 0.6f).set("sub", 0.8f)}};
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
        std::printf("  %-8s centroid %5.0f Hz, above 4 kHz %4.1f %%, within 5 %% of a harmonic %4.1f %%\n", names[mode], num / den,
                    100.0 * high / den, 100.0 * harm / den);
    }
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
        measurePitch();
        measureLevel();
        measureAliasing();
        measureCpu();
        measureBreath();
        measurePresets();
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
