#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    enum class Fmt { db, hz, ms, sec, ratio, q, fader };

    juce::String formatValue (Fmt f, float v)
    {
        switch (f)
        {
            case Fmt::db:    return juce::String (v, 1) + " dB";
            case Fmt::hz:    return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                                 : juce::String ((int) std::round (v)) + " Hz";
            case Fmt::ms:    return juce::String (v, 1) + " ms";
            case Fmt::sec:   return juce::String (v, 2) + " s";
            case Fmt::ratio: return juce::String (v, 1) + ":1";
            case Fmt::q:     return juce::String (v, 2);
            case Fmt::fader: return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB";
        }
        return {};
    }

    void atomicMax (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v)) {}
    }

    void atomicMin (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v < cur && ! a.compare_exchange_weak (cur, v)) {}
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ChannelStripAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout] (const char* id, const char* name, float lo, float hi, float def,
                               float step, Fmt fmt, float centre = 0.0f)
    {
        juce::NormalisableRange<float> range (lo, hi, step);
        if (centre > 0.0f)
            range.setSkewForCentre (centre);

        auto attrs = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([fmt] (float v, int) { return formatValue (fmt, v); })
            .withValueFromStringFunction ([] (const juce::String& t)
            {
                if (t.containsIgnoreCase ("inf")) return -60.0f;
                auto v = t.getFloatValue();
                if (t.containsIgnoreCase ("k")) v *= 1000.0f;
                return v;
            });

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attrs));
    };

    auto addBool = [&layout] (const char* id, const char* name, bool def = false)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };

    // Input
    addFloat ("inTrim", "Input Trim", -24.0f, 24.0f, 0.0f, 0.1f, Fmt::db);
    addBool  ("phase", "Phase Invert");

    // Filters
    addFloat ("hpfFreq", "HPF Freq", 16.0f, 350.0f, 80.0f, 1.0f, Fmt::hz, 70.0f);
    addBool  ("hpfOn", "HPF In");
    addFloat ("lpfFreq", "LPF Freq", 3000.0f, 20000.0f, 18000.0f, 1.0f, Fmt::hz, 8000.0f);
    addBool  ("lpfOn", "LPF In");

    // Gate / expander
    addFloat ("gateThr", "Gate Threshold", -70.0f, 0.0f, -50.0f, 0.1f, Fmt::db);
    addFloat ("gateRange", "Gate Range", -60.0f, 0.0f, -30.0f, 0.1f, Fmt::db);
    addFloat ("gateRel", "Gate Release", 0.05f, 4.0f, 0.3f, 0.01f, Fmt::sec, 0.5f);
    addBool  ("gateOn", "Gate In");

    // Compressor
    addFloat ("compThr", "Comp Threshold", -30.0f, 10.0f, -10.0f, 0.1f, Fmt::db);
    addFloat ("compRatio", "Comp Ratio", 1.0f, 20.0f, 4.0f, 0.1f, Fmt::ratio, 4.0f);
    addFloat ("compAtk", "Comp Attack", 0.1f, 50.0f, 10.0f, 0.1f, Fmt::ms, 5.0f);
    addFloat ("compRel", "Comp Release", 0.05f, 2.0f, 0.3f, 0.01f, Fmt::sec, 0.4f);
    addFloat ("compMakeup", "Comp Makeup", 0.0f, 24.0f, 0.0f, 0.1f, Fmt::db);
    addBool  ("compOn", "Comp In");

    // EQ
    addFloat ("lfFreq", "LF Freq", 30.0f, 450.0f, 100.0f, 1.0f, Fmt::hz, 120.0f);
    addFloat ("lfGain", "LF Gain", -15.0f, 15.0f, 0.0f, 0.1f, Fmt::db);
    addBool  ("lfBell", "LF Bell");
    addFloat ("lmfFreq", "LMF Freq", 200.0f, 2500.0f, 700.0f, 1.0f, Fmt::hz, 700.0f);
    addFloat ("lmfGain", "LMF Gain", -15.0f, 15.0f, 0.0f, 0.1f, Fmt::db);
    addFloat ("lmfQ", "LMF Q", 0.4f, 4.0f, 1.0f, 0.01f, Fmt::q, 1.2f);
    addFloat ("hmfFreq", "HMF Freq", 600.0f, 7000.0f, 3000.0f, 1.0f, Fmt::hz, 2500.0f);
    addFloat ("hmfGain", "HMF Gain", -15.0f, 15.0f, 0.0f, 0.1f, Fmt::db);
    addFloat ("hmfQ", "HMF Q", 0.4f, 4.0f, 1.0f, 0.01f, Fmt::q, 1.2f);
    addFloat ("hfFreq", "HF Freq", 1500.0f, 16000.0f, 8000.0f, 1.0f, Fmt::hz, 6000.0f);
    addFloat ("hfGain", "HF Gain", -15.0f, 15.0f, 0.0f, 0.1f, Fmt::db);
    addBool  ("hfBell", "HF Bell");
    addBool  ("eqOn", "EQ In");

    // Routing / output
    addBool  ("dynPost", "Dynamics Post EQ");
    addFloat ("outLevel", "Output Level", -60.0f, 12.0f, 0.0f, 0.1f, Fmt::fader);

    return layout;
}

