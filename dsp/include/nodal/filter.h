#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "common.h"

namespace nodal {

// FILTER: one stereo filter on the synth, nine models. Every model is zero-delay feedback
// (topology-preserving transform: no unit delay in a loop, stable up to Nyquist), its
// saturation where the circuit has it. MODE picks the response where the model has more
// than one: LP, BP, HP, NOTCH (a model without one gives its nearest: COMB + for LP / BP,
// - for HP / NOTCH).
//
//  LADDER   four transistor stages and their feedback (Moog): 24 dB, the input saturates.
//           BP, HP, NOTCH mixed from the stages (as the Oberheim Xpander does)
//  DIODE    a diode ladder (TB-303): each stage saturates, a gentler self-oscillation, the
//           lows thinned in the feedback when the resonance is high
//  MS-20    Sallen-Key 12 dB (Korg 35): the resonance goes through a hard saturation and
//           screams
//  SEM      state-variable 12 dB (Oberheim SEM), soft: LP, BP, HP, NOTCH
//  OTA      four OTA stages (CEM 3320, Prophet-5): each stage saturates, rounder than LADDER
//  DIGITAL  a clean state-variable, no saturation
//  FORMANT  three band-passes on a vowel: CUTOFF slides A → E → I → O → U, RES sharpens
//  COMB     a delay of one period of CUTOFF fed back (+ or -): a pitched resonance
//  PHASER   four all-pass stages around CUTOFF, fed back by RES, mixed with the input
class Filter {
public:
    enum class Model { Off = 0, Ladder, Diode, Ms20, Sem, Ota, Digital, Formant, Comb, Phaser };
    enum class Mode { Lp = 0, Bp, Hp, Notch };
    static constexpr int kModels = 10;

    void prepare(float sampleRate) {
        sampleRate_ = sampleRate;
        combSize_ = int(sampleRate / 15.0f) + 4;  // one period at 15 Hz
        for (auto& c : ch_) c.comb.assign(size_t(combSize_), 0.0f);
        reset();
    }
    void reset() {
        for (auto& c : ch_) {
            c.s.fill(0.0f);
            c.f.fill(0.0f);
            c.ap.fill(0.0f);
            std::fill(c.comb.begin(), c.comb.end(), 0.0f);
            c.combPos = 0;
            c.combLp = c.fb = c.hpState = 0.0f;
        }
    }

    // Settings for the next samples (called every few samples: the cutoff may move).
    void set(Model model, Mode mode, float cutoffHz, float res, float drive) {
        model_ = model;
        mode_ = mode;
        res_ = std::clamp(res, 0.0f, 1.0f);
        const float fc = std::clamp(cutoffHz, 16.0f, 0.45f * sampleRate_);
        fc_ = fc;
        g_ = std::tan(kPi * fc / sampleRate_);
        G_ = g_ / (1.0f + g_);
        hpA_ = std::exp(-kTwoPi * 120.0f / sampleRate_);
        driveIn_ = 1.0f + 7.0f * drive * drive;
        driveOut_ = 1.0f / std::sqrt(driveIn_);
        if (model == Model::Formant) {
            // The vowel: CUTOFF's position from 150 Hz to 6 kHz on A E I O U (a voice at 130 Hz).
            static const float f[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {270, 2300, 3000}, {450, 800, 2830}, {325, 700, 2530}};
            static const float a[5][3] = {{0, -6, -32}, {0, -24, -30}, {0, -24, -28}, {0, -9, -40}, {0, -12, -42}};
            const float s = std::clamp(std::log(fc / 150.0f) / std::log(40.0f), 0.0f, 1.0f) * 4.0f;
            const int i = std::min(3, int(s));
            const float u = s - float(i);
            for (int j = 0; j < 3; ++j) {
                const float hz = std::min(f[i][j] + u * (f[i + 1][j] - f[i][j]), 0.45f * sampleRate_);
                formantG_[size_t(j)] = std::tan(kPi * hz / sampleRate_);
                formantGain_[size_t(j)] = dbToGain(a[i][j] + u * (a[i + 1][j] - a[i][j]));
            }
            formantR_ = 0.5f / (2.0f + 18.0f * res_);  // damping: Q from 2 to 20
        }
        if (model == Model::Comb) combDelay_ = std::clamp(sampleRate_ / fc, 2.0f, float(combSize_ - 3));
        if (model == Model::Phaser)
            for (int i = 0; i < 4; ++i) {
                static const float spread[4] = {0.5f, 0.8f, 1.25f, 2.0f};
                const float t = std::tan(kPi * std::min(fc * spread[i], 0.45f * sampleRate_) / sampleRate_);
                apA_[size_t(i)] = (t - 1.0f) / (t + 1.0f);
            }
    }

