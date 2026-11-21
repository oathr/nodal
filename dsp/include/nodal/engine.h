#pragma once

#include <atomic>

#include "analyzer.h"
#include "wavetable.h"

namespace nodal {

// NODAL: the input is cut into hops (~5.3 ms); each hop is analysed (pitch, loudness,
// spectral envelope, brightness) and drives up to 8 wavetable voices.
//  - Every voice rebuilds its single cycle once per hop from the table's spectra: only the
//    harmonics under Nyquist for its pitch (band-limited by construction), optionally
//    shaped by the input's formants. The new cycle is crossfaded in over the hop.
//  - SPRAY: each voice reads its own moment of the past (up to 2 s), grain by grain, so a
//    phrase becomes a cloud of its own notes. FREEZE holds one moment.
//  - The level follows the input sample by sample (envelope follower): articulation and
//    dynamics stay the performer's.
// Latency: ALIGNED delays the dry and the envelope by half an analysis window (1024 samples
// at 48 kHz, reported to the host), so the synth sits on the notes; LIVE adds none.
// QUANTIZE: Off, Chromatic, then the scales of Ableton Live's list, in its order. Degrees in
// semitones from the key (-1 ends a list).
constexpr int kScaleCount = 37;
inline const char* scaleName(int s) {
    static const char* names[kScaleCount] = {
        "Off", "Chromatic", "Major", "Minor", "Dorian", "Mixolydian", "Lydian", "Phrygian", "Locrian", "Whole Tone",
        "Half-whole Dim.", "Whole-half Dim.", "Minor Blues", "Minor Pentatonic", "Major Pentatonic", "Harmonic Minor",
        "Harmonic Major", "Dorian #4", "Phrygian Dominant", "Melodic Minor", "Lydian Augmented", "Lydian Dominant",
        "Super Locrian", "8-Tone Spanish", "Bhairav", "Hungarian Minor", "Hirajoshi", "In-Sen", "Iwato", "Kumoi",
        "Pelog Selisir", "Pelog Tembung", "Messiaen 3", "Messiaen 4", "Messiaen 5", "Messiaen 6", "Messiaen 7"};
    return names[std::clamp(s, 0, kScaleCount - 1)];
}
inline std::array<int, 12> scaleDegrees(int s) {
    static const std::array<int, 12> d[kScaleCount] = {
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},  // Off
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},            // Chromatic
        {0, 2, 4, 5, 7, 9, 11, -1},                         // Major
        {0, 2, 3, 5, 7, 8, 10, -1},                         // Minor
        {0, 2, 3, 5, 7, 9, 10, -1},                         // Dorian
        {0, 2, 4, 5, 7, 9, 10, -1},                         // Mixolydian
        {0, 2, 4, 6, 7, 9, 11, -1},                         // Lydian
        {0, 1, 3, 5, 7, 8, 10, -1},                         // Phrygian
        {0, 1, 3, 5, 6, 8, 10, -1},                         // Locrian
        {0, 2, 4, 6, 8, 10, -1},                            // Whole Tone
        {0, 1, 3, 4, 6, 7, 9, 10, -1},                      // Half-whole Dim.
        {0, 2, 3, 5, 6, 8, 9, 11, -1},                      // Whole-half Dim.
        {0, 3, 5, 6, 7, 10, -1},                            // Minor Blues
        {0, 3, 5, 7, 10, -1},                               // Minor Pentatonic
        {0, 2, 4, 7, 9, -1},                                // Major Pentatonic
        {0, 2, 3, 5, 7, 8, 11, -1},                         // Harmonic Minor
        {0, 2, 4, 5, 7, 8, 11, -1},                         // Harmonic Major
        {0, 2, 3, 6, 7, 9, 10, -1},                         // Dorian #4
        {0, 1, 4, 5, 7, 8, 10, -1},                         // Phrygian Dominant
        {0, 2, 3, 5, 7, 9, 11, -1},                         // Melodic Minor
        {0, 2, 4, 6, 8, 9, 11, -1},                         // Lydian Augmented
        {0, 2, 4, 6, 7, 9, 10, -1},                         // Lydian Dominant
        {0, 1, 3, 4, 6, 8, 10, -1},                         // Super Locrian
        {0, 1, 3, 4, 5, 6, 8, 10, -1},                      // 8-Tone Spanish
        {0, 1, 4, 5, 7, 8, 11, -1},                         // Bhairav
        {0, 2, 3, 6, 7, 8, 11, -1},                         // Hungarian Minor
        {0, 2, 3, 7, 8, -1},                                // Hirajoshi
        {0, 1, 5, 7, 10, -1},                               // In-Sen
        {0, 1, 5, 6, 10, -1},                               // Iwato
        {0, 2, 3, 7, 9, -1},                                // Kumoi
        {0, 1, 3, 7, 8, -1},                                // Pelog Selisir
        {0, 1, 5, 7, 8, -1},                                // Pelog Tembung
        {0, 2, 3, 4, 6, 7, 8, 10, 11, -1},                  // Messiaen 3
        {0, 1, 2, 5, 6, 7, 8, 11, -1},                      // Messiaen 4
        {0, 1, 5, 6, 7, 11, -1},                            // Messiaen 5
        {0, 2, 4, 5, 6, 8, 10, 11, -1},                     // Messiaen 6
        {0, 1, 2, 3, 5, 6, 7, 8, 9, 11, -1},                // Messiaen 7
    };
    auto out = d[std::clamp(s, 0, kScaleCount - 1)];
    // Brace lists shorter than 12 leave zeros after the -1: everything after it is unused.
    bool ended = false;
    for (auto& x : out) {
        if (ended) x = -1;
        if (x < 0) ended = true;
    }
    return out;
}

