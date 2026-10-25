#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg    (0xff222326);
    const juce::Colour kPanel (0xff2e2f33);
    const juce::Colour kText  (0xffc9cbd1);
    const juce::Colour kAmber (0xffe8a317);
}

//==============================================================================
void StripLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float pos, float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float r = d * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff17181a));
    g.strokePath (track, juce::PathStrokeType (2.5f));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, r, r, 0.0f, startAngle, angle, true);
    g.setColour (juce::Colour (0xff8c8f96));
    g.strokePath (value, juce::PathStrokeType (2.5f));

    const float capR = r * 0.78f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (centre.x - capR, centre.y - capR + 2.0f, capR * 2.0f, capR * 2.0f);

    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.5f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -capR, 3.0f, capR * 0.55f, 1.0f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (juce::Colour (0xfff2f2f2));
    g.fillPath (pointer);
}

//==============================================================================
void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0d0d0f));
    g.fillRoundedRectangle (b, 3.0f);

    auto bar = b.reduced (3.0f);
    if (mode == Mode::level)
        bar.removeFromRight (24.0f);

    const float minDb = -60.0f, maxDb = 6.0f, grRange = 24.0f;

    auto dbToY = [this, bar, minDb, maxDb, grRange] (float db)
    {
        if (mode == Mode::level)
        {
            const float n = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
            return bar.getBottom() - n * bar.getHeight();
        }
        const float n = juce::jlimit (0.0f, 1.0f, -db / grRange);
        return bar.getY() + n * bar.getHeight();
    };

    if (mode == Mode::level)
    {
        juce::ColourGradient grad (juce::Colour (0xffe53935), 0.0f, bar.getY(),
                                   juce::Colour (0xff2e9e4f), 0.0f, bar.getBottom(), false);
        grad.addColour (0.091, juce::Colour (0xffe53935));
        grad.addColour (0.150, juce::Colour (0xffff9800));
        grad.addColour (0.250, juce::Colour (0xffe6d23a));
        grad.addColour (0.330, juce::Colour (0xff5ccf5c));

        auto dim = grad;
        dim.multiplyOpacity (0.18f);
        g.setGradientFill (dim);
        g.fillRect (bar);

        if (rms > minDb)
        {
            const float yR = dbToY (rms);
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> (bar.getX(), yR, bar.getWidth(), juce::jmax (0.0f, bar.getBottom() - yR)));
        }

        if (showTarget)
        {
            const float y1 = dbToY (-15.0f), y2 = dbToY (-21.0f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (bar.getX(), y1, bar.getWidth(), y2 - y1));
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawHorizontalLine ((int) dbToY (-18.0f), bar.getX(), bar.getRight());
        }

        if (peak > minDb)
        {
            g.setColour (peak >= -0.5f ? juce::Colour (0xffff3b30) : juce::Colours::white);
            g.fillRect (bar.getX(), dbToY (peak) - 1.0f, bar.getWidth(), 2.0f);
        }

        const int ticks[] = { 0, -6, -12, -18, -24, -36, -48 };
        g.setFont (10.0f);
        for (int t : ticks)
        {
            const float y = dbToY ((float) t);
            g.setColour (juce::Colour (0xff6c6f76));
            g.drawHorizontalLine ((int) y, bar.getRight(), bar.getRight() + 4.0f);
            g.setColour (t == -18 && showTarget ? kAmber : juce::Colour (0xff9a9da5));
            g.drawText (juce::String (t), juce::Rectangle<float> (bar.getRight() + 5.0f, y - 6.0f, 20.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }
    else
    {
        if (rms < 0.0f)
        {
            const float yG = dbToY (rms);
            g.setColour (juce::Colour (0xffff9800));
            g.fillRect (juce::Rectangle<float> (bar.getX(), bar.getY(), bar.getWidth(), yG - bar.getY()));
        }

        const int ticks[] = { -3, -6, -10, -15, -20 };
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        for (int t : ticks)
            g.drawHorizontalLine ((int) dbToY ((float) t), bar.getX(), bar.getX() + 5.0f);
    }
}

//==============================================================================
ChannelStripEditor::ChannelStripEditor (ChannelStripAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      inMeter (LevelMeter::Mode::level, true),
      outMeter (LevelMeter::Mode::level, false),
      grMeter (LevelMeter::Mode::gainReduction)
{
    setLookAndFeel (&laf);

    const juce::Colour grey   (0xff8a8d94);
    const juce::Colour dark   (0xff2a2a2d);
    const juce::Colour red    (0xffc0392b);
    const juce::Colour green  (0xff3fa34d);
    const juce::Colour blue   (0xff3b7dd8);
    const juce::Colour white  (0xffd8d8d8);

    // Input
    addKnob ("inTrim", "TRIM", white);
    addToggle ("phase", "PHASE");

    // Filters
    addKnob ("hpfFreq", "HPF", grey);
    addToggle ("hpfOn", "HPF IN");
    addKnob ("lpfFreq", "LPF", grey);
    addToggle ("lpfOn", "LPF IN");

    // Gate
    addKnob ("gateThr", "THRESH", green);
    addKnob ("gateRange", "RANGE", green);
    addKnob ("gateRel", "RELEASE", green);
    addToggle ("gateOn", "GATE IN");

    // Compressor
    addKnob ("compThr", "THRESH", red);
    addKnob ("compRatio", "RATIO", red);
    addKnob ("compAtk", "ATTACK", red);
    addKnob ("compRel", "RELEASE", red);
    addKnob ("compMakeup", "MAKEUP", red);
    addToggle ("compOn", "COMP IN");

    // EQ
    addKnob ("lfGain", "LF GAIN", dark);
    addKnob ("lfFreq", "LF FREQ", dark);
    addToggle ("lfBell", "LF BELL");
    addKnob ("lmfGain", "LMF GAIN", blue);
    addKnob ("lmfFreq", "LMF FREQ", blue);
    addKnob ("lmfQ", "LMF Q", blue);
    addKnob ("hmfGain", "HMF GAIN", green);
    addKnob ("hmfFreq", "HMF FREQ", green);
    addKnob ("hmfQ", "HMF Q", green);
    addKnob ("hfGain", "HF GAIN", red);
    addKnob ("hfFreq", "HF FREQ", red);
    addToggle ("hfBell", "HF BELL");
    addToggle ("eqOn", "EQ IN");

    // Output
    addToggle ("dynPost", "DYN AFTER EQ");

    outFader.setSliderStyle (juce::Slider::LinearVertical);
    outFader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 16);
    outFader.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe0e0e0));
    outFader.setColour (juce::Slider::trackColourId, juce::Colour (0xff8c8f96));
    outFader.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff111113));
    outFader.setColour (juce::Slider::textBoxTextColourId, kText);
    outFader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (outFader);
    faderAttachment = std::make_unique<APVTS::SliderAttachment> (proc.apvts, "outLevel", outFader);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (grMeter);

    setupLabel (peakLabel, "PEAK --", 12.0f, juce::Justification::centred, true);
    setupLabel (rmsLabel, "RMS --", 12.0f, juce::Justification::centred, true);
    setupLabel (hintLabel, "GAIN STAGING\nAim for RMS around -18 dB (band), peaks below -6 dB. Use TRIM.",
                10.0f, juce::Justification::topLeft);
    setupLabel (outPeakLabel, "PEAK --", 11.0f, juce::Justification::centred, true);
    setupLabel (grCaption, "GR", 11.0f, juce::Justification::centred);
    setupLabel (outCaption, "OUT", 11.0f, juce::Justification::centred);
    setupLabel (faderCaption, "LEVEL", 11.0f, juce::Justification::centred);

    setSize (1020, 460);
    startTimerHz (30);
}