    // The output through a soft limit: transparent below 1, never above 2 (a ladder's high-pass
    // or an MS-20 screaming at full resonance reached +23 dB on noise).
    float process(int c, float x) {
        if (model_ == Model::Off) return x;
        const float y = run(c, x);
        const float a = std::abs(y);
        return a <= 1.0f ? y : std::copysign(1.0f + std::tanh(a - 1.0f), y);
    }

private:
    float run(int c, float x) {
        Channel& ch = ch_[size_t(c)];
        switch (model_) {
            case Model::Off: return x;
            case Model::Ladder: return ladder(ch, x, false);
            case Model::Diode: return ladder(ch, x, true);
            case Model::Ota: return ota(ch, x);
            case Model::Ms20: return svf(ch, x, 2);
            case Model::Sem: return svf(ch, x, 1);
            case Model::Digital: return svf(ch, x, 0);
            case Model::Formant: return formant(ch, x);
            case Model::Comb: return comb(ch, x);
            case Model::Phaser: return phaser(ch, x);
        }
        return x;
    }

    struct Channel {
        std::array<float, 4> s{};   // ladder / OTA stages, SVF states (s[0], s[1])
        std::array<float, 6> f{};   // formant band-passes (two states each)
        std::array<float, 4> ap{};  // phaser all-passes
        std::vector<float> comb;
        int combPos = 0;
        float combLp = 0.0f, fb = 0.0f, hpState = 0.0f;
    };

    // One-pole TPT low-pass stage: returns y, updates s.
    static float stage(float x, float& s, float G) {
        const float v = (x - s) * G, y = v + s;
        s = y + v;
        return y;
    }

    // LADDER (and DIODE): the feedback loop solved without delay, then the stages run.
    float ladder(Channel& ch, float x, bool diode) {
        const float G = G_, k = (diode ? 3.4f : 3.98f) * res_;
        // y4 = G^4 u + G^3 b1 + G^2 b2 + G b3 + b4, with b_i = (1 - G) s_i and u = x - k y4.
        const float b1 = (1.0f - G) * ch.s[0], b2 = (1.0f - G) * ch.s[1], b3 = (1.0f - G) * ch.s[2], b4 = (1.0f - G) * ch.s[3];
        const float G2 = G * G, G4 = G2 * G2;
        const float in = std::tanh(x * driveIn_);
        float fb = (G4 * in + G2 * G * b1 + G2 * b2 + G * b3 + b4) / (1.0f + k * G4);
        if (diode) {  // the 303's feedback loses its lows (a one-pole high-pass at ~120 Hz), more so as RES rises
            ch.hpState = fb + hpA_ * (ch.hpState - fb);
            fb = (1.0f - 0.5f * res_) * fb + 0.5f * res_ * (fb - ch.hpState);
        }
        const float u = in - k * fb;
        float y1, y2, y3, y4;
        if (diode) {  // each stage saturates a little
            y1 = stage(std::tanh(u), ch.s[0], G), y2 = stage(std::tanh(y1), ch.s[1], G), y3 = stage(std::tanh(y2), ch.s[2], G), y4 = stage(y3, ch.s[3], G);
        } else {
            y1 = stage(u, ch.s[0], G), y2 = stage(y1, ch.s[1], G), y3 = stage(y2, ch.s[2], G), y4 = stage(y3, ch.s[3], G);
        }
        return mixLadder(u, y1, y2, y3, y4) * (1.0f + 0.6f * k / 4.0f) * driveOut_ * (diode ? 1.3f : 1.0f);
    }
    // OTA: four stages, each through an OTA's tanh (the feedback still solved linearly).
    float ota(Channel& ch, float x) {
        const float G = G_, k = 3.9f * res_;
        const float b1 = (1.0f - G) * ch.s[0], b2 = (1.0f - G) * ch.s[1], b3 = (1.0f - G) * ch.s[2], b4 = (1.0f - G) * ch.s[3];
        const float G2 = G * G, G4 = G2 * G2;
        const float in = x * driveIn_;
        const float fb = (G4 * in + G2 * G * b1 + G2 * b2 + G * b3 + b4) / (1.0f + k * G4);
        const float u = std::tanh(in - k * fb);
        const float y1 = stage(u, ch.s[0], G), y2 = stage(std::tanh(y1), ch.s[1], G), y3 = stage(std::tanh(y2), ch.s[2], G), y4 = stage(std::tanh(y3), ch.s[3], G);
        return mixLadder(u, y1, y2, y3, y4) * (1.0f + 0.5f * k / 4.0f) * driveOut_;
    }
    // Responses from a ladder's stages: (1 - L)^n and L^n mixes, BP and NOTCH at the cutoff.
    float mixLadder(float u, float y1, float y2, float y3, float y4) const {
        switch (mode_) {
            case Mode::Lp: return y4;
            case Mode::Bp: return 4.0f * (y2 - 2.0f * y3 + y4);
            case Mode::Hp: return u - 4.0f * y1 + 6.0f * y2 - 4.0f * y3 + y4;
            case Mode::Notch: return u - 4.0f * (y2 - 2.0f * y3 + y4);
        }
        return y4;
    }

