#pragma once
#include <atomic>
#include <complex>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "StemSeparator.h"

// Built-in engine: STFT + harmonic/percussive median filtering + stereo
// centre/side analysis + per-stem spectral priors (see StemDefs.h).
// All stem masks sum to 1 per bin, so the stems always add back to the input.
// This is a HEURISTIC splitter: it cannot truly tell an electric guitar from a
// synth or a phin from a khaen. Use an ONNX model for real separation.
class DspSeparator : public StemSeparator
{
public:
    static constexpr int kOrder  = 11;
    static constexpr int kN      = 1 << kOrder;      // FFT size
    static constexpr int kHop    = kN / 4;
    static constexpr int kBins   = kN / 2 + 1;
    static constexpr int kHist   = 17;               // frames kept (time median)
    static constexpr int kCenter = kHist / 2;        // 8
    static constexpr int kFifo   = 8192;             // power of two

    DspSeparator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    int  getLatencySamples() const override { return kN + kCenter * kHop; }
    void setWanted (const std::array<bool, stemsplit::kNumStems>& w) override { wanted = w; }
    void setIsolation (float p) override { power.store (juce::jlimit (1.0f, 8.0f, p)); }
    void process (const float* const* in, float* const* out, int numSamples) override;
    juce::String getName() const override { return "Built-in DSP (heuristic)"; }

private:
    using cf = std::complex<float>;
    static constexpr int S = stemsplit::kNumStems;

    void tick();
    void computeMasks (int c);
    void mirrorAndInverse (float* buf);

    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, fftBuf;
    float ifftScale = 1.0f;
    double sr = 44100.0;

    std::array<std::vector<float>, 2> inRing;
    int inWrite = 0, hopCount = 0;

    std::vector<cf>    histSpec[2];   // [kHist * kBins]
    std::vector<float> histMag;       // [kHist * kBins]
    int head = 0;

    std::vector<float> bandW;         // [S * kBins]
    std::vector<float> masks;         // [S * kBins]

    std::vector<float> ola[S][2];     // [kN]
    int olaPos = 0;
    std::vector<float> fifo[S][2];    // [kFifo]
    unsigned fifoW = 0, fifoR = 0;

    std::array<bool, S> wanted;
    std::atomic<float> power { 5.0f };
};
