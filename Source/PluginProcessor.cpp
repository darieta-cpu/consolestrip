#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    enum class Fmt { db, hz, ms, sec, ratio, q, fader, pct };

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
            case Fmt::pct:   return juce::String ((int) std::round (v)) + " %";
            case Fmt::fader: return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB";
        }
        return {};
    }

    void atomicMax (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v)) {}
    }

    // Curve di saturazione dei 3 modelli di console (pendenza 1 nell'origine)
    inline float consoleShape (int model, float u) noexcept
    {
        switch (model)
        {
            case 1:  return u >= 0.0f ? std::tanh (u) : std::tanh (0.75f * u) / 0.75f;   // asimmetrica: armoniche pari
            case 2:  { const float u2 = u * u; return u / std::sqrt (std::sqrt (1.0f + u2 * u2)); } // ginocchio piu' netto
            default: return std::tanh (u);                                                  // simmetrica, morbida
        }
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

    // Console (saturazione a monte)
    addBool  ("consoleOn", "Console In");
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "consoleModel", 1 }, "Console Model",
                                                              juce::StringArray { "CLEAN", "WARM", "PUNCH" }, 0));
    addFloat ("consoleDrive", "Console Drive", 0.0f, 100.0f, 30.0f, 1.0f, Fmt::pct);
    addBool  ("consoleTx", "Console Transformer");

    // Filters
    addFloat ("hpfFreq", "HPF Freq", 16.0f, 350.0f, 80.0f, 1.0f, Fmt::hz, 70.0f);
    addBool  ("hpfOn", "HPF In");
    addFloat ("lpfFreq", "LPF Freq", 3000.0f, 20000.0f, 18000.0f, 1.0f, Fmt::hz, 8000.0f);
    addBool  ("lpfOn", "LPF In");

    // Gate / expander
    addFloat ("gateThr", "Gate Threshold", -70.0f, 0.0f, -50.0f, 0.1f, Fmt::db);
    addFloat ("gateRange", "Gate Range", -60.0f, 0.0f, -30.0f, 0.1f, Fmt::db);
    addFloat ("gateRel", "Gate Release", 0.05f, 4.0f, 0.3f, 0.01f, Fmt::sec, 0.5f);
    addFloat ("gateHold", "Gate Hold", 0.0f, 500.0f, 50.0f, 1.0f, Fmt::ms, 100.0f);
    addBool  ("gateOn", "Gate In");

    // Compressor
    addFloat ("compThr", "Comp Threshold", -30.0f, 10.0f, -10.0f, 0.1f, Fmt::db);
    addFloat ("compRatio", "Comp Ratio", 1.0f, 20.0f, 4.0f, 0.1f, Fmt::ratio, 4.0f);
    addFloat ("compAtk", "Comp Attack", 0.1f, 50.0f, 10.0f, 0.1f, Fmt::ms, 5.0f);
    addFloat ("compRel", "Comp Release", 0.05f, 2.0f, 0.3f, 0.01f, Fmt::sec, 0.4f);
    addFloat ("compMakeup", "Comp Makeup", 0.0f, 24.0f, 0.0f, 0.1f, Fmt::db);
    addFloat ("compScHpf", "Comp SC HPF", 20.0f, 400.0f, 20.0f, 1.0f, Fmt::hz, 100.0f);
    addFloat ("compMix", "Comp Mix", 0.0f, 100.0f, 100.0f, 1.0f, Fmt::pct);
    addBool  ("compOn", "Comp In");

    // De-esser
    addFloat ("deEssFreq", "DeEss Freq", 3000.0f, 10000.0f, 6500.0f, 1.0f, Fmt::hz, 6000.0f);
    addFloat ("deEssThr", "DeEss Threshold", -60.0f, 0.0f, -30.0f, 0.1f, Fmt::db);
    addFloat ("deEssRange", "DeEss Range", 0.0f, 20.0f, 10.0f, 0.1f, Fmt::db);
    addBool  ("deEssListen", "DeEss Listen");
    addBool  ("deEssOn", "DeEss In");

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
    p.gateHold = f ("gateHold");
    p.compThr = f ("compThr");      p.compRatio = f ("compRatio");
    p.compAtk = f ("compAtk");      p.compRel = f ("compRel");
    p.compMakeup = f ("compMakeup"); p.compOn = b ("compOn");
    p.compScHpf = f ("compScHpf");  p.compMix = f ("compMix") / 100.0f;
    p.deEssFreq = f ("deEssFreq");  p.deEssThr = f ("deEssThr");
    p.deEssRange = f ("deEssRange"); p.deEssListen = b ("deEssListen"); p.deEssOn = b ("deEssOn");
    p.lfFreq = f ("lfFreq");        p.lfGain = f ("lfGain");   p.lfBell = b ("lfBell");
    p.lmfFreq = f ("lmfFreq");      p.lmfGain = f ("lmfGain"); p.lmfQ = f ("lmfQ");
    p.hmfFreq = f ("hmfFreq");      p.hmfGain = f ("hmfGain"); p.hmfQ = f ("hmfQ");
    p.hfFreq = f ("hfFreq");        p.hfGain = f ("hfGain");   p.hfBell = b ("hfBell");
    p.eqOn = b ("eqOn");
    p.dynPost = b ("dynPost");
    p.consoleOn = b ("consoleOn");
    p.consoleTx = b ("consoleTx");
    p.consoleModel = juce::jlimit (0, 2, (int) std::round (f ("consoleModel")));
    p.consoleDrive = f ("consoleDrive") / 100.0f;
    p.outDb = f ("outLevel");
    return p;
}

