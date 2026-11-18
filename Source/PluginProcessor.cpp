#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace stemsplit;

// ------------------------------------------------------------------ set-up
juce::AudioProcessor::BusesProperties StemSplitProcessor::makeBuses()
{
    auto b = BusesProperties()
                 .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                 .withOutput ("Stem Mix", juce::AudioChannelSet::stereo(), true);
    for (int i = 0; i < kNumStems; ++i)   // 30 optional stereo outputs, one per stem
        b = b.withOutput (juce::String::fromUTF8 (stemInfo (i).name), juce::AudioChannelSet::stereo(), false);
    return b;
}

juce::AudioProcessorValueTreeState::ParameterLayout StemSplitProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    for (int i = 0; i < kNumStems; ++i)
    {
        const juce::String id (stemInfo (i).id);
        const auto nm = juce::String::fromUTF8 (stemInfo (i).name);
        p.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "_gain", 1 }, nm + " Gain",
            juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id + "_mute", 1 }, nm + " Mute", false));
        p.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id + "_solo", 1 }, nm + " Solo", false));
    }
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "isolation", 1 }, "Isolation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f));
    return { p.begin(), p.end() };
}

StemSplitProcessor::StemSplitProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int i = 0; i < kNumStems; ++i)
    {
        const juce::String id (stemInfo (i).id);
        gainP[(size_t) i] = apvts.getRawParameterValue (id + "_gain");
        muteP[(size_t) i] = apvts.getRawParameterValue (id + "_mute");
        soloP[(size_t) i] = apvts.getRawParameterValue (id + "_solo");
        meterPeak[(size_t) i].store (0.0f);
    }
    isolationP = apvts.getRawParameterValue ("isolation");
    formatManager.registerBasicFormats();
}

StemSplitProcessor::~StemSplitProcessor()
{
    transport.setSource (nullptr);
}

bool StemSplitProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
        return false;
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (const auto& set : l.outputBuses)
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    return true;
}

void StemSplitProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    scratchBlock = std::max (block, 512);
    inL.assign ((size_t) scratchBlock, 0.0f);
    inR.assign ((size_t) scratchBlock, 0.0f);
    stemScratch.assign ((size_t) kNumStems * 2 * (size_t) scratchBlock, 0.0f);
    fileScratch.setSize (2, scratchBlock);
    transport.prepareToPlay (scratchBlock, sr);
    rebuildEngine();
}

void StemSplitProcessor::releaseResources() { transport.releaseResources(); }

void StemSplitProcessor::rebuildEngine()
{
    if (sampleRate <= 0.0) return;
    juce::File mf;
    { juce::ScopedLock sl (infoLock); mf = modelFile; }

    juce::String status;
    auto fresh = createSeparator (mf, sampleRate, scratchBlock, false, status);   // may take a while (model load)
    const int lat = fresh->getLatencySamples();
    {
        juce::SpinLock::ScopedLockType lock (sepLock);
        sep = std::move (fresh);
    }
    currentLatency.store (lat);
    setLatencySamples (lat);
    juce::ScopedLock sl (infoLock);
    statusText = status;
}

void StemSplitProcessor::loadModel (const juce::File& f)
{
    { juce::ScopedLock sl (infoLock); modelFile = f; }
    rebuildEngine();
}

juce::File StemSplitProcessor::getModelFile() const { juce::ScopedLock sl (infoLock); return modelFile; }
juce::String StemSplitProcessor::getStatusText() const { juce::ScopedLock sl (infoLock); return statusText; }
juce::String StemSplitProcessor::getLoadedFileName() const { juce::ScopedLock sl (infoLock); return loadedName; }

// -------------------------------------------------------------- file preview
bool StemSplitProcessor::loadFile (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr) return false;

    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();

    const double fileSR = reader->sampleRate;
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    transport.setSource (readerSource.get(), 0, nullptr, fileSR, 2);
    juce::ScopedLock sl (infoLock);
    loadedName = f.getFileName();
    return true;
}