// BREATH spectra: FLUTE (jet noise in narrow bands on the synth's harmonics, over a soft
// airy hiss), AIR (bright hiss, a breathy voice), WHISPER (the input's vowels without its
// tilt), INPUT (the input's own spectrum, tilt included: the darkest).
enum class BreathMode { Flute = 0, Air, Whisper, Input };

struct Settings {
    float mix = 1.0f, outputGain = 1.0f;
    bool aligned = true;
    bool fixedPitch = false;
    float transpose = 0.0f;  // semitones, fine included
    int quantize = 0, key = 0;
    float fixedNote = 48.0f, glideS = 0.02f;
    float rangeLow = 12.0f, rangeHigh = 108.0f;  // RANGE (MIDI notes): octaves folded inside; C0..C8 = off
    int table = 0;  // factory index, or FactoryTables::count() for the user table
    float position = 0.25f, track = 0.0f, formant = 0.5f, formantShift = 0.0f;
    int voices = 3;
    float detuneCents = 12.0f, wtSpread = 0.15f, width = 0.6f;
    float sprayS = 0.0f, grainS = 0.12f;
    bool freeze = false;
    float releaseS = 0.12f, gateDb = -60.0f, voiced = 0.5f, threshold = 0.15f;
    float sub = 0.0f, breath = 0.15f, toneHz = 0.0f, drive = 0.0f;
    int breathMode = 0;  // BreathMode
};

class Engine {
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kCycle = 1024;

    void prepare(float sampleRate) {
        sampleRate_ = sampleRate;
        analyzer_.prepare(sampleRate);
        hop_ = analyzer_.hop();
        invHop_ = 1.0f / float(hop_);
        cycleFft_.prepare(kCycle);
        bins_.assign(size_t(kCycle / 2 + 1), Complex{});
        wave_.assign(size_t(kCycle), 0.0f);
        noiseSize_ = hop_ * 4;
        noiseFft_.prepare(noiseSize_);
        noiseBins_.assign(size_t(noiseSize_ / 2 + 1), Complex{});
        noiseFrame_.assign(size_t(noiseSize_), 0.0f);
        noiseWindow_.resize(size_t(noiseSize_));
        for (int i = 0; i < noiseSize_; ++i) noiseWindow_[size_t(i)] = 0.5f - 0.5f * std::cos(kTwoPi * float(i) / float(noiseSize_));
        noiseEnvIndex_.resize(size_t(noiseSize_ / 2 + 1));
        for (int k = 0; k <= noiseSize_ / 2; ++k)
            noiseEnvIndex_[size_t(k)] = envIndexOfLog2(std::log2(std::max(1.0f, float(k) * sampleRate / float(noiseSize_))));
        for (auto& o : noiseOla_) o.assign(size_t(noiseSize_), 0.0f);
        noiseShape_.assign(size_t(noiseSize_ / 2 + 1), 0.0f);
        noiseAir_.resize(size_t(noiseSize_ / 2 + 1));
        noiseFluteTop_.resize(size_t(noiseSize_ / 2 + 1));
        for (int k = 0; k <= noiseSize_ / 2; ++k) {
            const float f = std::max(1.0f, float(k) * sampleRate / float(noiseSize_));
            // Air: 12 dB/oct high-pass at 900 Hz, +3 dB/oct up to 3 kHz, 24 dB/oct roll-off from 8 kHz (no sting).
            const float hp = std::pow(f / 900.0f, 4.0f), lift = std::sqrt(std::min(f, 3000.0f) / 1000.0f);
            noiseAir_[size_t(k)] = hp / (1.0f + hp) * lift / (1.0f + std::pow(f / 8000.0f, 4.0f));
            // The flute's jet noise on the harmonics fades out above 5 kHz.
            noiseFluteTop_[size_t(k)] = 1.0f / (1.0f + std::pow(f / 5000.0f, 4.0f));
        }
        delaySize_ = analyzer_.windowSize() / 2 + 1;
        for (auto& d : delay_) d.assign(size_t(delaySize_), 0.0f);
        for (int k = 0; k <= kHarmonics; ++k) log2k_[size_t(k)] = k > 0 ? std::log2(float(k)) : 0.0f;
        attackCoef_ = smoothingCoef(0.001f, sampleRate);
        ampSmoothCoef_ = smoothingCoef(0.006f, sampleRate);
        paramCoef_ = smoothingCoef(0.02f, sampleRate);
        outCoef_ = smoothingCoef(0.15f, sampleRate);
        crestCoef_ = smoothingCoef(0.1f, sampleRate);
        crestReleaseCoef_ = smoothingCoef(0.05f, sampleRate);
        reset();
    }