//==============================================================================
ChannelStripAudioProcessor::ChannelStripAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
}

bool ChannelStripAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    return in == out;
}

ChannelStripAudioProcessor::Params ChannelStripAudioProcessor::readParams() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto b = [&f] (const char* id) { return f (id) > 0.5f; };

    Params p;
    p.inTrimDb = f ("inTrim");      p.phase = b ("phase");
    p.hpfFreq = f ("hpfFreq");      p.hpfOn = b ("hpfOn");
    p.lpfFreq = f ("lpfFreq");      p.lpfOn = b ("lpfOn");
    p.gateThr = f ("gateThr");      p.gateRange = f ("gateRange");
    p.gateRel = f ("gateRel");      p.gateOn = b ("gateOn");
    p.compThr = f ("compThr");      p.compRatio = f ("compRatio");
    p.compAtk = f ("compAtk");      p.compRel = f ("compRel");
    p.compMakeup = f ("compMakeup"); p.compOn = b ("compOn");
    p.lfFreq = f ("lfFreq");        p.lfGain = f ("lfGain");   p.lfBell = b ("lfBell");
    p.lmfFreq = f ("lmfFreq");      p.lmfGain = f ("lmfGain"); p.lmfQ = f ("lmfQ");
    p.hmfFreq = f ("hmfFreq");      p.hmfGain = f ("hmfGain"); p.hmfQ = f ("hmfQ");
    p.hfFreq = f ("hfFreq");        p.hfGain = f ("hfGain");   p.hfBell = b ("hfBell");
    p.eqOn = b ("eqOn");
    p.dynPost = b ("dynPost");
    p.outDb = f ("outLevel");
    return p;
}

//==============================================================================
void ChannelStripAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;

    const auto p = readParams();
    trimSmooth.reset (sr, 0.03);
    outSmooth.reset (sr, 0.03);
    trimSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.inTrimDb) * (p.phase ? -1.0f : 1.0f));
    outSmooth.setCurrentAndTargetValue (p.outDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (p.outDb));

    for (int ch = 0; ch < 2; ++ch)
    {
        hpf1[ch].reset(); hpf2[ch].reset(); lpf[ch].reset();
        lf[ch].reset(); lmf[ch].reset(); hmf[ch].reset(); hf[ch].reset();
    }

    gateEnv = 0.0f; gateGain = 1.0f; gateOpen = false;
    compGrDb = 0.0f;
    msIn = msOut = 0.0f;
}

