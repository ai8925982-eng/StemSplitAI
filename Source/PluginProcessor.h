#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <functional>
#include "StemSeparator.h"

class StemSplitProcessor : public juce::AudioProcessor
{
public:
    StemSplitProcessor();
    ~StemSplitProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "StemSplit AI"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor ----
    bool  loadFile (const juce::File&);
    void  setPlaying (bool);
    bool  isPlaying() const { return transport.isPlaying(); }
    juce::String getLoadedFileName() const;

    void  loadModel (const juce::File& onnx);          // empty File = built-in DSP
    juce::File getModelFile() const;
    juce::String getStatusText() const;
    int   getReportedLatency() const { return currentLatency.load(); }

    bool  exportStems (const juce::File& audioIn, const juce::File& outDir,
                       const std::function<bool (double)>& progressAndCancel, juce::String& error);

    float getMeter (int stem) const { return meterPeak[(size_t) stem].load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void rebuildEngine();

    std::array<std::atomic<float>*, stemsplit::kNumStems> gainP {}, muteP {}, soloP {};
    std::atomic<float>* isolationP = nullptr;
    std::array<std::atomic<float>, stemsplit::kNumStems> meterPeak;

    juce::SpinLock sepLock;
    std::unique_ptr<StemSeparator> sep;

    mutable juce::CriticalSection infoLock;
    juce::File modelFile;
    juce::String statusText { "Not started" };
    juce::String loadedName;

    double sampleRate = 0.0;
    int scratchBlock = 0;
    std::atomic<int> currentLatency { 0 };
    std::vector<float> inL, inR, stemScratch;

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> fileScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitProcessor)
};