    // State-variable (TPT). flavour 0: clean (DIGITAL), 1: soft (SEM), 2: the MS-20's
    // screaming resonance (its band-pass feedback through a hard saturation).
    float svf(Channel& ch, float x, int flavour) {
        const float g = g_;
        float R = flavour == 2 ? 1.0f - 0.995f * res_ : 1.0f - 0.98f * res_;  // damping: Q 0.5 .. ~50
        const float in = flavour == 0 ? x : std::tanh(x * driveIn_) * (flavour == 2 ? 1.2f : 1.0f);
        const float h = 1.0f / (1.0f + 2.0f * R * g + g * g);
        const float hp = (in - (2.0f * R + g) * ch.s[0] - ch.s[1]) * h;
        float bp = g * hp + ch.s[0];
        if (flavour == 2) bp = std::tanh(bp * (1.0f + 2.0f * res_)) / (1.0f + 2.0f * res_) * (1.0f + res_);
        else if (flavour == 1) bp = std::tanh(bp);
        ch.s[0] = g * hp + bp;
        const float lp = g * bp + ch.s[1];
        ch.s[1] = g * bp + lp;
        float y = lp;
        switch (mode_) {
            case Mode::Lp: y = lp; break;
            case Mode::Bp: y = 2.0f * R * bp; break;
            case Mode::Hp: y = hp; break;
            case Mode::Notch: y = in - 2.0f * R * bp; break;
        }
        return y * (flavour == 0 ? 1.0f : driveOut_);
    }

    // FORMANT: three band-passes in parallel (TPT SVF band-pass outputs).
    float formant(Channel& ch, float x) {
        const float in = std::tanh(x * driveIn_);
        float y = 0.0f;
        for (int j = 0; j < 3; ++j) {
            const float g = formantG_[size_t(j)], R = formantR_;
            float& s1 = ch.f[size_t(2 * j)];
            float& s2 = ch.f[size_t(2 * j + 1)];
            const float hp = (in - (2.0f * R + g) * s1 - s2) / (1.0f + 2.0f * R * g + g * g);
            const float bp = g * hp + s1;
            s1 = g * hp + bp;
            const float lp = g * bp + s2;
            s2 = g * bp + lp;
            y += 2.0f * R * bp * formantGain_[size_t(j)];
        }
        return y * 3.0f * driveOut_;
    }

    // COMB: y = x + fb · y(t - one period), the loop softly saturated and damped.
    float comb(Channel& ch, float x) {
        const float sign = (mode_ == Mode::Hp || mode_ == Mode::Notch) ? -1.0f : 1.0f;
        const float fbGain = sign * 0.97f * res_;
        float readAt = float(ch.combPos) - combDelay_;
        if (readAt < 0.0f) readAt += float(combSize_);
        const int i = int(readAt);
        const float frac = readAt - float(i);
        const float d = ch.comb[size_t(i)] + frac * (ch.comb[size_t((i + 1) % combSize_)] - ch.comb[size_t(i)]);
        ch.combLp += 0.6f * (d - ch.combLp);  // a little damping in the loop
        const float y = std::tanh(x * driveIn_ + fbGain * ch.combLp);
        ch.comb[size_t(ch.combPos)] = y;
        if (++ch.combPos == combSize_) ch.combPos = 0;
        return y * driveOut_ * (1.0f - 0.5f * res_);
    }

    // PHASER: four first-order all-passes, the last fed back, half mixed with the input.
    float phaser(Channel& ch, float x) {
        float v = std::tanh(x * driveIn_) + 0.85f * res_ * ch.fb;
        for (int i = 0; i < 4; ++i) {
            const float a = apA_[size_t(i)];
            const float y = a * v + ch.ap[size_t(i)];
            ch.ap[size_t(i)] = v - a * y;
            v = y;
        }
        ch.fb = v;
        const float y = 0.5f * (x * driveIn_ + (mode_ == Mode::Hp || mode_ == Mode::Notch ? -v : v));
        return y * driveOut_;
    }

    float sampleRate_ = 48000.0f, g_ = 0.1f, G_ = 0.1f, res_ = 0.0f, fc_ = 1000.0f, driveIn_ = 1.0f, driveOut_ = 1.0f;
    Model model_ = Model::Off;
    Mode mode_ = Mode::Lp;
    std::array<Channel, 2> ch_;
    std::array<float, 3> formantG_{}, formantGain_{};
    float formantR_ = 0.1f, combDelay_ = 100.0f, hpA_ = 0.98f;
    std::array<float, 4> apA_{};
    int combSize_ = 4096;
};

}  // namespace nodal
