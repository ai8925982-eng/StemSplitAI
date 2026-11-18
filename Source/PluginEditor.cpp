#include "PluginEditor.h"

using namespace stemsplit;

namespace
{
    juce::Colour groupColour (const juce::String& g)
    {
        if (g == "Drums")      return juce::Colour (0xffff7a59);
        if (g == "Bass")       return juce::Colour (0xffb06cff);
        if (g == "Guitar")     return juce::Colour (0xffffc23d);
        if (g == "Keys")       return juce::Colour (0xff4cc9f0);
        if (g == "Isan")       return juce::Colour (0xff3ddc97);
        if (g == "Orchestral") return juce::Colour (0xfff56ba8);
        if (g == "Vocal")      return juce::Colour (0xffffffff);
        if (g == "Chorus")     return juce::Colour (0xffa8dadc);
        return juce::Colour (0xff8d99ae);
    }

    juce::Font uiFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }
}

// One mixer strip: name, fader, mute, solo, level meter.
class StemStrip : public juce::Component
{
public:
    StemStrip (StemSplitProcessor& p, int idx) : index (idx)
    {
        const auto& si = stemInfo (idx);
        const juce::String id (si.id);
        accent = groupColour (si.group);

        name.setText (juce::String::fromUTF8 (si.name), juce::dontSendNotification);
        name.setJustificationType (juce::Justification::centred);
        name.setFont (uiFont (12.0f, true));
        name.setColour (juce::Label::textColourId, accent);
        addAndMakeVisible (name);

        gain.setSliderStyle (juce::Slider::LinearVertical);
        gain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        gain.setPopupDisplayEnabled (true, true, nullptr);
        gain.setTextValueSuffix (" dB");
        gain.setDoubleClickReturnValue (true, 0.0);
        gain.setColour (juce::Slider::thumbColourId, accent);
        gain.setColour (juce::Slider::trackColourId, accent.withAlpha (0.55f));
        addAndMakeVisible (gain);

        mute.setButtonText ("M");
        solo.setButtonText ("S");
        mute.setClickingTogglesState (true);
        solo.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe5383b));
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff4d35e));
        solo.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);

        gainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id + "_gain", gain);
        muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, id + "_mute", mute);
        soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, id + "_solo", solo);
    }

    void setLevel (float peak)
    {
        const float v = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (peak, -60.0f) / 60.0f + 1.0f);
        level = std::max (v, level * 0.88f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (juce::Colour (0xff1d2230));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (accent.withAlpha (0.25f));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);

        g.setColour (juce::Colour (0xff0e1118));
        g.fillRect (meterArea);
        auto lit = meterArea;
        lit.removeFromTop ((int) ((1.0f - level) * (float) meterArea.getHeight()));
        g.setColour (level > 0.92f ? juce::Colour (0xffe5383b) : accent);
        g.fillRect (lit);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        name.setBounds (r.removeFromTop (34));
        auto btns = r.removeFromBottom (26);
        mute.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (2, 0));
        solo.setBounds (btns.reduced (2, 0));
        r.removeFromBottom (4);
        meterArea = r.removeFromRight (7).reduced (0, 2);
        r.removeFromRight (3);
        gain.setBounds (r);
    }

private:
    int index;
    juce::Colour accent;
    juce::Label name;
    juce::Slider gain;
    juce::TextButton mute, solo;
    juce::Rectangle<int> meterArea;
    float level = 0.0f;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt;
};

// Background export with a progress window.
class ExportTask : public juce::ThreadWithProgressWindow
{
public:
    ExportTask (StemSplitProcessor& p, juce::File in, juce::File out)
        : ThreadWithProgressWindow ("Separating stems...", true, true),
          proc (p), src (std::move (in)), dst (std::move (out)) {}

    void run() override
    {
        ok = proc.exportStems (src, dst, [this] (double prog)
        {
            setProgress (prog);
            return threadShouldExit();
        }, error);
    }

    void threadComplete (bool userPressedCancel) override
    {
        if (userPressedCancel) return;
        juce::AlertWindow::showMessageBoxAsync (
            ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
            "StemSplit AI",
            ok ? "Done. 30 stems saved to:\n" + dst.getFullPathName() : "Export failed: " + error);
    }

private:
    StemSplitProcessor& proc;
    juce::File src, dst;
    bool ok = false;
    juce::String error;
};