    void reset() {
        analyzer_.reset();
        for (auto& v : voices_) v = Voice{};
        for (auto& v : voices_) v.reset();
        for (auto& o : noiseOla_) std::fill(o.begin(), o.end(), 0.0f);
        for (auto& d : delay_) std::fill(d.begin(), d.end(), 0.0f);
        noiseRead_ = 0;
        delayWrite_ = 0;
        hopPos_ = 0;
        peak_ = amp_ = crestPeak_ = crestPower_ = crestPeakAvg_ = 0.0f;
        crest_ = 0.7071f;
        gateStart_ = gateEnd_ = 0.0f;
        voicedStart_ = voicedEnd_ = 1.0f;
        breathStart_ = breathEnd_ = 0.0f;
        subPhase_ = 0.0;
        tone_[0] = tone_[1] = {};
        frozen_ = false;
        mixNow_ = settings_.mix;
        outNow_ = settings_.outputGain;
        outLevel_ = 0.0f;
        needsFirstHop_ = true;
    }

    void setSettings(const Settings& s) {
        settings_ = s;
        analyzer_.setThreshold(s.threshold);
        releaseCoef_ = smoothingCoef(std::max(0.005f, s.releaseS), sampleRate_);
    }
    // Any thread. The audio thread reads the pointer once per hop and is done with it within
    // that hop, so the caller keeps the previous table alive a little longer (see the
    // processor's retired list) and the swap needs no lock and no dropout.
    void setUserTable(const Wavetable* t) { user_.store(t, std::memory_order_release); }
    const Wavetable& table() const {
        const auto& factory = FactoryTables::get();
        if (settings_.table >= factory.count()) {
            const Wavetable* user = user_.load(std::memory_order_acquire);
            return user != nullptr ? *user : factory.table(0);
        }
        return factory.table(settings_.table);
    }

    int latencySamples(bool aligned) const { return aligned ? analyzer_.windowSize() / 2 : 0; }
    int latencySamples() const { return latencySamples(settings_.aligned); }

