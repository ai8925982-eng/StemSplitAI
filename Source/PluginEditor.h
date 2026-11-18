#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class StemStrip;

class StemSplitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StemSplitEditor (StemSplitProcessor&);
    ~StemSplitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseAudioFile();
    void chooseModel();
    void startExport();

    StemSplitProcessor& proc;

    juce::TextButton openBtn   { "Open audio..." },
                     playBtn   { "Play / Stop" },
                     modelBtn  { "Load AI model (.onnx)..." },
                     dspBtn    { "Use built-in DSP" },
                     exportBtn { "Export all stems (WAV)..." };
    juce::Label status, isoLabel;
    juce::Slider isoSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> isoAtt;

    std::vector<std::unique_ptr<StemStrip>> strips;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::ThreadWithProgressWindow> exportTask;
    juce::File exportSource;
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSplitEditor)
};
