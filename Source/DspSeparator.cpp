#include "DspSeparator.h"
#include <algorithm>
#include <cmath>

using namespace stemsplit;

DspSeparator::DspSeparator()
{
    wanted.fill (true);
    window.resize (kN);
    for (int i = 0; i < kN; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) kN);
    fftBuf.assign (2 * kN, 0.0f);
}

void DspSeparator::mirrorAndInverse (float* buf)
{
    // buf holds bins 0..N/2 interleaved (re,im). Build the conjugate-symmetric half.
    buf[1] = 0.0f;
    buf[2 * (kN / 2) + 1] = 0.0f;
    for (int k = 1; k < kN / 2; ++k)
    {
        buf[2 * (kN - k)]     =  buf[2 * k];
        buf[2 * (kN - k) + 1] = -buf[2 * k + 1];
    }
    fft.performRealOnlyInverseTransform (buf);
}

void DspSeparator::prepare (double sampleRate, int)
{
    sr = sampleRate;

    // Self-calibrate the FFT round-trip gain (differs between FFT back-ends).
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    fftBuf[5] = 1.0f;
    fft.performRealOnlyForwardTransform (fftBuf.data(), true);
    mirrorAndInverse (fftBuf.data());
    ifftScale = std::abs (fftBuf[5]) > 1.0e-6f ? 1.0f / fftBuf[5] : 1.0f;

    // Spectral priors: gaussian in log-frequency per stem.
    bandW.assign ((size_t) S * kBins, 0.0f);
    const double binHz = sr / (double) kN;
    for (int s = 0; s < S; ++s)
    {
        const auto& si = stemInfo (s);
        for (int k = 0; k < kBins; ++k)
        {
            const double f = std::max (0.5, (double) k) * binHz;
            const double x = std::log2 (f / (double) si.fc) / (double) si.sigmaOct;
            bandW[(size_t) s * kBins + (size_t) k] = (float) std::exp (-0.5 * x * x);
        }
    }

    for (auto& r : inRing) r.assign (kN, 0.0f);
    for (int c = 0; c < 2; ++c) histSpec[c].assign ((size_t) kHist * kBins, cf {});
    histMag.assign ((size_t) kHist * kBins, 0.0f);
    masks.assign ((size_t) S * kBins, 0.0f);
    for (int s = 0; s < S; ++s)
        for (int c = 0; c < 2; ++c)
        {
            ola[s][c].assign (kN, 0.0f);
            fifo[s][c].assign (kFifo, 0.0f);
        }
    reset();
}

void DspSeparator::reset()
{
    for (auto& r : inRing) std::fill (r.begin(), r.end(), 0.0f);
    for (int c = 0; c < 2; ++c) std::fill (histSpec[c].begin(), histSpec[c].end(), cf {});
    std::fill (histMag.begin(), histMag.end(), 0.0f);
    for (int s = 0; s < S; ++s)
        for (int c = 0; c < 2; ++c)
        {
            std::fill (ola[s][c].begin(), ola[s][c].end(), 0.0f);
            std::fill (fifo[s][c].begin(), fifo[s][c].end(), 0.0f);
        }
    inWrite = hopCount = head = olaPos = 0;
    fifoR = 0;
    fifoW = (unsigned) kHop;   // prefill: guarantees the FIFO never runs dry
}

void DspSeparator::computeMasks (int c)
{
    float tmp[kHist];
    float tmp2[17];
    const float p = power.load();
    const float* magC = histMag.data() + (size_t) c * kBins;
    const cf* L = histSpec[0].data() + (size_t) c * kBins;
    const cf* R = histSpec[1].data() + (size_t) c * kBins;

    for (int k = 0; k < kBins; ++k)
    {
        // harmonic estimate: median over time; percussive estimate: median over frequency
        for (int t = 0; t < kHist; ++t) tmp[t] = histMag[(size_t) t * kBins + (size_t) k];
        std::nth_element (tmp, tmp + kCenter, tmp + kHist);
        const float hMed = tmp[kCenter];

        int cnt = 0;
        for (int j = -8; j <= 8; ++j)
            tmp2[cnt++] = magC[juce::jlimit (0, kBins - 1, k + j)];
        std::nth_element (tmp2, tmp2 + 8, tmp2 + 17);
        const float pMed = tmp2[8];

        const float h2 = hMed * hMed, p2 = pMed * pMed;
        const float hm = h2 / (h2 + p2 + 1.0e-12f);
        const float pm = 1.0f - hm;

        const cf mid  = 0.5f * (L[k] + R[k]);
        const cf side = 0.5f * (L[k] - R[k]);
        const float m2 = std::norm (mid), s2 = std::norm (side);
        const float ctr = m2 / (m2 + s2 + 1.0e-12f);

        float sum = 0.0f;
        for (int s = 0; s < S; ++s)
        {
            const auto& si = stemInfo (s);
            const float w = bandW[(size_t) s * kBins + (size_t) k]
                          * (si.perc * pm + si.harm * hm + 0.02f)
                          * (si.centre * ctr + si.side * (1.0f - ctr) + 0.02f)
                          + 1.0e-9f;
            // sharpen: w^p then renormalise -> winner-take-more, far less bleed between stems
            const float wp = std::pow (std::max (w, 1.0e-12f), p);
            masks[(size_t) s * kBins + (size_t) k] = wp;
            sum += wp;
        }
        const float inv = 1.0f / (sum + 1.0e-30f);
        for (int s = 0; s < S; ++s)
            masks[(size_t) s * kBins + (size_t) k] *= inv;
    }
}