    void process(float* left, float* right, int numSamples) {
        const Settings& s = settings_;
        const int delay = s.aligned ? analyzer_.windowSize() / 2 : 0;
        for (int n = 0; n < numSamples; ++n) {
            const float inL = left[n], inR = right[n];
            if (analyzer_.push(0.5f * (inL + inR)) || needsFirstHop_) hopUpdate();

            // The present: the input, delayed in ALIGNED mode so it meets its own analysis.
            delay_[0][size_t(delayWrite_)] = inL;
            delay_[1][size_t(delayWrite_)] = inR;
            int readAt = delayWrite_ - delay;
            if (readAt < 0) readAt += delaySize_;
            const float dryL = delay_[0][size_t(readAt)], dryR = delay_[1][size_t(readAt)];
            if (++delayWrite_ == delaySize_) delayWrite_ = 0;

            // Envelope follower: fast peak, chosen release, then a 6 ms smoother (no ripple).
            // The peak becomes an RMS through the input's own crest factor (over ~100 ms):
            // a sine and a spiky voice both come out at their own RMS.
            const float mono = 0.5f * (dryL + dryR), a = std::abs(mono);
            peak_ += (a > peak_ ? attackCoef_ : releaseCoef_) * (a - peak_);
            crestPeak_ += (a > crestPeak_ ? attackCoef_ : crestReleaseCoef_) * (a - crestPeak_);
            crestPower_ += crestCoef_ * (mono * mono - crestPower_);
            crestPeakAvg_ += crestCoef_ * (crestPeak_ - crestPeakAvg_);
            amp_ += ampSmoothCoef_ * (crest_ * peak_ - amp_);

            const float w = std::min(1.0f, float(hopPos_) * invHop_);
            ++hopPos_;
            const float gate = gateStart_ + (gateEnd_ - gateStart_) * w;
            const float level = amp_ * gate;

            float synthL = 0.0f, synthR = 0.0f;
            if (level > 1.0e-7f) {
                for (int v = 0; v < activeVoices_; ++v) {
                    Voice& vo = voices_[size_t(v)];
                    const float x = float(vo.phase) * float(kCycle);
                    const int i = std::min(kCycle - 1, int(x));
                    const float t = x - float(i);
                    const float* c = vo.current() + i;
                    const float* p = vo.previous() + i;
                    const float now = hermite(c[0], c[1], c[2], c[3], t);
                    const float before = hermite(p[0], p[1], p[2], p[3], t);
                    const float g = vo.gainStart + (vo.gainEnd - vo.gainStart) * w;
                    const float sample = (before + w * (now - before)) * g;
                    synthL += sample * vo.panL;
                    synthR += sample * vo.panR;
                    vo.advance(vo.incStart + (vo.incEnd - vo.incStart) * w);
                }
            } else {  // silent: keep the oscillators turning, skip the reading
                for (int v = 0; v < activeVoices_; ++v) {
                    Voice& vo = voices_[size_t(v)];
                    vo.advance(vo.incStart + (vo.incEnd - vo.incStart) * w);
                }
            }
            const float voicedGain = voicedStart_ + (voicedEnd_ - voicedStart_) * w;
            const float synthGain = level * voiceNorm_ * voicedGain;
            synthL *= synthGain;
            synthR *= synthGain;
            if (s.sub > 0.0f) {
                const Voice& v0 = voices_[0];
                subPhase_ += 0.5 * double(v0.incStart + (v0.incEnd - v0.incStart) * w);
                if (subPhase_ >= 1.0) subPhase_ -= 1.0;
                const float sub = std::sin(kTwoPi * float(subPhase_)) * s.sub * level * voicedGain * 1.4142f;
                synthL += sub;
                synthR += sub;
            }
            if (s.breath > 0.0f) {
                const float b = (breathStart_ + (breathEnd_ - breathStart_) * w) * level * 1.4142f;
                synthL += noiseOla_[0][size_t(noiseRead_)] * b;
                synthR += noiseOla_[1][size_t(noiseRead_)] * b;
            }
            noiseOla_[0][size_t(noiseRead_)] = noiseOla_[1][size_t(noiseRead_)] = 0.0f;
            if (++noiseRead_ == noiseSize_) noiseRead_ = 0;

            if (s.toneHz > 0.0f) {
                synthL = tone_[0].process(synthL, toneG_, toneK_);
                synthR = tone_[1].process(synthR, toneG_, toneK_);
            }
            if (s.drive > 0.0f) {
                synthL = std::tanh(synthL * driveIn_) * driveOut_;
                synthR = std::tanh(synthR * driveIn_) * driveOut_;
            }

            mixNow_ += paramCoef_ * (s.mix - mixNow_);
            outNow_ += paramCoef_ * (s.outputGain - outNow_);
            float outL = (dryL + mixNow_ * (synthL - dryL)) * outNow_;
            float outR = (dryR + mixNow_ * (synthR - dryR)) * outNow_;
            outL = safety(outL);
            outR = safety(outR);
            outLevel_ += outCoef_ * (std::max(std::abs(outL), std::abs(outR)) - outLevel_);
            left[n] = outL;
            right[n] = outR;
        }
    }

    // Live state for the interface (written once per hop, read on the message thread).
    struct Readout {
        std::atomic<float> f0{0}, confidence{0}, levelDb{-120}, brightness{0}, outLevel{0};
        std::atomic<int> voices{0};
        std::array<std::atomic<float>, kMaxVoices> midi{}, position{}, lag{}, gain{};
        std::array<std::atomic<float>, 8> harmonics{};
        std::array<std::atomic<float>, kEnvPoints> env{};
    };
    const Readout& readout() const { return readout_; }
    const Analyzer& analyzer() const { return analyzer_; }

private:
    struct Voice {
        std::array<float, kCycle + 3> a{}, b{};
        bool aIsCurrent = true, started = false;
        double phase = 0.0;
        float incStart = 0.0f, incEnd = 0.0f;
        float gainStart = 0.0f, gainEnd = 0.0f;
        float midi = 60.0f, pos = 0.25f, panL = 1.0f, panR = 1.0f;
        int lag = 0, grainLeft = 0, grainLength = 1, quantised = -1000, octave = 0;
        float jitter = 0.0f;
        void reset() {
            a.fill(0.0f);
            b.fill(0.0f);
            started = false;
        }
        const float* current() const { return aIsCurrent ? a.data() : b.data(); }
        const float* previous() const { return aIsCurrent ? b.data() : a.data(); }
        float* nextSlot() { return aIsCurrent ? b.data() : a.data(); }  // the old cycle, overwritten
        void advance(float inc) {
            phase += double(inc);
            if (phase >= 1.0) phase -= 1.0;
        }
    };

    struct Svf {  // TPT state-variable low-pass
        float s1 = 0.0f, s2 = 0.0f;
        float process(float x, float g, float k) {
            const float hp = (x - (k + g) * s1 - s2) / (1.0f + g * (g + k));
            const float bp = g * hp + s1;
            s1 = g * hp + bp;
            const float lp = g * bp + s2;
            s2 = g * bp + lp;
            return lp;
        }
    };