// ---------------------------------------------------------------- editor
StemSplitEditor::StemSplitEditor (StemSplitProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    for (int i = 0; i < kNumStems; ++i)
    {
        strips.push_back (std::make_unique<StemStrip> (p, i));
        addAndMakeVisible (*strips.back());
    }

    for (auto* b : { &openBtn, &playBtn, &modelBtn, &dspBtn, &exportBtn })
        addAndMakeVisible (*b);
    playBtn.setClickingTogglesState (true);
    playBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3ddc97));
    playBtn.setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    openBtn.onClick   = [this] { chooseAudioFile(); };
    playBtn.onClick   = [this] { proc.setPlaying (playBtn.getToggleState()); };
    modelBtn.onClick  = [this] { chooseModel(); };
    dspBtn.onClick    = [this] { proc.loadModel (juce::File()); };
    exportBtn.onClick = [this] { startExport(); };

    isoLabel.setText ("Isolation (less bleed)", juce::dontSendNotification);
    isoLabel.setFont (uiFont (12.0f));
    isoLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8c0cc));
    isoSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    isoSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    addAndMakeVisible (isoLabel);
    addAndMakeVisible (isoSlider);
    isoAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "isolation", isoSlider);

    status.setFont (uiFont (12.5f));
    status.setColour (juce::Label::textColourId, juce::Colour (0xffb8c0cc));
    addAndMakeVisible (status);

    setResizable (true, true);
    setResizeLimits (900, 560, 2000, 1300);
    setSize (1160, 740);
    startTimerHz (30);
}

StemSplitEditor::~StemSplitEditor()
{
    stopTimer();
    exportTask.reset();
}

void StemSplitEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff10131a), 0.0f, 0.0f,
                                             juce::Colour (0xff1a1f2e), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (juce::Colours::white);
    g.setFont (uiFont (22.0f, true));
    g.drawText ("StemSplit AI", 18, 10, 300, 28, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff8d99ae));
    g.setFont (uiFont (12.0f));
    g.drawText ("30-stem separator  |  VST3 + Standalone", 18, 36, 360, 18, juce::Justification::centredLeft);
}

void StemSplitEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (60);
    top.removeFromLeft (330);
    auto row = top.removeFromTop (28);
    const int w = row.getWidth() / 5;
    for (auto* b : { &openBtn, &playBtn, &modelBtn, &dspBtn, &exportBtn })
        b->setBounds (row.removeFromLeft (w).reduced (3, 0));
    auto isoArea = top.removeFromRight (380);
    isoLabel.setBounds (isoArea.removeFromLeft (140));
    isoSlider.setBounds (isoArea);
    status.setBounds (top.reduced (3, 2));

    r.removeFromTop (6);
    const int cols = 8, rows = (kNumStems + cols - 1) / cols;
    const int cw = r.getWidth() / cols, ch = r.getHeight() / rows;
    for (int i = 0; i < kNumStems; ++i)
        strips[(size_t) i]->setBounds (r.getX() + (i % cols) * cw, r.getY() + (i / cols) * ch, cw, ch);
}

void StemSplitEditor::timerCallback()
{
    for (int i = 0; i < kNumStems; ++i)
        strips[(size_t) i]->setLevel (proc.getMeter (i));

    const auto lat = proc.getReportedLatency();
    juce::String text = proc.getStatusText() + "   |   latency "
                      + juce::String (lat / std::max (1.0, proc.getSampleRate()), 2) + " s";
    const auto f = proc.getLoadedFileName();
    if (f.isNotEmpty()) text += "   |   file: " + f;
    if (text != lastStatus) { lastStatus = text; status.setText (text, juce::dontSendNotification); }
    if (playBtn.getToggleState() != proc.isPlaying())
        playBtn.setToggleState (proc.isPlaying(), juce::dontSendNotification);
}

void StemSplitEditor::chooseAudioFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile() && ! proc.loadFile (f))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "StemSplit AI", "Cannot open this file.");
        });
}

void StemSplitEditor::chooseModel()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an ONNX stem model", juce::File(), "*.onnx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) proc.loadModel (f);
        });
}

void StemSplitEditor::startExport()
{
    chooser = std::make_unique<juce::FileChooser> ("1/2  Choose the audio file to separate", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            exportSource = fc.getResult();
            if (! exportSource.existsAsFile()) return;

            chooser = std::make_unique<juce::FileChooser> ("2/2  Choose the output folder", exportSource.getParentDirectory());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this] (const juce::FileChooser& fc2)
                {
                    const auto dir = fc2.getResult();
                    if (dir == juce::File()) return;
                    exportTask = std::make_unique<ExportTask> (proc, exportSource, dir.getChildFile (exportSource.getFileNameWithoutExtension() + "_stems"));
                    exportTask->launchThread();
                });
        });
}
