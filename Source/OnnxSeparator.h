#pragma once
#include "StemSeparator.h"

#if STEMSPLIT_ONNX
// AI backend. Model contract (see training/README.md):
//   input  "mix"   float32 [1, 2, T]            stereo mixture
//   output "stems" float32 [1, 30, 2, T]        stems in the order of StemDefs.h
// Optional ONNX metadata key "sample_rate" (default 44100).
// Runs inference on a background thread in 50%-overlapped chunks, so the
// latency is ~1.5 x the model chunk length (seconds). Best for offline
// export / bounce; for live use prefer the built-in engine.
class OnnxSeparator : public StemSeparator
{
public:
    static std::unique_ptr<OnnxSeparator> create (const juce::File& model, juce::String& error);
    ~OnnxSeparator() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    int  getLatencySamples() const override;
    void setOffline (bool) override;
    void process (const float* const* in, float* const* out, int numSamples) override;
    juce::String getName() const override { return "ONNX AI model"; }
    bool isAI() const override { return true; }

private:
    OnnxSeparator();
    struct Impl;
    std::unique_ptr<Impl> impl;
};
#endif
