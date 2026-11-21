#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <nodal/engine.h>

// Every parameter of NODAL, in one table shared by the processor (APVTS layout), the
// editor (one web relay each), the engine (through toSettings) and the render tool.
// Continuous parameters are stored 0..1, the control's position; the functions below turn
// them into real units, and `text` shows the same label in the interface and the DAW.
// The units are copied in design/ui.html (SPECS): keep both in step.
namespace nodal::params {

enum class Kind { Float, Bool, Choice };

struct Spec {
    std::string id;
    std::string name;
    Kind kind;
    float defaultValue;                     // 0..1, false/true, or choice index
    std::vector<std::string> choices = {};  // Choice only
    std::function<std::string(float)> text = {};
};

inline std::string fmt(const char* f, double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}
inline std::string pct(float t) { return fmt("%.0f %%", 100.0 * t); }
inline std::string bipolarPct(float t) { return fmt("%+.0f %%", 200.0 * t - 100.0); }
inline std::string msText(float s) { return s < 1.0f ? fmt("%.0f ms", 1000.0 * s) : fmt("%.2f s", s); }
inline std::string hzText(float hz) { return hz < 1000.0f ? fmt("%.0f Hz", hz) : fmt("%.1f kHz", hz / 1000.0f); }
inline std::string noteName(float midi) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int n = int(std::lround(midi));
    return std::string(names[(n % 12 + 12) % 12]) + std::to_string(n / 12 - 1);
}

// Units.
inline float outputDb(float t) { return -24.0f + 36.0f * t; }                 // -24 .. +12 dB
inline int semitones(float t) { return int(std::lround(-24.0f + 48.0f * t)); }  // -24 .. +24
inline float cents(float t) { return -100.0f + 200.0f * t; }
inline float glideS(float t) { return 1.0f * t * t; }                           // 0 .. 1 s
inline float fixedNote(float t) { return std::round(24.0f + 72.0f * t); }      // C1 .. C7
inline float formantShift(float t) { return -12.0f + 24.0f * t; }
inline float detuneCents(float t) { return 50.0f * t * t; }                     // 0 .. 50 cents
inline float sprayS(float t) { return 2.0f * t * t; }                           // 0 .. 2 s
inline float grainS(float t) { return 0.02f * std::pow(25.0f, t); }            // 20 .. 500 ms
inline float releaseS(float t) { return 0.03f * std::pow(1000.0f / 30.0f, t); }  // 30 ms .. 1 s
inline float gateDb(float t) { return -90.0f + 70.0f * t; }                    // -90 .. -20 dB
inline float threshold(float t) { return 0.05f + 0.3f * t; }                   // YIN threshold
inline float subGain(float t) { return t <= 0.0f ? 0.0f : std::pow(10.0f, (-36.0f + 36.0f * t) / 20.0f); }
inline float toneHz(float t) { return t >= 1.0f ? 0.0f : 200.0f * std::pow(100.0f, t); }  // 200 Hz .. 20 kHz, top = off
// Inverse maps, for the presets.
inline float tOutput(float db) { return (db + 24.0f) / 36.0f; }
inline float tSemitones(float st) { return (st + 24.0f) / 48.0f; }
inline float tGlide(float s) { return std::sqrt(s); }
inline float tNote(float midi) { return (midi - 24.0f) / 72.0f; }
inline float tDetune(float c) { return std::sqrt(c / 50.0f); }
inline float tSpray(float s) { return std::sqrt(s / 2.0f); }
inline float tGrain(float s) { return std::log(s / 0.02f) / std::log(25.0f); }
inline float tRelease(float s) { return std::log(s / 0.03f) / std::log(1000.0f / 30.0f); }
inline float tGate(float db) { return (db + 90.0f) / 70.0f; }
inline float tSub(float db) { return (db + 36.0f) / 36.0f; }
inline float tTone(float hz) { return std::log(hz / 200.0f) / std::log(100.0f); }
inline float tBipolar(float v) { return 0.5f + 0.5f * v; }  // -1..1

inline const std::vector<std::string> kPitchModes = {"Track", "Fixed"};
inline const std::vector<std::string> kScales = {"Off", "Chromatic", "Major", "Minor", "Pentatonic", "Harm. minor", "Dorian"};
inline const std::vector<std::string> kKeys = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline const std::vector<std::string> kVoices = {"1", "2", "3", "4", "5", "6", "7", "8"};
inline const std::vector<std::string> kLatency = {"Aligned", "Live"};
inline std::vector<std::string> tableChoices() {
    auto list = FactoryTables::names();
    list.push_back("User");
    return list;
}

