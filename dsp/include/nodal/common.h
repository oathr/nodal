#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nodal {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1.0e-9f)); }
inline float midiToHz(float m) { return 440.0f * std::exp2((m - 69.0f) / 12.0f); }
inline float hzToMidi(float hz) { return 69.0f + 12.0f * std::log2(std::max(hz, 1.0f) / 440.0f); }

// One-pole coefficient reaching 63 % of a step in `seconds`, at `rate` updates per second.
inline float smoothingCoef(float seconds, float rate) {
    if (seconds <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-1.0f / (seconds * rate));
}

// xorshift32: cheap, deterministic, allocation-free randomness for the audio thread.
struct Rng {
    uint32_t state = 0x9E3779B9u;
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float uniform() { return float(next() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bipolar() { return 2.0f * uniform() - 1.0f; }                    // [-1, 1)
};

// Catmull-Rom (Hermite) interpolation between b and c, t in [0, 1).
inline float hermite(float a, float b, float c, float d, float t) {
    const float c1 = 0.5f * (c - a);
    const float c2 = a - 2.5f * b + 2.0f * c - 0.5f * d;
    const float c3 = 0.5f * (d - a) + 1.5f * (b - c);
    return ((c3 * t + c2) * t + c1) * t + b;
}

}  // namespace nodal
