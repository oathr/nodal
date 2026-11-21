#pragma once

#include <complex>
#include <vector>

#include "common.h"

namespace nodal {

using Complex = std::complex<float>;

// Real FFT of size N (power of two) through one complex FFT of size N/2.
// forward: N real samples -> N/2 + 1 bins, unnormalised.
// inverse: N/2 + 1 bins -> N real samples, scaled so inverse(forward(x)) == x.
// The complex FFT works on split real / imaginary arrays with one contiguous twiddle table
// per stage, so the butterfly loops vectorise. Nothing allocates after prepare().
class RealFft {
public:
    void prepare(int size) {
        n_ = size;
        m_ = size / 2;
        bits_ = 0;
        while ((1 << bits_) < m_) ++bits_;
        // Stage twiddles: for each butterfly length L >= 8, L/2 values e^{-2 pi i j / L}.
        twRe_.clear();
        twIm_.clear();
        stageOffset_.clear();
        for (int len = 8; len <= m_; len <<= 1) {
            stageOffset_.push_back(int(twRe_.size()));
            for (int j = 0; j < len / 2; ++j) {
                twRe_.push_back(std::cos(kTwoPi * float(j) / float(len)));
                twIm_.push_back(-std::sin(kTwoPi * float(j) / float(len)));
            }
        }
        split_.resize(size_t(m_ + 1));
        for (int k = 0; k <= m_; ++k) split_[size_t(k)] = std::polar(1.0f, -kTwoPi * float(k) / float(n_));
        reverse_.resize(size_t(m_));
        for (int i = 0; i < m_; ++i) {
            int r = 0;
            for (int b = 0; b < bits_; ++b)
                if (i & (1 << b)) r |= 1 << (bits_ - 1 - b);
            reverse_[size_t(i)] = r;
        }
        re_.assign(size_t(m_), 0.0f);
        im_.assign(size_t(m_), 0.0f);
    }
    int size() const { return n_; }
    int bins() const { return m_ + 1; }

    void forward(const float* in, Complex* out) {
        for (int i = 0; i < m_; ++i) {
            const int r = reverse_[size_t(i)];
            re_[size_t(r)] = in[2 * i];
            im_[size_t(r)] = in[2 * i + 1];
        }
        butterflies(false);
        out[0] = {re_[0] + im_[0], 0.0f};
        out[m_] = {re_[0] - im_[0], 0.0f};
        for (int k = 1; k < m_; ++k) {
            const float ar = re_[size_t(k)], ai = im_[size_t(k)], br = re_[size_t(m_ - k)], bi = -im_[size_t(m_ - k)];
            const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);   // even
            const float or_ = 0.5f * (ai - bi), oi = -0.5f * (ar - br);  // odd = (a - b) / 2i
            const float wr = split_[size_t(k)].real(), wi = split_[size_t(k)].imag();
            out[k] = {er + wr * or_ - wi * oi, ei + wr * oi + wi * or_};
        }
    }

    void inverse(const Complex* in, float* out) {
        for (int k = 0; k < m_; ++k) {
            const float ar = in[k].real(), ai = in[k].imag(), br = in[m_ - k].real(), bi = -in[m_ - k].imag();
            const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
            const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
            const float wr = split_[size_t(k)].real(), wi = -split_[size_t(k)].imag();  // divide by W^k
            const float or_ = dr * wr - di * wi, oi = dr * wi + di * wr;
            const int r = reverse_[size_t(k)];
            re_[size_t(r)] = er - oi;  // even + i * odd
            im_[size_t(r)] = ei + or_;
        }
        butterflies(true);
        const float scale = 1.0f / float(m_);
        for (int i = 0; i < m_; ++i) {
            out[2 * i] = re_[size_t(i)] * scale;
            out[2 * i + 1] = im_[size_t(i)] * scale;
        }
    }

private:
    // In-place radix-2 on bit-reversed input. Lengths 2 and 4 have trivial twiddles.
    void butterflies(bool inverse) {
        float* __restrict re = re_.data();
        float* __restrict im = im_.data();
        const int n = m_;
        if (n >= 2)
            for (int i = 0; i < n; i += 2) {
                const float ar = re[i], ai = im[i], br = re[i + 1], bi = im[i + 1];
                re[i] = ar + br, im[i] = ai + bi;
                re[i + 1] = ar - br, im[i + 1] = ai - bi;
            }
        if (n >= 4) {
            const float s = inverse ? 1.0f : -1.0f;  // twiddle of j = 1: -i forward, +i inverse
            for (int i = 0; i < n; i += 4) {
                float ar = re[i], ai = im[i], br = re[i + 2], bi = im[i + 2];
                re[i] = ar + br, im[i] = ai + bi;
                re[i + 2] = ar - br, im[i + 2] = ai - bi;
                ar = re[i + 1], ai = im[i + 1];
                const float cr = -s * im[i + 3], ci = s * re[i + 3];
                re[i + 1] = ar + cr, im[i + 1] = ai + ci;
                re[i + 3] = ar - cr, im[i + 3] = ai - ci;
            }
        }
        int stage = 0;
        const float sign = inverse ? -1.0f : 1.0f;
        for (int len = 8; len <= n; len <<= 1, ++stage) {
            const int half = len / 2;
            const float* __restrict wr = twRe_.data() + stageOffset_[size_t(stage)];
            const float* __restrict wi0 = twIm_.data() + stageOffset_[size_t(stage)];
            for (int i = 0; i < n; i += len) {
                float* __restrict ar = re + i;
                float* __restrict ai = im + i;
                float* __restrict br = re + i + half;
                float* __restrict bi = im + i + half;
                for (int j = 0; j < half; ++j) {
                    const float wi = sign * wi0[j];
                    const float vr = br[j] * wr[j] - bi[j] * wi, vi = br[j] * wi + bi[j] * wr[j];
                    const float ur = ar[j], ui = ai[j];
                    ar[j] = ur + vr, ai[j] = ui + vi;
                    br[j] = ur - vr, bi[j] = ui - vi;
                }
            }
        }
    }

    int n_ = 0, m_ = 0, bits_ = 0;
    std::vector<float> twRe_, twIm_, re_, im_;
    std::vector<int> stageOffset_, reverse_;
    std::vector<Complex> split_;
};

}  // namespace nodal
