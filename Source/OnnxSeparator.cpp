#include "OnnxSeparator.h"

#if STEMSPLIT_ONNX

#include <onnxruntime_cxx_api.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace stemsplit;

struct OnnxSeparator::Impl
{
    static constexpr int kCh = kNumStems * 2;

    struct Worker : juce::Thread
    {
        explicit Worker (Impl& o) : juce::Thread ("StemSplit inference"), owner (o) {}
        void run() override { owner.workerLoop (*this); }
        Impl& owner;
    };

    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "StemSplit" };
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
    std::string inName, outName;
    int modelT = 343980;
    double modelSR = 44100.0;

    double hostSR = 44100.0;
    int C = 2, H = 1;
    std::array<std::vector<float>, 2> ring, job;
    int ringPos = 0, sinceJob = 0, startDelay = 0;
    std::atomic<bool> jobPending { false };
    juce::WaitableEvent wake;
    std::vector<std::vector<float>> ola, fifoData;
    std::vector<float> window, inTensor, padded, resampled;
    juce::AbstractFifo fifo { 1 };
    std::atomic<int> underruns { 0 }, dropped { 0 };
    bool offline = false;
    std::unique_ptr<Worker> worker;

    ~Impl() { stopWorker(); }

    void stopWorker()
    {
        if (worker != nullptr)
        {
            worker->signalThreadShouldExit();
            wake.signal();
            worker->stopThread (10000);
            worker.reset();
        }
    }

    void startWorker()
    {
        worker = std::make_unique<Worker> (*this);
        worker->startThread();
    }

    // ---------------------------------------------------------------- worker
    void workerLoop (Worker& w)
    {
        while (! w.threadShouldExit())
        {
            wake.wait (200);
            if (w.threadShouldExit()) break;
            if (! jobPending.load (std::memory_order_acquire)) continue;
            runChunk (w);
            jobPending.store (false, std::memory_order_release);
        }
    }

    void addWindowed (int sc, const float* src)
    {
        float* o = ola[(size_t) sc].data();
        for (int i = 0; i < C; ++i) o[i] += src[i] * window[(size_t) i];
    }

    void runChunk (Worker& w)
    {
        const int T = modelT;
        inTensor.assign ((size_t) 2 * (size_t) T, 0.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            if (C == T)
                std::memcpy (inTensor.data() + (size_t) ch * T, job[(size_t) ch].data(), sizeof (float) * (size_t) C);
            else
            {
                padded.assign ((size_t) C + 16, 0.0f);
                std::memcpy (padded.data(), job[(size_t) ch].data(), sizeof (float) * (size_t) C);
                juce::LagrangeInterpolator li;
                li.process ((double) C / (double) T, padded.data(), inTensor.data() + (size_t) ch * T, T);
            }
        }

        try
        {
            std::array<int64_t, 3> shape { 1, 2, (int64_t) T };
            auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value input = Ort::Value::CreateTensor<float> (mem, inTensor.data(), inTensor.size(),
                                                               shape.data(), shape.size());
            const char* inNames[]  = { inName.c_str() };
            const char* outNames[] = { outName.c_str() };
            auto outs = session->Run (Ort::RunOptions { nullptr }, inNames, &input, 1, outNames, 1);

            const auto info = outs[0].GetTensorTypeAndShapeInfo();
            if (info.GetElementCount() >= (size_t) kCh * (size_t) T)
            {
                const float* o = outs[0].GetTensorData<float>();
                for (int sc = 0; sc < kCh; ++sc)
                {
                    const float* src = o + (size_t) sc * T;
                    if (C == T)
                        addWindowed (sc, src);
                    else
                    {
                        padded.assign ((size_t) T + 16, 0.0f);
                        std::memcpy (padded.data(), src, sizeof (float) * (size_t) T);
                        resampled.assign ((size_t) C, 0.0f);
                        juce::LagrangeInterpolator li;
                        li.process ((double) T / (double) C, padded.data(), resampled.data(), C);
                        addWindowed (sc, resampled.data());
                    }
                }
            }
        }
        catch (const std::exception&) { /* emit what we have (silence) so timing stays aligned */ }

        emit (w);
    }

    void emit (Worker& w)
    {
        while (fifo.getFreeSpace() < H && ! w.threadShouldExit())
            juce::Thread::sleep (2);
        if (w.threadShouldExit()) return;

        int s1, b1, s2, b2;
        fifo.prepareToWrite (H, s1, b1, s2, b2);
        for (int sc = 0; sc < kCh; ++sc)
        {
            const float* o = ola[(size_t) sc].data();
            if (b1 > 0) std::memcpy (fifoData[(size_t) sc].data() + s1, o, sizeof (float) * (size_t) b1);
            if (b2 > 0) std::memcpy (fifoData[(size_t) sc].data() + s2, o + b1, sizeof (float) * (size_t) b2);
        }
        fifo.finishedWrite (b1 + b2);

        for (int sc = 0; sc < kCh; ++sc)
        {
            auto& v = ola[(size_t) sc];
            std::memmove (v.data(), v.data() + H, sizeof (float) * (size_t) H);
            std::fill (v.begin() + H, v.end(), 0.0f);
        }
    }

    // ----------------------------------------------------------- audio thread
    void submit()
    {
        if (jobPending.load (std::memory_order_acquire))
        {
            if (! offline) { ++dropped; return; }
            while (jobPending.load (std::memory_order_acquire)) juce::Thread::sleep (1);
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& r = ring[(size_t) ch];
            auto& j = job[(size_t) ch];
            std::memcpy (j.data(), r.data() + ringPos, sizeof (float) * (size_t) (C - ringPos));
            std::memcpy (j.data() + (C - ringPos), r.data(), sizeof (float) * (size_t) ringPos);
        }
        jobPending.store (true, std::memory_order_release);
        wake.signal();
    }

    void readOutput (float* const* out, int off, int want)
    {
        if (offline)
        {
            const auto t0 = juce::Time::getMillisecondCounter();
            while (fifo.getNumReady() < want && juce::Time::getMillisecondCounter() - t0 < 120000)
                juce::Thread::sleep (1);
        }
        const int take = std::min (want, fifo.getNumReady());
        if (take < want) ++underruns;

        int s1 = 0, b1 = 0, s2 = 0, b2 = 0;
        if (take > 0) fifo.prepareToRead (take, s1, b1, s2, b2);
        for (int sc = 0; sc < kCh; ++sc)
        {
            float* o = out[sc] + off;
            if (b1 > 0) std::memcpy (o, fifoData[(size_t) sc].data() + s1, sizeof (float) * (size_t) b1);
            if (b2 > 0) std::memcpy (o + b1, fifoData[(size_t) sc].data() + s2, sizeof (float) * (size_t) b2);
            if (take < want) std::fill (o + take, o + want, 0.0f);
        }
        if (take > 0) fifo.finishedRead (b1 + b2);
    }
};