ChannelStripEditor::~ChannelStripEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ChannelStripEditor::setupLabel (juce::Label& l, const juce::String& text, float size,
                                     juce::Justification j, bool bold)
{
    auto font = juce::FontOptions (size);
    if (bold)
        font = font.withStyle ("Bold");

    l.setText (text, juce::dontSendNotification);
    l.setFont (font);
    l.setJustificationType (j);
    l.setColour (juce::Label::textColourId, kText);
    l.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (l);
}

void ChannelStripEditor::addKnob (const juce::String& id, const juce::String& caption, juce::Colour colour)
{
    auto k = std::make_unique<Knob>();

    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 16);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    k->slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setMouseDragSensitivity (200);

    setupLabel (k->label, caption, 11.0f, juce::Justification::centred, true);

    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<APVTS::SliderAttachment> (proc.apvts, id, k->slider);

    knobs[id] = std::move (k);
}

void ChannelStripEditor::addToggle (const juce::String& id, const juce::String& text)
{
    auto t = std::make_unique<Toggle>();

    t->button.setButtonText (text);
    t->button.setClickingTogglesState (true);
    t->button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff45464b));
    t->button.setColour (juce::TextButton::buttonOnColourId, kAmber);
    t->button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb0b3ba));
    t->button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    addAndMakeVisible (t->button);
    t->attachment = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, id, t->button);

    toggles[id] = std::move (t);
}