    static float safety(float x) {  // transparent under 0.9, never over 1.0
        const float a = std::abs(x);
        if (a <= 0.9f) return x;
        return std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) * 10.0f), x);
    }

    static bool inScale(int quantize, int note, int key) {
        static const auto masks = [] {
            std::array<uint16_t, kScaleCount> m{};
            for (int s = 0; s < kScaleCount; ++s)
                for (const int degree : scaleDegrees(s))
                    if (degree >= 0) m[size_t(s)] |= uint16_t(1u << degree);
            return m;
        }();
        const int degree = ((note - key) % 12 + 12) % 12;
        return (masks[size_t(std::clamp(quantize, 0, kScaleCount - 1))] >> degree) & 1;
    }
    // RANGE: the note moved by whole octaves into [low, high] (an octave wide at least, so
    // every note has a place). The octave sticks while the note stays within a semitone of
    // the range, so a note on an edge does not flip back and forth; when it has to change,
    // the placement nearest the voice's last note wins: no jump that is not needed. With a
    // one-octave range an octave slip of the input never reaches the synth.
    float fold(Voice& v, float midi) const {
        const Settings& s = settings_;
        float lo = std::min(s.rangeLow, s.rangeHigh), hi = std::max(s.rangeLow, s.rangeHigh);
        if (lo <= 12.0f && hi >= 108.0f) {
            v.octave = 0;
            return midi;
        }
        if (hi - lo < 12.0f) hi = lo + 12.0f;
        const float kept = midi + 12.0f * float(v.octave);
        if (v.started && kept >= lo - 1.0f && kept <= hi + 1.0f) return kept;
        const int kMin = int(std::ceil((lo - midi) / 12.0f)), kMax = int(std::floor((hi - midi) / 12.0f));
        const float reference = v.started ? v.midi : 0.5f * (lo + hi);
        int best = kMin;
        for (int k = kMin + 1; k <= kMax; ++k)
            if (std::abs(midi + 12.0f * float(k) - reference) < std::abs(midi + 12.0f * float(best) - reference)) best = k;
        v.octave = best;
        return midi + 12.0f * float(best);
    }
    float quantise(Voice& v, float midi) const {
        const Settings& s = settings_;
        if (s.quantize <= 0) return midi;
        // Hysteresis: stay on the current note until the input is clearly nearer another.
        if (v.quantised > -1000 && inScale(s.quantize, v.quantised, s.key) && std::abs(midi - float(v.quantised)) < 0.65f) return float(v.quantised);
        const int centre = int(std::lround(midi));
        int best = centre;
        float bestDistance = 1.0e9f;
        for (int d = -6; d <= 6; ++d) {
            const int note = centre + d;
            if (!inScale(s.quantize, note, s.key)) continue;
            const float distance = std::abs(midi - float(note));
            if (distance < bestDistance) bestDistance = distance, best = note;
        }
        v.quantised = best;
        return float(best);
    }

    void hopUpdate() {
        const Settings& s = settings_;
        const bool first = needsFirstHop_;
        needsFirstHop_ = false;
        hopPos_ = 0;
        const float hopsPerSecond = sampleRate_ / float(hop_);
        const Frame& newest = analyzer_.frame(0);
        if (s.freeze && !frozen_) frozenFrame_ = newest;
        frozen_ = s.freeze;

        // Global gains, ramped over the hop.
        crest_ = crestPeakAvg_ > 1.0e-6f ? std::clamp(std::sqrt(crestPower_) / crestPeakAvg_, 0.25f, 0.7071f) : 0.7071f;
        const float ampDb = gainToDb(amp_);
        const float g = std::clamp((ampDb - s.gateDb) / 6.0f + 1.0f, 0.0f, 1.0f);
        gateStart_ = gateEnd_;
        gateEnd_ = g * g;
        const Frame& lead = frozen_ ? frozenFrame_ : newest;
        const float periodic = std::clamp((lead.confidence - 0.55f) / 0.35f, 0.0f, 1.0f);
        voicedStart_ = voicedEnd_;
        voicedEnd_ = 1.0f - s.voiced * (1.0f - periodic);
        breathStart_ = breathEnd_;
        // A flute breathes under every note; a voice mostly between them.
        const float steady = s.breathMode == int(BreathMode::Flute) ? 0.75f : s.breathMode == int(BreathMode::Air) ? 0.5f : 0.35f;
        // A touch, not a layer: squared curve, about -10 dB under the synth at 100 %, -30 dB at 30 %.
        breathEnd_ = 0.3f * s.breath * s.breath * (steady + (1.0f - steady) * (1.0f - periodic));
        if (first) gateStart_ = gateEnd_, voicedStart_ = voicedEnd_, breathStart_ = breathEnd_;

        // Output stage coefficients.
        if (s.toneHz > 0.0f) {
            toneG_ = std::tan(kPi * std::min(s.toneHz, 0.45f * sampleRate_) / sampleRate_);
            toneK_ = 1.4142f;
        }
        driveIn_ = 1.0f + 15.0f * s.drive * s.drive;
        driveOut_ = 1.0f / std::sqrt(driveIn_);

        const int count = std::clamp(s.voices, 1, kMaxVoices);
        if (count != activeVoices_) {
            for (int v = activeVoices_; v < count; ++v) {  // new voices start from voice 0's state
                Voice& nv = voices_[size_t(v)];
                nv.started = false;
                nv.phase = rng_.uniform();
                nv.gainEnd = 0.0f;
            }
            activeVoices_ = count;
        }
        voiceNorm_ = 1.0f / std::sqrt(float(count));
        const float glideCoef = smoothingCoef(s.glideS, hopsPerSecond);
        const float posCoef = smoothingCoef(0.03f, hopsPerSecond);
        const int sprayHops = std::min(Analyzer::kHistory - 1, int(s.sprayS * hopsPerSecond));
        const int grainHops = std::max(2, int(s.grainS * hopsPerSecond));
        static constexpr float kPan[kMaxVoices] = {-1.0f, 1.0f, -0.3f, 0.45f, -0.7f, 0.8f, 0.1f, -0.5f};

        const Wavetable& t = table();
        lastBuilt_ = {};
        for (int v = 0; v < count; ++v) {
            Voice& vo = voices_[size_t(v)];
            const float spread = count > 1 ? 2.0f * float(v) / float(count - 1) - 1.0f : 0.0f;

            // Which moment of the input this voice plays.
            const Frame* frame = &newest;
            float grainGain = 1.0f;
            if (frozen_) {
                frame = &frozenFrame_;
            } else if (sprayHops > 0) {
                if (--vo.grainLeft <= 0) {
                    vo.lag = int(rng_.uniform() * float(sprayHops));
                    vo.grainLength = std::max(2, int(float(grainHops) * (0.7f + 0.6f * rng_.uniform())));
                    // Voices start their grains out of step, so the cloud never pulses as one.
                    vo.grainLeft = vo.started ? vo.grainLength : 1 + int(rng_.uniform() * float(vo.grainLength));
                    vo.jitter = rng_.bipolar();
                }
                frame = &analyzer_.frame(vo.lag);
                const float progress = 1.0f - float(vo.grainLeft) / float(vo.grainLength);
                const float edge = std::clamp(std::min(progress, 1.0f - progress) / 0.25f, 0.0f, 1.0f);
                grainGain = std::sin(0.5f * kPi * edge);
            } else {
                vo.lag = 0;
                vo.grainLeft = 0;
                vo.jitter = 0.0f;
            }

            // Pitch: tracked (or fixed), transposed, folded into RANGE, quantised, glided, detuned.
            float target = s.fixedPitch ? s.fixedNote : hzToMidi(frame->f0) + s.transpose;
            if (!s.fixedPitch) target = quantise(vo, fold(vo, target));
            if (!vo.started) vo.midi = target;
            else vo.midi += glideCoef * (target - vo.midi);
            const float hz = std::clamp(midiToHz(vo.midi + spread * s.detuneCents * 0.01f), 8.0f, 0.45f * sampleRate_);
            const float inc = hz / sampleRate_;
            vo.incStart = vo.started ? vo.incEnd : inc;
            vo.incEnd = inc;

            // Table position: the knob, the input's brightness (TRACK), the unison spread and
            // each grain's own scatter.
            float pos = s.position + s.track * (frame->brightness - 0.5f) + 0.5f * s.wtSpread * spread;
            if (sprayHops > 0) pos += 0.12f * vo.jitter * std::min(1.0f, s.sprayS);
            pos = std::clamp(pos, 0.0f, 1.0f);
            vo.pos = vo.started ? vo.pos + posCoef * (pos - vo.pos) : pos;

            const float pan = kPan[v] * s.width * (count > 1 ? 1.0f : 0.0f);
            vo.panL = 1.4142f * std::cos(0.25f * kPi * (pan + 1.0f));
            vo.panR = 1.4142f * std::sin(0.25f * kPi * (pan + 1.0f));
            vo.gainStart = vo.started ? vo.gainEnd : grainGain;
            vo.gainEnd = grainGain;

            buildCycle(vo, t, *frame, std::max(vo.incStart, vo.incEnd));
            if (!vo.started) {  // nothing to crossfade from: both slots hold the new cycle
                std::copy(vo.current(), vo.current() + kCycle + 3, vo.nextSlot());
                vo.started = true;
            }
        }

        if (s.breath > 0.0f) addNoise(lead);

        // Readout.
        readout_.f0.store(newest.confidence > 0.0f ? newest.f0 : 0.0f, std::memory_order_relaxed);
        readout_.confidence.store(newest.confidence, std::memory_order_relaxed);
        readout_.levelDb.store(newest.levelDb, std::memory_order_relaxed);
        readout_.brightness.store(newest.brightness, std::memory_order_relaxed);
        readout_.outLevel.store(outLevel_, std::memory_order_relaxed);
        readout_.voices.store(count, std::memory_order_relaxed);
        for (int v = 0; v < kMaxVoices; ++v) {
            const Voice& vo = voices_[size_t(v)];
            readout_.midi[size_t(v)].store(vo.midi, std::memory_order_relaxed);
            readout_.position[size_t(v)].store(vo.pos, std::memory_order_relaxed);
            readout_.lag[size_t(v)].store(float(vo.lag) / hopsPerSecond, std::memory_order_relaxed);
            readout_.gain[size_t(v)].store(v < count ? vo.gainEnd : 0.0f, std::memory_order_relaxed);
        }
        for (int i = 0; i < kEnvPoints; ++i) readout_.env[size_t(i)].store(lead.env[size_t(i)], std::memory_order_relaxed);
    }

    // One voice's cycle for the next hop: the table's frames at its position (spectral
    // crossfade), the harmonics under 0.45 x the sample rate, the input's formants, RMS 1.
    void buildCycle(Voice& vo, const Wavetable& t, const Frame& frame, float maxInc) {
        const Settings& s = settings_;
        const int top = std::clamp(int(0.45f / std::max(maxInc, 1.0e-6f)), 1, std::min(kHarmonics, kCycle / 2 - 1));
        // Voices with the same moment, position and harmonic count share one cycle.
        if (lastBuilt_.frame == &frame && lastBuilt_.pos == vo.pos && lastBuilt_.top == top && lastBuilt_.cycle != nullptr &&
            lastBuilt_.midi == (s.formant > 0.001f ? vo.midi : 0.0f)) {
            float* out = vo.nextSlot();
            std::copy(lastBuilt_.cycle, lastBuilt_.cycle + kCycle + 3, out);
            vo.aIsCurrent = !vo.aIsCurrent;
            return;
        }
        const float fp = vo.pos * float(t.frames - 1);
        const int i0 = std::clamp(int(fp), 0, t.frames - 1), i1 = std::min(i0 + 1, t.frames - 1);
        const float u = fp - float(i0);
        const Complex* a = t.frame(i0);
        const Complex* b = t.frame(i1);

        std::fill(bins_.begin(), bins_.end(), Complex{});
        float energy = 0.0f;
        const bool shaped = s.formant > 0.001f;
        if (shaped) {
            // The envelope as linear gains first (128 exp2), then per harmonic a lookup.
            for (int i = 0; i < kEnvPoints; ++i) envGain_[size_t(i)] = std::exp2(s.formant * frame.env[size_t(i)] / 6.0206f);
        }
        const float baseLog2 = std::log2(std::max(1.0f, maxInc * sampleRate_)) - s.formantShift / 12.0f;
        for (int k = 1; k <= top; ++k) {
            Complex c = a[k - 1] + u * (b[k - 1] - a[k - 1]);
            if (shaped) {
                const float idx = std::clamp(envIndexOfLog2(baseLog2 + log2k_[size_t(k)]), 0.0f, float(kEnvPoints - 1) - 0.001f);
                const int j = int(idx);
                const float f = idx - float(j);
                c *= envGain_[size_t(j)] + f * (envGain_[size_t(j + 1)] - envGain_[size_t(j)]);
            }
            bins_[size_t(k)] = c;
            energy += 0.5f * std::norm(c);
        }
        const float scale = energy > 1.0e-12f ? 0.5f * float(kCycle) / std::sqrt(energy) : 0.0f;
        for (int k = 1; k <= top; ++k) bins_[size_t(k)] *= scale;
        cycleFft_.inverse(bins_.data(), wave_.data());

        float* out = vo.nextSlot();
        out[0] = wave_[size_t(kCycle - 1)];
        std::copy(wave_.begin(), wave_.end(), out + 1);
        out[kCycle + 1] = wave_[0];
        out[kCycle + 2] = wave_[1];
        vo.aIsCurrent = !vo.aIsCurrent;
        lastBuilt_ = {&frame, vo.pos, top, shaped ? vo.midi : 0.0f, vo.current()};

        if (&vo == &voices_[0]) {  // the first harmonics, for the Chladni plate
            float peak = 1.0e-9f;
            for (int k = 1; k <= 8; ++k) peak = std::max(peak, std::abs(bins_[size_t(std::min(k, top))]));
            for (int k = 1; k <= 8; ++k) readout_.harmonics[size_t(k - 1)].store(k <= top ? std::abs(bins_[size_t(k)]) / peak : 0.0f, std::memory_order_relaxed);
        }
    }

    // BREATH: shaped noise, one frame per hop, Hann overlap-add at 75 %, two channels of
    // independent noise. The shape depends on the mode (see BreathMode); every shape is
    // normalised to the same energy, so the modes change the colour, not the level.
    void addNoise(const Frame& frame) {
        const Settings& s = settings_;
        const auto mode = BreathMode(std::clamp(s.breathMode, 0, 3));
        const float tilt = mode == BreathMode::Input ? frame.tiltB : 0.0f;
        const float envAmount = mode == BreathMode::Air ? 0.3f : mode == BreathMode::Flute ? 0.0f : 1.0f;
        for (int i = 0; i < kEnvPoints; ++i) {
            const float db = envAmount * frame.env[size_t(i)] + tilt * std::log2(envPointHz(i) / 1000.0f);
            envGain_[size_t(i)] = std::exp2(db / 6.0206f);
        }
        const float shift = s.formantShift * float(kEnvPoints - 1) / (12.0f * 9.965784f);  // semitones -> points
        const int bins = noiseSize_ / 2;
        const float binHz = sampleRate_ / float(noiseSize_);
        // FLUTE: the synth's own note (voice 1), so the breath sits on what is heard.
        const float f0 = std::max(30.0f, voices_[0].incEnd * sampleRate_);
        const float width = std::max(0.1f, 0.75f * binHz / f0);  // in harmonics
        for (int k = 1; k < bins; ++k) {
            const float idx = std::clamp(noiseEnvIndex_[size_t(k)] - shift, 0.0f, float(kEnvPoints - 1) - 0.001f);
            const int j = int(idx);
            float m = envGain_[size_t(j)] + (idx - float(j)) * (envGain_[size_t(j + 1)] - envGain_[size_t(j)]);
            if (mode == BreathMode::Air) m *= noiseAir_[size_t(k)];
            else if (mode == BreathMode::Flute) {
                const float h = float(k) * binHz / f0, n = std::round(h), d = (h - n) / width;
                const float comb = n >= 1.0f ? std::exp(-d * d) * noiseFluteTop_[size_t(k)] / std::sqrt(n) : 0.0f;
                m = comb + 0.035f * noiseAir_[size_t(k)];
            }
            noiseShape_[size_t(k)] = m;
        }
        for (int ch = 0; ch < 2; ++ch) {
            float energy = 0.0f;
            noiseBins_[0] = noiseBins_[size_t(bins)] = Complex{};
            for (int k = 1; k < bins; ++k) {
                const float m = noiseShape_[size_t(k)];
                const Complex c = {m * noiseRng_.bipolar(), m * noiseRng_.bipolar()};
                noiseBins_[size_t(k)] = c;
                energy += std::norm(c);
            }
            if (energy < 1.0e-12f) continue;
            const float scale = float(noiseSize_) / std::sqrt(2.0f * energy) / std::sqrt(1.5f);
            for (int k = 1; k < bins; ++k) noiseBins_[size_t(k)] *= scale;
            noiseFft_.inverse(noiseBins_.data(), noiseFrame_.data());
            auto& ola = noiseOla_[size_t(ch)];
            for (int i = 0; i < noiseSize_; ++i) {
                int at = noiseRead_ + i;
                if (at >= noiseSize_) at -= noiseSize_;
                ola[size_t(at)] += noiseFrame_[size_t(i)] * noiseWindow_[size_t(i)];
            }
        }
    }

    struct Built {
        const Frame* frame = nullptr;
        float pos = -1.0f;
        int top = 0;
        float midi = 0.0f;
        const float* cycle = nullptr;
    };

    Settings settings_;
    Analyzer analyzer_;
    std::atomic<const Wavetable*> user_{nullptr};
    float sampleRate_ = 48000.0f, invHop_ = 1.0f / 256.0f;
    int hop_ = 256, hopPos_ = 0, activeVoices_ = 0;
    bool needsFirstHop_ = true, frozen_ = false;
    Frame frozenFrame_;
    std::array<Voice, kMaxVoices> voices_{};
    Built lastBuilt_;
    RealFft cycleFft_, noiseFft_;
    std::vector<Complex> bins_, noiseBins_;
    std::vector<float> wave_, noiseFrame_, noiseWindow_, noiseEnvIndex_, noiseShape_, noiseAir_, noiseFluteTop_;
    std::array<std::vector<float>, 2> noiseOla_, delay_;
    std::array<float, kHarmonics + 1> log2k_{};
    std::array<float, kEnvPoints> envGain_{};
    int noiseSize_ = 1024, noiseRead_ = 0, delaySize_ = 1, delayWrite_ = 0;
    float attackCoef_ = 0.1f, releaseCoef_ = 0.001f, ampSmoothCoef_ = 0.01f, paramCoef_ = 0.001f, outCoef_ = 0.001f;
    float peak_ = 0.0f, amp_ = 0.0f, voiceNorm_ = 1.0f;
    float crestPeak_ = 0.0f, crestPower_ = 0.0f, crestPeakAvg_ = 0.0f, crest_ = 0.7071f, crestCoef_ = 0.001f, crestReleaseCoef_ = 0.001f;
    float gateStart_ = 0, gateEnd_ = 0, voicedStart_ = 1, voicedEnd_ = 1, breathStart_ = 0, breathEnd_ = 0;
    float toneG_ = 1.0f, toneK_ = 1.4142f, driveIn_ = 1.0f, driveOut_ = 1.0f;
    float mixNow_ = 1.0f, outNow_ = 1.0f, outLevel_ = 0.0f;
    double subPhase_ = 0.0;
    std::array<Svf, 2> tone_{};
    Rng rng_, noiseRng_{0x2545F491u};  // the breath has its own: it never changes which grains SPRAY picks
    Readout readout_;
};

}  // namespace nodal