inline const std::vector<Spec>& all() {
    static const std::vector<Spec> specs = {
        // OUTPUT
        {"mix", "Mix", Kind::Float, 1.0f, {}, pct},
        {"output", "Output", Kind::Float, tOutput(0.0f), {}, [](float t) { return fmt("%+.1f dB", outputDb(t)); }},
        {"latency", "Latency", Kind::Choice, 0.0f, kLatency},
        // PITCH
        {"pitch_mode", "Pitch Mode", Kind::Choice, 0.0f, kPitchModes},
        {"transpose", "Transpose", Kind::Float, 0.5f, {}, [](float t) { return fmt("%+.0f st", semitones(t)); }},
        {"fine", "Fine", Kind::Float, 0.5f, {}, [](float t) { return fmt("%+.0f ct", cents(t)); }},
        {"glide", "Glide", Kind::Float, tGlide(0.02f), {}, [](float t) { return t <= 0.0f ? std::string("off") : msText(glideS(t)); }},
        {"quantize", "Quantize", Kind::Choice, 0.0f, kScales},
        {"key", "Key", Kind::Choice, 0.0f, kKeys},
        {"fixed_note", "Fixed Note", Kind::Float, tNote(48.0f), {}, [](float t) { return noteName(fixedNote(t)); }},
        // TABLE
        {"table", "Table", Kind::Choice, 0.0f, tableChoices()},
        {"position", "Position", Kind::Float, 0.25f, {}, pct},
        {"track", "Track", Kind::Float, 0.5f, {}, bipolarPct},
        {"formant", "Formant", Kind::Float, 0.5f, {}, pct},
        {"formant_shift", "Formant Shift", Kind::Float, 0.5f, {}, [](float t) { return fmt("%+.1f st", formantShift(t)); }},
        // UNISON
        {"voices", "Voices", Kind::Choice, 2.0f, kVoices},
        {"detune", "Detune", Kind::Float, tDetune(12.0f), {}, [](float t) { return fmt("%.1f ct", detuneCents(t)); }},
        {"spread", "WT Spread", Kind::Float, 0.15f, {}, pct},
        {"width", "Width", Kind::Float, 0.6f, {}, pct},
        // SPRAY
        {"spray", "Spray", Kind::Float, 0.0f, {}, [](float t) { return t <= 0.0f ? std::string("off") : msText(sprayS(t)); }},
        {"grain", "Grain", Kind::Float, tGrain(0.12f), {}, [](float t) { return msText(grainS(t)); }},
        {"freeze", "Freeze", Kind::Bool, 0.0f},
        // DYNAMICS
        {"release", "Release", Kind::Float, tRelease(0.12f), {}, [](float t) { return msText(releaseS(t)); }},
        {"gate", "Gate", Kind::Float, tGate(-60.0f), {}, [](float t) { return fmt("%.0f dB", gateDb(t)); }},
        {"voiced", "Voiced Only", Kind::Float, 0.5f, {}, pct},
        {"sensitivity", "Sensitivity", Kind::Float, 1.0f / 3.0f, {}, pct},
        // COLOR
        {"sub", "Sub", Kind::Float, 0.0f, {}, [](float t) { return t <= 0.0f ? std::string("off") : fmt("%+.1f dB", -36.0 + 36.0 * t); }},
        {"breath", "Breath", Kind::Float, 0.15f, {}, pct},
        {"tone", "Tone", Kind::Float, 1.0f, {}, [](float t) { return t >= 1.0f ? std::string("open") : hzText(toneHz(t)); }},
        {"drive", "Drive", Kind::Float, 0.0f, {}, pct},
    };
    return specs;
}

inline int indexOf(const std::string& id) {
    const auto& s = all();
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i].id == id) return int(i);
    return -1;
}

// The engine's settings from the parameter values (in table order, plain values: 0..1 for
// floats, 0/1 for switches, the index for menus).
inline Settings toSettings(const float* v) {
    static const auto at = [] {
        std::vector<int> map;
        for (const char* id : {"mix", "output", "latency", "pitch_mode", "transpose", "fine", "glide", "quantize", "key", "fixed_note",
                               "table", "position", "track", "formant", "formant_shift", "voices", "detune", "spread", "width", "spray",
                               "grain", "freeze", "release", "gate", "voiced", "sensitivity", "sub", "breath", "tone", "drive"})
            map.push_back(indexOf(id));
        return map;
    }();
    const auto f = [&](int i) { return v[at[size_t(i)]]; };
    const auto c = [&](int i) { return int(std::lround(v[at[size_t(i)]])); };
    Settings s;
    s.mix = f(0);
    s.outputGain = std::pow(10.0f, outputDb(f(1)) / 20.0f);
    s.aligned = c(2) == 0;
    s.fixedPitch = c(3) == 1;
    s.transpose = float(semitones(f(4))) + cents(f(5)) / 100.0f;
    s.glideS = glideS(f(6));
    s.quantize = c(7);
    s.key = c(8);
    s.fixedNote = fixedNote(f(9));
    s.table = c(10);
    s.position = f(11);
    s.track = 2.0f * f(12) - 1.0f;
    s.formant = f(13);
    s.formantShift = formantShift(f(14));
    s.voices = c(15) + 1;
    s.detuneCents = detuneCents(f(16));
    s.wtSpread = f(17);
    s.width = f(18);
    s.sprayS = sprayS(f(19));
    s.grainS = grainS(f(20));
    s.freeze = c(21) != 0;
    s.releaseS = releaseS(f(22));
    s.gateDb = gateDb(f(23));
    s.voiced = f(24);
    s.threshold = threshold(f(25));
    s.sub = subGain(f(26));
    s.breath = f(27);
    s.toneHz = toneHz(f(28));
    s.drive = f(29);
    return s;
}

}  // namespace nodal::params