void ChannelStripEditor::placeKnob (const juce::String& id, juce::Rectangle<int> cell)
{
    auto& k = *knobs.at (id);
    k.label.setBounds (cell.removeFromTop (14));
    k.slider.setBounds (cell);
}

void ChannelStripEditor::placeToggle (const juce::String& id, juce::Rectangle<int> r)
{
    toggles.at (id)->button.setBounds (r.reduced (2, 0));
}

//==============================================================================
void ChannelStripEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (juce::Colour (0xfff0f0f0));
    g.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
    g.drawText ("CONSOLE STRIP", 14, 8, 300, 28, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff8c8f96));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Input  >  Filters  >  Dynamics  >  EQ  >  Output", getWidth() - 400, 8, 386, 28,
                juce::Justification::centredRight);

    for (auto& s : sections)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (s.second.toFloat(), 6.0f);

        g.setColour (kAmber);
        g.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
        g.drawText (s.first, s.second.getX() + 8, s.second.getY() + 4, s.second.getWidth() - 16, 18,
                    juce::Justification::centredLeft);
    }
}

void ChannelStripEditor::resized()
{
    constexpr int kh = 90, bh = 24;

    sections.clear();
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (40);

    auto take = [&] (int w, const juce::String& name)
    {
        auto r = area.removeFromLeft (w);
        area.removeFromLeft (8);
        sections.push_back ({ name, r });
        auto c = r.reduced (6);
        c.removeFromTop (22);
        return c;
    };

    // INPUT
    {
        auto c = take (160, "INPUT");
        peakLabel.setBounds (c.removeFromTop (16));
        rmsLabel.setBounds (c.removeFromTop (16));
        c.removeFromTop (4);
        inMeter.setBounds (c.removeFromLeft (62));
        c.removeFromLeft (4);
        placeKnob ("inTrim", c.removeFromTop (kh));
        c.removeFromTop (6);
        placeToggle ("phase", c.removeFromTop (bh));
        c.removeFromTop (10);
        hintLabel.setBounds (c.removeFromTop (110));
    }

    // FILTERS
    {
        auto c = take (84, "FILTERS");
        placeKnob ("hpfFreq", c.removeFromTop (kh));
        placeToggle ("hpfOn", c.removeFromTop (bh));
        c.removeFromTop (20);
        placeKnob ("lpfFreq", c.removeFromTop (kh));
        placeToggle ("lpfOn", c.removeFromTop (bh));
    }

    // GATE
    {
        auto c = take (84, "GATE");
        placeToggle ("gateOn", c.removeFromBottom (bh));
        placeKnob ("gateThr", c.removeFromTop (kh));
        placeKnob ("gateRange", c.removeFromTop (kh));
        placeKnob ("gateRel", c.removeFromTop (kh));
    }

    // COMPRESSOR
    {
        auto c = take (160, "COMPRESSOR");
        placeToggle ("compOn", c.removeFromBottom (bh));
        auto col0 = c.removeFromLeft (c.getWidth() / 2);
        auto col1 = c;
        placeKnob ("compThr", col0.removeFromTop (kh));
        placeKnob ("compAtk", col0.removeFromTop (kh));
        placeKnob ("compMakeup", col0.removeFromTop (kh));
        placeKnob ("compRatio", col1.removeFromTop (kh));
        placeKnob ("compRel", col1.removeFromTop (kh));
    }

    // EQ
    {
        auto c = take (300, "EQUALIZER");
        placeToggle ("eqOn", c.removeFromBottom (bh));
        const int colW = c.getWidth() / 4;

        auto lfCol = c.removeFromLeft (colW);
        placeKnob ("lfGain", lfCol.removeFromTop (kh));
        placeKnob ("lfFreq", lfCol.removeFromTop (kh));
        placeToggle ("lfBell", lfCol.removeFromTop (bh).translated (0, 8));

        auto lmfCol = c.removeFromLeft (colW);
        placeKnob ("lmfGain", lmfCol.removeFromTop (kh));
        placeKnob ("lmfFreq", lmfCol.removeFromTop (kh));
        placeKnob ("lmfQ", lmfCol.removeFromTop (kh));

        auto hmfCol = c.removeFromLeft (colW);
        placeKnob ("hmfGain", hmfCol.removeFromTop (kh));
        placeKnob ("hmfFreq", hmfCol.removeFromTop (kh));
        placeKnob ("hmfQ", hmfCol.removeFromTop (kh));

        auto hfCol = c;
        placeKnob ("hfGain", hfCol.removeFromTop (kh));
        placeKnob ("hfFreq", hfCol.removeFromTop (kh));
        placeToggle ("hfBell", hfCol.removeFromTop (bh).translated (0, 8));
    }

    // OUTPUT
    {
        auto c = take (area.getWidth(), "OUTPUT");
        placeToggle ("dynPost", c.removeFromBottom (bh));
        c.removeFromBottom (6);

        auto caps = c.removeFromTop (16);
        auto faderCol = c.removeFromLeft (70);
        c.removeFromLeft (6);
        auto grCol = c.removeFromLeft (24);
        c.removeFromLeft (6);
        auto outCol = c;

        faderCaption.setBounds (caps.removeFromLeft (70));
        caps.removeFromLeft (6);
        grCaption.setBounds (caps.removeFromLeft (24));
        caps.removeFromLeft (6);
        outCaption.setBounds (caps);

        outFader.setBounds (faderCol);

        outPeakLabel.setBounds (outCol.removeFromBottom (16));
        grMeter.setBounds (grCol.withTrimmedBottom (16));
        outMeter.setBounds (outCol);
    }
}