void DspSeparator::tick()
{
    // 1) analyse newest frame
    head = (head + 1) % kHist;
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < kN; ++i)
            fftBuf[(size_t) i] = inRing[(size_t) ch][(size_t) ((inWrite + i) & (kN - 1))] * window[(size_t) i];
        std::fill (fftBuf.begin() + kN, fftBuf.end(), 0.0f);
        fft.performRealOnlyForwardTransform (fftBuf.data(), true);
        cf* dst = histSpec[ch].data() + (size_t) head * kBins;
        for (int k = 0; k < kBins; ++k)
            dst[k] = cf { fftBuf[(size_t) (2 * k)], fftBuf[(size_t) (2 * k + 1)] };
    }
    {
        const cf* L = histSpec[0].data() + (size_t) head * kBins;
        const cf* R = histSpec[1].data() + (size_t) head * kBins;
        float* m = histMag.data() + (size_t) head * kBins;
        for (int k = 0; k < kBins; ++k) m[k] = std::abs (0.5f * (L[k] + R[k]));
    }

    // 2) process the frame that is kCenter hops old (needs future context)
    const int c = (head - kCenter + kHist) % kHist;
    computeMasks (c);

    const float scale = ifftScale / 1.5f;   // sum of hann^2 at 75% overlap = 1.5
    for (int s = 0; s < S; ++s)
    {
        if (! wanted[(size_t) s]) continue;
        const float* mk = masks.data() + (size_t) s * kBins;
        for (int ch = 0; ch < 2; ++ch)
        {
            const cf* X = histSpec[ch].data() + (size_t) c * kBins;
            for (int k = 0; k < kBins; ++k)
            {
                fftBuf[(size_t) (2 * k)]     = X[k].real() * mk[k];
                fftBuf[(size_t) (2 * k + 1)] = X[k].imag() * mk[k];
            }
            mirrorAndInverse (fftBuf.data());
            auto& o = ola[s][ch];
            for (int i = 0; i < kN; ++i)
                o[(size_t) ((olaPos + i) & (kN - 1))] += fftBuf[(size_t) i] * window[(size_t) i] * scale;
        }
    }

    // 3) emit the now-complete first hop of the OLA ring, then clear it
    for (int s = 0; s < S; ++s)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& o = ola[s][ch];
            auto& f = fifo[s][ch];
            for (int i = 0; i < kHop; ++i)
            {
                const size_t idx = (size_t) ((olaPos + i) & (kN - 1));
                f[(size_t) ((fifoW + (unsigned) i) & (kFifo - 1))] = wanted[(size_t) s] ? o[idx] : 0.0f;
                o[idx] = 0.0f;
            }
        }
    fifoW += (unsigned) kHop;
    olaPos = (olaPos + kHop) & (kN - 1);
}

void DspSeparator::process (const float* const* in, float* const* out, int n)
{
    for (int i = 0; i < n; ++i)
    {
        inRing[0][(size_t) inWrite] = in[0][i];
        inRing[1][(size_t) inWrite] = in[1][i];
        inWrite = (inWrite + 1) & (kN - 1);
        if (++hopCount == kHop) { hopCount = 0; tick(); }

        const size_t r = (size_t) (fifoR & (kFifo - 1));
        for (int s = 0; s < S; ++s)
        {
            if (wanted[(size_t) s])
            {
                out[2 * s][i]     = fifo[s][0][r];
                out[2 * s + 1][i] = fifo[s][1][r];
            }
            else
            {
                out[2 * s][i] = 0.0f;
                out[2 * s + 1][i] = 0.0f;
            }
        }
        ++fifoR;
    }
}