//==============================================================================
void ChannelStripAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    if (buffer.getNumChannels() == 0 || n == 0)
        return;

    const Params p = readParams();

    stageInput (buffer, p);

    if (p.hpfOn || p.lpfOn)
        stageFilters (buffer, p);

    if (! p.dynPost)
        stageDynamics (buffer, p);

    if (p.eqOn)
        stageEq (buffer, p);

    if (p.dynPost)
        stageDynamics (buffer, p);

    stageOutput (buffer, p);
}

//==============================================================================
void ChannelStripAudioProcessor::stageInput (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    trimSmooth.setTargetValue (juce::Decibels::decibelsToGain (p.inTrimDb) * (p.phase ? -1.0f : 1.0f));

    float* data[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };

    float peak = 0.0f, sumSq = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float g = trimSmooth.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float v = data[ch][i] * g;
            data[ch][i] = v;
            peak = juce::jmax (peak, std::abs (v));
            sumSq += v * v;
        }
    }

    const float ms = sumSq / (float) (n * numCh);
    const float a = 1.0f - std::exp (-(float) n / (float) sr / 0.3f);
    msIn += a * (ms - msIn);

    atomicMax (inPeak, peak);
    inRms.store (std::sqrt (msIn));
}

void ChannelStripAudioProcessor::stageFilters (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* d = buffer.getWritePointer (ch);

        if (p.hpfOn)
        {
            // 18 dB/oct high-pass: 1st order + 2nd order Butterworth
            hpf1[ch].makeHighPass1 (sr, p.hpfFreq);
            hpf2[ch].makeHighPass (sr, p.hpfFreq, 1.0);
            for (int i = 0; i < n; ++i)
                d[i] = hpf2[ch].process (hpf1[ch].process (d[i]));
        }
        else
        {
            hpf1[ch].reset(); hpf2[ch].reset();
        }

        if (p.lpfOn)
        {
            // 12 dB/oct low-pass
            lpf[ch].makeLowPass (sr, p.lpfFreq, 0.7071);
            for (int i = 0; i < n; ++i)
                d[i] = lpf[ch].process (d[i]);
        }
        else
        {
            lpf[ch].reset();
        }
    }
}

void ChannelStripAudioProcessor::stageDynamics (juce::AudioBuffer<float>& buffer, const Params& p)
{
    if (! p.gateOn && ! p.compOn)
    {
        gateGain = 1.0f;
        compGrDb = 0.0f;
        return;
    }

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    float* d0 = buffer.getWritePointer (0);
    float* d1 = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    // Gate constants
    const float gateAtk   = std::exp (-1.0f / (0.0005f * (float) sr));
    const float gateRelC  = std::exp (-1.0f / (p.gateRel * (float) sr));
    const float gateEnvRel = std::exp (-1.0f / (0.05f * (float) sr));
    const float gateThrLin = juce::Decibels::decibelsToGain (p.gateThr);
    const float gateCloseLin = gateThrLin * 0.7079f; // 3 dB hysteresis
    const float rangeLin = juce::Decibels::decibelsToGain (p.gateRange);

    // Compressor constants
    const float atk = std::exp (-1.0f / (p.compAtk * 0.001f * (float) sr));
    const float rel = std::exp (-1.0f / (p.compRel * (float) sr));
    const float ratio = juce::jmax (1.0f, p.compRatio);
    const float knee = 6.0f;
    float minGr = 0.0f;

    if (! p.gateOn) gateGain = 1.0f;
    if (! p.compOn) compGrDb = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float l = d0[i];
        float r = d1 != nullptr ? d1[i] : l;

        if (p.gateOn)
        {
            const float lvl = juce::jmax (std::abs (l), std::abs (r));
            gateEnv = lvl > gateEnv ? lvl : lvl + gateEnvRel * (gateEnv - lvl);

            if (! gateOpen && gateEnv > gateThrLin)      gateOpen = true;
            else if (gateOpen && gateEnv < gateCloseLin) gateOpen = false;

            const float target = gateOpen ? 1.0f : rangeLin;
            const float c = target > gateGain ? gateAtk : gateRelC;
            gateGain = target + c * (gateGain - target);

            l *= gateGain;
            r *= gateGain;
        }

        if (p.compOn)
        {
            const float lvl = juce::jmax (std::abs (l), std::abs (r));
            const float xDb = 20.0f * std::log10 (juce::jmax (lvl, 1.0e-6f));
            const float over = xDb - p.compThr;

            float yDb;
            if (2.0f * over < -knee)
                yDb = xDb;
            else if (2.0f * std::abs (over) <= knee)
            {
                const float t = over + knee * 0.5f;
                yDb = xDb + (1.0f / ratio - 1.0f) * (t * t) / (2.0f * knee);
            }
            else
                yDb = p.compThr + over / ratio;

            const float target = yDb - xDb; // <= 0
            const float c = target < compGrDb ? atk : rel;
            compGrDb = target + c * (compGrDb - target);

            const float g = juce::Decibels::decibelsToGain (compGrDb + p.compMakeup);
            l *= g;
            r *= g;
            minGr = juce::jmin (minGr, compGrDb);
        }

        d0[i] = l;
        if (d1 != nullptr)
            d1[i] = r;
    }

    if (p.compOn)
        atomicMin (gainReduction, minGr);
}