//==============================================================================
void ChannelStripEditor::timerCallback()
{
    using juce::Decibels;

    // ---- Input
    const float pkDb  = Decibels::gainToDecibels (proc.inPeak.exchange (0.0f), -100.0f);
    const float rmsDb = Decibels::gainToDecibels (proc.inRms.load(), -100.0f);

    if (pkDb >= inHold) { inHold = pkDb; inHoldFrames = 60; }
    else if (inHoldFrames > 0) --inHoldFrames;
    else inHold = juce::jmax (pkDb, inHold - 0.6f);

    inPeakDisp = juce::jmax (pkDb, inPeakDisp - 1.0f);
    inRmsDisp += 0.5f * (rmsDb - inRmsDisp);
    inMeter.setValues (inPeakDisp, inRmsDisp);

    peakLabel.setText ("PEAK  " + (inHold > -99.0f ? juce::String (inHold, 1) : juce::String ("--")) + " dB",
                       juce::dontSendNotification);
    peakLabel.setColour (juce::Label::textColourId, inHold >= -0.5f ? juce::Colour (0xffff3b30) : kText);

    rmsLabel.setText ("RMS  " + (inRmsDisp > -99.0f ? juce::String (inRmsDisp, 1) : juce::String ("--")) + " dB",
                      juce::dontSendNotification);
    const bool onTarget = inRmsDisp > -21.0f && inRmsDisp < -15.0f;
    rmsLabel.setColour (juce::Label::textColourId, onTarget ? juce::Colour (0xff5ccf5c) : kText);

    // ---- Output
    const float oPk  = Decibels::gainToDecibels (proc.outPeak.exchange (0.0f), -100.0f);
    const float oRms = Decibels::gainToDecibels (proc.outRms.load(), -100.0f);

    if (oPk >= outHold) { outHold = oPk; outHoldFrames = 60; }
    else if (outHoldFrames > 0) --outHoldFrames;
    else outHold = juce::jmax (oPk, outHold - 0.6f);

    outPeakDisp = juce::jmax (oPk, outPeakDisp - 1.0f);
    outRmsDisp += 0.5f * (oRms - outRmsDisp);
    outMeter.setValues (outPeakDisp, outRmsDisp);

    outPeakLabel.setText ((outHold > -99.0f ? juce::String (outHold, 1) : juce::String ("--")) + " dB",
                          juce::dontSendNotification);
    outPeakLabel.setColour (juce::Label::textColourId, outHold >= -0.5f ? juce::Colour (0xffff3b30) : kText);

    // ---- Gain reduction
    const float gr = proc.gainReduction.exchange (0.0f);
    grDisp = juce::jmin (gr, grDisp + 0.8f);
    grMeter.setValues (0.0f, grDisp);
}