void StemSplitProcessor::setPlaying (bool shouldPlay)
{
    if (shouldPlay) { transport.setPosition (transport.hasStreamFinished() ? 0.0 : transport.getCurrentPosition()); transport.start(); }
    else transport.stop();
}

// ------------------------------------------------------------------ audio
void StemSplitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inCh = getMainBusNumInputChannels();

    juce::SpinLock::ScopedTryLockType lock (sepLock);
    if (! lock.isLocked() || sep == nullptr || scratchBlock == 0)
    {
        buffer.clear();
        return;
    }

    // --- fader / mute / solo -> gains, and which stems are needed at all
    std::array<float, kNumStems> gain {};
    std::array<bool, kNumStems> wanted {}, auxOn {};
    bool anySolo = false;
    for (int s = 0; s < kNumStems; ++s)
        if (soloP[(size_t) s]->load() > 0.5f) anySolo = true;
    for (int s = 0; s < kNumStems; ++s)
    {
        const bool m = muteP[(size_t) s]->load() > 0.5f;
        const bool so = soloP[(size_t) s]->load() > 0.5f;
        gain[(size_t) s] = (m || (anySolo && ! so)) ? 0.0f
                         : juce::Decibels::decibelsToGain (gainP[(size_t) s]->load(), -60.0f);
        auto* bus = getBus (false, 1 + s);
        auxOn[(size_t) s] = bus != nullptr && bus->isEnabled();
        wanted[(size_t) s] = gain[(size_t) s] > 0.0f || auxOn[(size_t) s];
    }
    sep->setWanted (wanted);
    sep->setIsolation (1.0f + 7.0f * isolationP->load());

    auto mainOut = getBusBuffer (buffer, false, 0);
    const bool useFile = transport.isPlaying();

    std::array<float*, kNumStems * 2> outPtrs {};
    for (int i = 0; i < kNumStems * 2; ++i)
        outPtrs[(size_t) i] = stemScratch.data() + (size_t) i * (size_t) scratchBlock;
    const float* inPtrs[2] = { inL.data(), inR.data() };

    int done = 0;
    while (done < total)
    {
        const int n = std::min (total - done, scratchBlock);

        // input: file preview or live input (read BEFORE writing outputs of this chunk)
        if (useFile)
        {
            juce::AudioSourceChannelInfo info (&fileScratch, 0, n);
            transport.getNextAudioBlock (info);
            std::copy (fileScratch.getReadPointer (0), fileScratch.getReadPointer (0) + n, inL.begin());
            std::copy (fileScratch.getReadPointer (1), fileScratch.getReadPointer (1) + n, inR.begin());
        }
        else if (inCh > 0)
        {
            const float* l = buffer.getReadPointer (0) + done;
            const float* r = inCh > 1 ? buffer.getReadPointer (1) + done : l;
            std::copy (l, l + n, inL.begin());
            std::copy (r, r + n, inR.begin());
        }
        else
        {
            std::fill (inL.begin(), inL.begin() + n, 0.0f);
            std::fill (inR.begin(), inR.begin() + n, 0.0f);
        }

        sep->process (inPtrs, outPtrs.data(), n);

        mainOut.clear (done, n);
        for (int s = 0; s < kNumStems; ++s)
        {
            if (! wanted[(size_t) s]) { meterPeak[(size_t) s].store (0.0f); continue; }
            const float* l = outPtrs[(size_t) (2 * s)];
            const float* r = outPtrs[(size_t) (2 * s + 1)];

            const float g = gain[(size_t) s];
            if (g > 0.0f)
            {
                mainOut.addFrom (0, done, l, n, g);
                mainOut.addFrom (1, done, r, n, g);
            }
            if (auxOn[(size_t) s])   // raw (pre-fader) stem to the host's aux output
            {
                auto bus = getBusBuffer (buffer, false, 1 + s);
                bus.copyFrom (0, done, l, n);
                bus.copyFrom (1, done, r, n);
            }
            const auto a = juce::FloatVectorOperations::findMinAndMax (l, n);
            const auto b = juce::FloatVectorOperations::findMinAndMax (r, n);
            meterPeak[(size_t) s].store (std::max ({ std::abs (a.getStart()), std::abs (a.getEnd()),
                                                     std::abs (b.getStart()), std::abs (b.getEnd()) }));
        }
        done += n;
    }
}