//==============================================================================
void ChannelStripAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    // Console stage
    maxBlock = juce::jmax (samplesPerBlock, 64);
    oversampling.initProcessing ((size_t) maxBlock);
    oversampling.reset();
    dryBuffer.setSize (2, maxBlock);
    dryBuffer.clear();
    consoleMix.reset (sr, 0.02);
    consoleMix.setCurrentAndTargetValue (0.0f);
    consoleWasOn = false;
    cachedModel = -1;
    dcR = std::exp (-2.0 * Biquad::pi() * 8.0 / sr);
    for (int ch = 0; ch < 2; ++ch)
    {
        txLow[ch].makeLowShelf (sr, 90.0, 0.7071, 2.0);     // "peso" sui bassi prima della saturazione
        txHigh[ch].makeHighShelf (sr, 12000.0, 0.7071, -1.5); // lieve cedimento sulle alte dopo
        txLow[ch].reset();
        txHigh[ch].reset();
        dcX[ch] = dcY[ch] = 0.0;
    }

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

    gateEnv = 0.0f; gateGain = 1.0f; gateOpen = false; gateHoldCnt = 0;
    dsEnv = 0.0f;
    for (int ch = 0; ch < 2; ++ch) { scHpf[ch].reset(); dsBp[ch].reset(); }
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
    stageConsole (buffer, p);

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

void ChannelStripAudioProcessor::updateConsoleShaper (int model, float drive01)
{
    if (model == cachedModel && drive01 == cachedDrive)
        return;

    cachedModel = model;
    cachedDrive = drive01;

    static constexpr float maxDriveDb[3] = { 12.0f, 15.0f, 18.0f };
    consoleGain = juce::Decibels::decibelsToGain (drive01 * maxDriveDb[model]);
    consoleInvGain = 1.0f / consoleGain;

    // Compensazione di livello: a -18 dBFS RMS (livello nominale) il segnale esce con lo stesso RMS
    constexpr int N = 512;
    const double A = std::pow (10.0, -15.0 / 20.0);   // picco di una sinusoide a -18 dBFS RMS
    double sumIn = 0.0, sumOut = 0.0, mean = 0.0;
    float y[N];
    for (int n = 0; n < N; ++n)
    {
        const float x = (float) (A * std::sin (2.0 * Biquad::pi() * 8.0 * (double) n / (double) N));
        y[n] = consoleShape (model, x * consoleGain) * consoleInvGain;
        sumIn += (double) x * (double) x;
        mean += (double) y[n];
    }
    mean /= (double) N;
    for (int n = 0; n < N; ++n)
        sumOut += ((double) y[n] - mean) * ((double) y[n] - mean);

    consoleMakeup = sumOut > 0.0 ? (float) std::sqrt (sumIn / sumOut) : 1.0f;
}