OnnxSeparator::OnnxSeparator() : impl (new Impl()) {}
OnnxSeparator::~OnnxSeparator() = default;

std::unique_ptr<OnnxSeparator> OnnxSeparator::create (const juce::File& model, juce::String& error)
{
    std::unique_ptr<OnnxSeparator> sep (new OnnxSeparator());
    auto& m = *sep->impl;
    try
    {
        m.opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
       #if JUCE_WINDOWS
        m.session = std::make_unique<Ort::Session> (m.env, model.getFullPathName().toWideCharPointer(), m.opts);
       #else
        m.session = std::make_unique<Ort::Session> (m.env, model.getFullPathName().toRawUTF8(), m.opts);
       #endif

        Ort::AllocatorWithDefaultOptions alloc;
        m.inName  = m.session->GetInputNameAllocated (0, alloc).get();
        m.outName = m.session->GetOutputNameAllocated (0, alloc).get();

        const auto ish = m.session->GetInputTypeInfo (0).GetTensorTypeAndShapeInfo().GetShape();
        if (ish.size() != 3 || (ish[1] != 2 && ish[1] > 0))
        {
            error = "input must be [1,2,T]";
            return nullptr;
        }
        if (ish[2] > 0) m.modelT = (int) ish[2];

        const auto osh = m.session->GetOutputTypeInfo (0).GetTensorTypeAndShapeInfo().GetShape();
        if (osh.size() != 4 || (osh[1] > 0 && osh[1] != kNumStems))
        {
            error = "output must be [1,30,2,T]";
            return nullptr;
        }

        auto meta = m.session->GetModelMetadata();
        auto sr = meta.LookupCustomMetadataMapAllocated ("sample_rate", alloc);
        if (sr) m.modelSR = std::max (8000.0, std::atof (sr.get()));
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return nullptr;
    }
    return sep;
}

void OnnxSeparator::prepare (double sampleRate, int)
{
    auto& m = *impl;
    m.stopWorker();
    m.hostSR = sampleRate;
    m.C = std::max (2, (int) std::llround ((double) m.modelT * sampleRate / m.modelSR));
    m.C &= ~1;
    m.H = m.C / 2;

    for (int ch = 0; ch < 2; ++ch)
    {
        m.ring[(size_t) ch].assign ((size_t) m.C, 0.0f);
        m.job[(size_t) ch].assign ((size_t) m.C, 0.0f);
    }
    m.ola.assign (Impl::kCh, std::vector<float> ((size_t) m.C, 0.0f));
    m.fifoData.assign (Impl::kCh, std::vector<float> ((size_t) m.C, 0.0f));
    m.fifo.setTotalSize (m.C);               // = 2 * H frames
    m.window.resize ((size_t) m.C);
    for (int i = 0; i < m.C; ++i)
        m.window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) m.C);

    reset();
}

void OnnxSeparator::reset()
{
    auto& m = *impl;
    m.stopWorker();
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (m.ring[(size_t) ch].begin(), m.ring[(size_t) ch].end(), 0.0f);
        std::fill (m.job[(size_t) ch].begin(), m.job[(size_t) ch].end(), 0.0f);
    }
    for (auto& v : m.ola) std::fill (v.begin(), v.end(), 0.0f);
    m.fifo.reset();
    m.ringPos = 0;
    m.sinceJob = 0;
    m.startDelay = m.C;          // first real output arrives C samples later
    m.jobPending = false;
    m.underruns = 0;
    m.dropped = 0;
    m.startWorker();
}

int OnnxSeparator::getLatencySamples() const { return impl->C + impl->H; }
void OnnxSeparator::setOffline (bool b) { impl->offline = b; }

void OnnxSeparator::process (const float* const* in, float* const* out, int n)
{
    auto& m = *impl;
    for (int i = 0; i < n; ++i)
    {
        m.ring[0][(size_t) m.ringPos] = in[0][i];
        m.ring[1][(size_t) m.ringPos] = in[1][i];
        m.ringPos = (m.ringPos + 1) % m.C;
        if (++m.sinceJob >= m.H) { m.sinceJob = 0; m.submit(); }
    }

    int off = 0;
    if (m.startDelay > 0)
    {
        off = std::min (m.startDelay, n);
        m.startDelay -= off;
        for (int sc = 0; sc < Impl::kCh; ++sc)
            std::fill (out[sc], out[sc] + off, 0.0f);
    }
    if (n - off > 0)
        m.readOutput (out, off, n - off);
}

#endif // STEMSPLIT_ONNX