// -------------------------------------------------------------- export WAVs
bool StemSplitProcessor::exportStems (const juce::File& audioIn, const juce::File& outDir,
                                      const std::function<bool (double)>& progress, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (audioIn));
    if (reader == nullptr) { error = "Cannot read audio file"; return false; }

    const double sr = reader->sampleRate;
    const juce::int64 totalLen = reader->lengthInSamples;
    constexpr int B = 2048;

    juce::File mf;
    { juce::ScopedLock sl (infoLock); mf = modelFile; }
    juce::String status;
    auto engine = createSeparator (mf, sr, B, true, status);
    std::array<bool, kNumStems> all;
    all.fill (true);
    engine->setWanted (all);
    engine->setIsolation (1.0f + 7.0f * isolationP->load());

    if (! outDir.createDirectory()) { error = "Cannot create output folder"; return false; }

    juce::WavAudioFormat wav;
    std::array<std::unique_ptr<juce::AudioFormatWriter>, kNumStems> writers;
    for (int s = 0; s < kNumStems; ++s)
    {
        auto f = outDir.getChildFile (audioIn.getFileNameWithoutExtension() + "_" + stemInfo (s).id + ".wav");
        f.deleteFile();
        std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
        if (os == nullptr) { error = "Cannot write " + f.getFileName(); return false; }
        writers[(size_t) s].reset (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        if (writers[(size_t) s] == nullptr) { error = "WAV writer failed"; return false; }
        os.release();   // owned by the writer now
    }

    juce::AudioBuffer<float> in (2, B);
    std::vector<float> store ((size_t) kNumStems * 2 * B);
    std::array<float*, kNumStems * 2> outPtrs {};
    for (int i = 0; i < kNumStems * 2; ++i) outPtrs[(size_t) i] = store.data() + (size_t) i * B;

    juce::int64 pos = 0, written = 0, skip = engine->getLatencySamples();
    while (written < totalLen)
    {
        const int n = B;
        in.clear();
        if (pos < totalLen)
        {
            const int toRead = (int) std::min<juce::int64> (B, totalLen - pos);
            reader->read (&in, 0, toRead, pos, true, true);
            if (reader->numChannels < 2)
                in.copyFrom (1, 0, in, 0, 0, toRead);
            pos += toRead;
        }
        const float* inPtrs[2] = { in.getReadPointer (0), in.getReadPointer (1) };
        engine->process (inPtrs, outPtrs.data(), n);

        int off = 0;
        if (skip > 0) { off = (int) std::min<juce::int64> (skip, n); skip -= off; }
        const int count = (int) std::min<juce::int64> (n - off, totalLen - written);
        if (count > 0)
        {
            for (int s = 0; s < kNumStems; ++s)
            {
                const float* chans[2] = { outPtrs[(size_t) (2 * s)] + off, outPtrs[(size_t) (2 * s + 1)] + off };
                writers[(size_t) s]->writeFromFloatArrays (chans, 2, count);
            }
            written += count;
        }
        if (progress (juce::jlimit (0.0, 1.0, (double) written / (double) std::max<juce::int64> (1, totalLen))))
        {
            error = "Cancelled";
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------- state
void StemSplitProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("modelPath", getModelFile().getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void StemSplitProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (vt);
            const auto mp = vt.getProperty ("modelPath").toString();
            const juce::File f (mp);
            if (mp.isNotEmpty() && f.existsAsFile() && f != getModelFile())
                loadModel (f);   // when not yet prepared this just remembers the path
        }
    }
}

juce::AudioProcessorEditor* StemSplitProcessor::createEditor() { return new StemSplitEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StemSplitProcessor(); }