void ChannelStripAudioProcessor::stageConsole (juce::AudioBuffer<float>& buffer, const Params& p)
{
    consoleMix.setTargetValue (p.consoleOn ? 1.0f : 0.0f);

    // modulo spento e dissolvenza finita: nessuna elaborazione
    if (! p.consoleOn && consoleMix.getCurrentValue() <= 0.0f)
    {
        consoleWasOn = false;
        return;
    }

    if (! consoleWasOn)
    {
        oversampling.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            txLow[ch].reset();
            txHigh[ch].reset();
            dcX[ch] = dcY[ch] = 0.0;
        }
        consoleWasOn = true;
    }

    updateConsoleShaper (p.consoleModel, p.consoleDrive);

    if (! p.consoleTx)
        for (int ch = 0; ch < 2; ++ch) { txLow[ch].reset(); txHigh[ch].reset(); }

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int total = buffer.getNumSamples();
    juce::dsp::AudioBlock<float> whole (buffer);

    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - pos);

        for (int ch = 0; ch < numCh; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, pos, n);

        auto block = whole.getSubBlock ((size_t) pos, (size_t) n);

        if (p.consoleTx)
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = block.getChannelPointer ((size_t) ch);
                for (int i = 0; i < n; ++i)
                    d[i] = txLow[ch].process (d[i]);
            }

        auto up = oversampling.processSamplesUp (block);
        const int upN = (int) up.getNumSamples();
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = up.getChannelPointer ((size_t) ch);
            for (int i = 0; i < upN; ++i)
                d[i] = consoleShape (p.consoleModel, d[i] * consoleGain) * consoleInvGain;
        }
        oversampling.processSamplesDown (block);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = block.getChannelPointer ((size_t) ch);
            for (int i = 0; i < n; ++i)
            {
                const double x = (double) d[i];
                const double y = x - dcX[ch] + dcR * dcY[ch];   // elimina il DC delle curve asimmetriche
                dcX[ch] = x;
                dcY[ch] = y;

                float v = (float) y * consoleMakeup;
                if (p.consoleTx)
                    v = txHigh[ch].process (v);
                d[i] = v;
            }
        }

        // dissolvenza dry/wet (attivazione e disattivazione senza click)
        for (int i = 0; i < n; ++i)
        {
            const float m = consoleMix.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* out = buffer.getWritePointer (ch) + pos;
                out[i] = dryBuffer.getSample (ch, i) * (1.0f - m) + out[i] * m;
            }
        }
    }
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
    if (! p.gateOn && ! p.compOn && ! p.deEssOn)
    {
        gateGain = 1.0f;
        compGrDb = 0.0f;
        dsEnv = 0.0f;
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
    const int holdSamples = (int) (p.gateHold * 0.001 * sr);

    // Compressor constants
    const float atk = std::exp (-1.0f / (p.compAtk * 0.001f * (float) sr));
    const float rel = std::exp (-1.0f / (p.compRel * (float) sr));
    const float ratio = juce::jmax (1.0f, p.compRatio);
    const float knee = 6.0f;
    float minGr = 0.0f;

    // Sidechain high-pass del compressore (20 Hz = escluso)
    const bool useScHpf = p.compOn && p.compScHpf > 21.0f;
    if (useScHpf)
        for (int ch = 0; ch < numCh; ++ch)
            scHpf[ch].makeHighPass (sr, p.compScHpf, 0.7071);
    else
        for (int ch = 0; ch < 2; ++ch)
            scHpf[ch].reset();

    // De-esser: banda di rilevamento e di riduzione (split-band)
    const float dsAtk = std::exp (-1.0f / (0.001f * (float) sr));
    const float dsRel = std::exp (-1.0f / (0.06f * (float) sr));
    float dsMinGr = 0.0f;
    if (p.deEssOn)
        for (int ch = 0; ch < numCh; ++ch)
            dsBp[ch].makeBandPass (sr, p.deEssFreq, 1.2);
    else
    {
        dsEnv = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            dsBp[ch].reset();
    }

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

            if (gateEnv > gateThrLin)
            {
                gateOpen = true;
                gateHoldCnt = holdSamples;      // ogni volta che supera la soglia, riparte la tenuta
            }
            else if (gateOpen && gateEnv < gateCloseLin)
            {
                if (gateHoldCnt > 0) --gateHoldCnt;
                else                 gateOpen = false;
            }

            const float target = gateOpen ? 1.0f : rangeLin;
            const float c = target > gateGain ? gateAtk : gateRelC;
            gateGain = target + c * (gateGain - target);

            l *= gateGain;
            r *= gateGain;
        }

        if (p.compOn)
        {
            // il rilevatore legge il segnale filtrato, il guadagno si applica al segnale intero
            float scL = l, scR = r;
            if (useScHpf)
            {
                scL = scHpf[0].process (l);
                scR = d1 != nullptr ? scHpf[1].process (r) : scL;
            }

            const float lvl = juce::jmax (std::abs (scL), std::abs (scR));
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

            // dry/wet (compressione parallela): il makeup agisce sul ramo compresso
            const float g = juce::Decibels::decibelsToGain (compGrDb + p.compMakeup);
            const float gEff = (1.0f - p.compMix) + p.compMix * g;
            l *= gEff;
            r *= gEff;
            minGr = juce::jmin (minGr, compGrDb);
        }

        if (p.deEssOn)
        {
            const float bpL = dsBp[0].process (l);
            const float bpR = d1 != nullptr ? dsBp[1].process (r) : bpL;

            if (p.deEssListen)
            {
                l = bpL;
                r = bpR;
            }
            else
            {
                const float lvl = juce::jmax (std::abs (bpL), std::abs (bpR));
                const float c = lvl > dsEnv ? dsAtk : dsRel;
                dsEnv = lvl + c * (dsEnv - lvl);

                const float envDb = 20.0f * std::log10 (juce::jmax (dsEnv, 1.0e-6f));
                const float over = envDb - p.deEssThr;
                const float grDb = over > 0.0f ? juce::jmin (p.deEssRange, over * 0.75f) : 0.0f; // rapporto 4:1

                // toglie dalla banda solo la parte da ridurre: fuori banda il segnale resta identico
                const float removal = 1.0f - juce::Decibels::decibelsToGain (-grDb);
                l -= bpL * removal;
                r -= bpR * removal;
                dsMinGr = juce::jmin (dsMinGr, -grDb);
            }
        }

        d0[i] = l;
        if (d1 != nullptr)
            d1[i] = r;
    }

    if (p.compOn)
        atomicMin (gainReduction, minGr);

    if (p.deEssOn)
        atomicMin (deEssReduction, dsMinGr);
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
    trackName = properties.name;
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
