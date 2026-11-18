#pragma once
#include <array>
#include <memory>
#include <juce_core/juce_core.h>
#include "StemDefs.h"

// A separator turns a stereo stream into kNumStems stereo streams.
// out[] holds 2 * kNumStems pointers: out[2*s] = left of stem s, out[2*s+1] = right.
class StemSeparator
{
public:
    virtual ~StemSeparator() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual int  getLatencySamples() const = 0;
    virtual void process (const float* const* in, float* const* out, int numSamples) = 0;

    // Tell the engine which stems are actually consumed (saves CPU).
    virtual void setWanted (const std::array<bool, stemsplit::kNumStems>&) {}
    // Offline = faster-than-real-time rendering: the engine may block and wait.
    virtual void setOffline (bool) {}
    // 1 = soft masks, higher = harder masks (less bleed, more artefacts). DSP engine only.
    virtual void setIsolation (float /*power*/) {}

    virtual juce::String getName() const = 0;
    virtual bool isAI() const { return false; }
};

// Creates the AI backend if a model is given and ONNX is compiled in,
// otherwise the built-in DSP engine. `status` explains what happened.
std::unique_ptr<StemSeparator> createSeparator (const juce::File& model, double sampleRate,
                                                int maxBlock, bool offline, juce::String& status);
