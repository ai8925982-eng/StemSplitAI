#include "StemSeparator.h"
#include "DspSeparator.h"
#include "OnnxSeparator.h"

std::unique_ptr<StemSeparator> createSeparator (const juce::File& model, double sampleRate,
                                                int maxBlock, bool offline, juce::String& status)
{
    std::unique_ptr<StemSeparator> s;
    status = {};

    if (model != juce::File())
    {
       #if STEMSPLIT_ONNX
        juce::String err;
        if (auto o = OnnxSeparator::create (model, err))
        {
            s = std::move (o);
            status = "AI model: " + model.getFileName();
        }
        else
            status = "Model failed (" + err + ") - using built-in DSP";
       #else
        status = "This build has no ONNX Runtime - using built-in DSP";
       #endif
    }

    if (s == nullptr)
    {
        s = std::make_unique<DspSeparator>();
        if (status.isEmpty())
            status = "Built-in DSP engine (heuristic, no AI model loaded)";
    }

    s->prepare (sampleRate, maxBlock);
    s->setOffline (offline);
    return s;
}