void ChannelStripAudioProcessor::stageEq (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* d = buffer.getWritePointer (ch);

        const bool lfActive  = std::abs (p.lfGain)  > 0.01f;
        const bool lmfActive = std::abs (p.lmfGain) > 0.01f;
        const bool hmfActive = std::abs (p.hmfGain) > 0.01f;
        const bool hfActive  = std::abs (p.hfGain)  > 0.01f;

        if (lfActive)
        {
            if (p.lfBell) lf[ch].makePeak (sr, p.lfFreq, 0.9, p.lfGain);
            else          lf[ch].makeLowShelf (sr, p.lfFreq, 0.7071, p.lfGain);
        }
        else lf[ch].reset();

        if (lmfActive) lmf[ch].makePeak (sr, p.lmfFreq, p.lmfQ, p.lmfGain); else lmf[ch].reset();
        if (hmfActive) hmf[ch].makePeak (sr, p.hmfFreq, p.hmfQ, p.hmfGain); else hmf[ch].reset();

        if (hfActive)
        {
            if (p.hfBell) hf[ch].makePeak (sr, p.hfFreq, 0.9, p.hfGain);
            else          hf[ch].makeHighShelf (sr, p.hfFreq, 0.7071, p.hfGain);
        }
        else hf[ch].reset();

        for (int i = 0; i < n; ++i)
        {
            float x = d[i];
            if (lfActive)  x = lf[ch].process (x);
            if (lmfActive) x = lmf[ch].process (x);
            if (hmfActive) x = hmf[ch].process (x);
            if (hfActive)  x = hf[ch].process (x);
            d[i] = x;
        }
    }
}

void ChannelStripAudioProcessor::stageOutput (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    outSmooth.setTargetValue (p.outDb <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (p.outDb));

    float* data[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };

    float peak = 0.0f, sumSq = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float g = outSmooth.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float v = data[ch][i] * g;
            data[ch][i] = v;
            peak = juce::jmax (peak, std::abs (v));
            sumSq += v * v;
        }
    }

    const float ms = sumSq / (float) (n * numCh);
    const float a = 1.0f - std::exp (-(float) n / (float) sr / 0.3f);
    msOut += a * (ms - msOut);

    atomicMax (outPeak, peak);
    outRms.store (std::sqrt (msOut));
}

//==============================================================================
juce::AudioProcessorEditor* ChannelStripAudioProcessor::createEditor()
{
    return new ChannelStripEditor (*this);
}

void ChannelStripAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ChannelStripAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void ChannelStripAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    const juce::ScopedLock sl (trackNameLock);
    trackName = properties.name.has_value() ? *properties.name : juce::String();
}

juce::String ChannelStripAudioProcessor::getTrackName() const
{
    const juce::ScopedLock sl (trackNameLock);
    return trackName;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChannelStripAudioProcessor();
}
