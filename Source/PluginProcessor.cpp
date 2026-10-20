#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout ChromaClipperProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input",
           NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Ceiling",
           NormalisableRange<float> (-24.f, 0.f, 0.1f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
           NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));
    // 0 % = Soft (default), 50 % = Soft and Hard blended 1:1, 100 % = Hard
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "hardness", 1 }, "Soft to Hard",
           NormalisableRange<float> (0.f, 100.f, 0.1f), 0.f, AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "oversample", 1 }, "Oversampling",
           StringArray { "2x", "4x", "8x" }, 0));
    {
        NormalisableRange<float> hp (20.f, 1000.f, 1.f), lp (1000.f, 20000.f, 1.f);
        hp.setSkewForCentre (150.f);  lp.setSkewForCentre (6000.f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "hpf", 1 }, "High-pass",
               hp, 20.f, AudioParameterFloatAttributes().withLabel ("Hz")));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lpf", 1 }, "Low-pass",
               lp, 20000.f, AudioParameterFloatAttributes().withLabel ("Hz")));
    }
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "match", 1 }, "Gain Match", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "zoom", 1 }, "Scope Zoom",
           NormalisableRange<float> (0.f, 100.f, 1.f), 67.f));
    return l;
}

ChromaClipperProcessor::ChromaClipperProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pIn   = apvts.getRawParameterValue ("input");
    pCeil = apvts.getRawParameterValue ("ceiling");
    pOut  = apvts.getRawParameterValue ("output");
    pHard = apvts.getRawParameterValue ("hardness");
    pOver = apvts.getRawParameterValue ("oversample");
    pHpf  = apvts.getRawParameterValue ("hpf");
    pLpf  = apvts.getRawParameterValue ("lpf");
    pMatch = apvts.getRawParameterValue ("match");
    startTimerHz (30);
}

namespace
{
    const char* const presetParamIds[] = { "input", "ceiling", "output", "hardness", "oversample", "hpf", "lpf", "match" };

    void setParamValue (juce::AudioProcessorValueTreeState& s, const char* id, float v)
    {
        if (auto* p = s.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    }
}

juce::File ChromaClipperProcessor::getPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("Himz Audio").getChildFile ("Chroma Clipper").getChildFile ("Presets");
}

juce::String ChromaClipperProcessor::sanitisePresetName (const juce::String& raw)
{
    return raw.removeCharacters ("\\/:*?\"<>|").trim().substring (0, 48).trim();
}

juce::StringArray ChromaClipperProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : getPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.ccpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

void ChromaClipperProcessor::applyDefaults (bool includeZoom)
{
    for (auto* id : presetParamIds)
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    if (includeZoom)
        if (auto* z = apvts.getParameter ("zoom"))
        {
            z->beginChangeGesture();
            z->setValueNotifyingHost (z->getDefaultValue());
            z->endChangeGesture();
        }
}

void ChromaClipperProcessor::resetAllParameters()
{
    applyDefaults (true);
    currentPreset = initPresetName;
}

bool ChromaClipperProcessor::loadPreset (const juce::String& name)
{
    if (name.equalsIgnoreCase (initPresetName))
    {
        applyDefaults (false);
        currentPreset = initPresetName;
        return true;
    }

    const auto file = getPresetDirectory().getChildFile (name + ".ccpreset");
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("ChromaClipperPreset"))
        return false;

    applyDefaults (false);                                  // anything missing from the file stays at its default
    for (auto* id : presetParamIds)
        if (xml->hasAttribute (id))
            setParamValue (apvts, id, (float) xml->getDoubleAttribute (id));
    if (! xml->hasAttribute ("hardness") && xml->hasAttribute ("mode"))      // preset from before the slider: Soft -> 0 %, Hard -> 100 %
        setParamValue (apvts, "hardness", xml->getDoubleAttribute ("mode") > 0.5 ? 100.f : 0.f);
    currentPreset = name;
    return true;
}

bool ChromaClipperProcessor::savePreset (const juce::String& rawName)
{
    const auto name = sanitisePresetName (rawName);
    if (name.isEmpty() || name.equalsIgnoreCase (initPresetName))
        return false;

    const auto dir = getPresetDirectory();
    if (! dir.createDirectory().wasOk())
        return false;

    juce::XmlElement xml ("ChromaClipperPreset");
    xml.setAttribute ("version", 1);
    for (auto* id : presetParamIds)
        if (auto* v = apvts.getRawParameterValue (id))
            xml.setAttribute (id, (double) v->load());

    if (! xml.writeTo (dir.getChildFile (name + ".ccpreset")))
        return false;
    currentPreset = name;
    return true;
}

bool ChromaClipperProcessor::deletePreset (const juce::String& name)
{
    if (name.isEmpty() || name.equalsIgnoreCase (initPresetName))
        return false;
    return getPresetDirectory().getChildFile (name + ".ccpreset").deleteFile();
}

void ChromaClipperProcessor::updateLatency()
{
    const int i = juce::jlimit (0, 2, (int) std::lround (pOver->load()));
    if (os[0][i] == nullptr) return;
    const int lat = os[0][i]->getLatency();
    if (lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
    }
}

bool ChromaClipperProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo())
           && o == l.getMainInputChannelSet();
}

void ChromaClipperProcessor::prepareToPlay (double sr, int)
{
    sampleRateAtomic = sr;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 3; ++i)
            os[c][i] = std::make_unique<chroma::Oversampler> (2 << i, sr);
    inSm.reset (sr, 0.01);  inSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    outSm.reset (sr, 0.01); outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    ceilSm.reset (sr, 0.02); ceilSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pCeil->load()));
    hardSm.reset (sr, 0.02); hardSm.setCurrentAndTargetValue (pHard->load() * 0.01f);
    lastIdx = -1;
    pwAlpha = (float) (1.0 - std::exp (-1.0 / (sr * 0.4)));    // ~400 ms loudness averaging
    gAlpha  = (float) (1.0 - std::exp (-1.0 / (sr * 0.05)));   // ~50 ms gain smoothing
    pwIn = pwClip = 0.f;  matchGain = matchTarget = 1.f;
    split.prepare (sr);
    split.setCutoffs (pHpf->load(), pLpf->load(), true);
    for (auto& dl : restDelay) dl.prepare (4096);
    for (auto& dl : bandDelay) dl.prepare (4096);
    {   // report the latency right here, not only on the first processed block, so the host compensates from the start
        const int i0 = juce::jlimit (0, 2, (int) std::lround (pOver->load()));
        reportedLatency = os[0][i0]->getLatency();
        setLatencySamples (reportedLatency);
    }
    chunkSamples = juce::jmax (1, (int) std::lround (sr * histChunkSec));
    histChunkSamples = chunkSamples;
    accCount = 0;  aInMax = aInMin = aOutMax = aOutMin = 0.f;
}

void ChromaClipperProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nCh = juce::jmin (buffer.getNumChannels(), 2);
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);

    const int idx = juce::jlimit (0, 2, (int) std::lround (pOver->load()));   // 0: 2x, 1: 4x, 2: 8x
    const bool matchOn = pMatch->load() > 0.5f;
    if (idx != lastIdx)
    {
        for (int c = 0; c < 2; ++c)
            if (os[c][idx] != nullptr) os[c][idx]->reset();
        lastIdx = idx;                                      // latency is reported from the message-thread timer (updateLatency)
    }

    ceilSm.setTargetValue (juce::Decibels::decibelsToGain (pCeil->load()));
    hardSm.setTargetValue (pHard->load() * 0.01f);
    chroma::ClipParams cp = chroma::makeClipParams (ceilSm.getCurrentValue(), hardSm.getCurrentValue());
    inSm.setTargetValue  (juce::Decibels::decibelsToGain (pIn->load()));
    outSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    split.setCutoffs (pHpf->load(), pLpf->load());
    const int restLat = os[0][idx] != nullptr ? os[0][idx]->getLatency() : 0;

    float* d[2] = { nullptr, nullptr };
    for (int c = 0; c < nCh; ++c) d[c] = buffer.getWritePointer (c);

    const int mask = histSize - 1;
    int w = histWrite.load();
    const bool wantScope = scopeActive.load (std::memory_order_relaxed);
    float mIn = 0.f, mBand = 0.f, mClip = 0.f, mOut = 0.f;

    for (int i = 0; i < n; ++i)
    {
        const float gi = inSm.isSmoothing() ? inSm.getNextValue() : inSm.getTargetValue(),
                    go = outSm.isSmoothing() ? outSm.getNextValue() : outSm.getTargetValue();
        const float gm = matchGain;
        if (ceilSm.isSmoothing() || hardSm.isSmoothing())       // ceiling / Soft-to-Hard change: glide, never step
            cp = chroma::makeClipParams (ceilSm.getNextValue(), hardSm.getNextValue());
        split.tick();
        float inSq = 0.f, clipSq = 0.f;
        for (int c = 0; c < nCh; ++c)
        {
            inSq += d[c][i] * d[c][i];                     // original input, before any processing
            float xin = d[c][i] * gi;
            if (! (std::fabs (xin) < 1.0e6f)) xin = 0.f;   // NaN / Inf from a misbehaving host must not enter the IIR filters
            float band, rest;
            split.process (c, xin, band, rest);            // band: what the clipper may touch; rest: passes through untouched
            const float y    = os[c][idx]->process (band, cp);   // oversample -> ADAA clip -> filter + decimate (band only)
            const float outS = y + restDelay[c].process (rest, restLat);   // rest is delayed to line up with the clipped band
            const float bandA = bandDelay[c].process (band, restLat);   // band delayed like the clipper output: In and Out line up in time
            if (c == 0 && wantScope)
            {
                aInMax  = juce::jmax (aInMax,  bandA); aInMin  = juce::jmin (aInMin,  bandA);
                aOutMax = juce::jmax (aOutMax, y);     aOutMin = juce::jmin (aOutMin, y);
            }
            clipSq += outS * outS;
            d[c][i] = outS * go * gm;
            mIn   = juce::jmax (mIn,   std::abs (xin));
            mBand = juce::jmax (mBand, std::abs (bandA));
            mClip = juce::jmax (mClip, std::abs (y));
            mOut  = juce::jmax (mOut,  std::abs (d[c][i]));
        }
        if (! (inSq < 1.0e12f))   inSq = 0.f;                    // NaN / Inf / absurd input must not poison the IIR averagers
        if (! (clipSq < 1.0e12f)) clipSq = 0.f;
        if (nCh > 0)
        {
            const float inv = 1.f / (float) nCh;
            pwIn   += pwAlpha * (inSq   * inv - pwIn);
            pwClip += pwAlpha * (clipSq * inv - pwClip);
            if (! matchOn)                                  matchTarget = 1.f;
            else if (pwIn > 1e-9f && pwClip > 1e-9f)        matchTarget = juce::jlimit (0.0631f, 15.85f, std::sqrt (pwIn / pwClip));   // else: silence, hold
            matchGain += gAlpha * (matchTarget - matchGain);
        }
        if (wantScope && ++accCount >= chunkSamples)
        {
            histInMax[(size_t) (w & mask)] = aInMax;   histInMin[(size_t) (w & mask)] = aInMin;
            histOutMax[(size_t) (w & mask)] = aOutMax; histOutMin[(size_t) (w & mask)] = aOutMin;
            w = (int) ((unsigned) w + 1u);                  // free-running chunk counter (wraps harmlessly), slot = counter & mask
            accCount = 0;  aInMax = aInMin = aOutMax = aOutMin = 0.f;
        }
    }
    if (wantScope)
    {
        histWrite.store (w);
        meterIn.store   (juce::jmax (meterIn.load(),   mIn),   std::memory_order_relaxed);
        meterBand.store (juce::jmax (meterBand.load(), mBand), std::memory_order_relaxed);
        meterClip.store (juce::jmax (meterClip.load(), mClip), std::memory_order_relaxed);
        meterOut.store  (juce::jmax (meterOut.load(),  mOut),  std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* ChromaClipperProcessor::createEditor() { return new ChromaClipperEditor (*this); }

void ChromaClipperProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("csVersion", 3, nullptr);
    state.setProperty ("uiWidth", editorWidth, nullptr);
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void ChromaClipperProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            editorWidth = juce::jlimit (300, 800, (int) state.getProperty ("uiWidth", 400));
            currentPreset = state.getProperty ("preset", initPresetName).toString();
            if (currentPreset.isEmpty()) currentPreset = initPresetName;
            if (! state.hasProperty ("csVersion"))   // pre-2.0 session: the oversampling list had a leading "1x (Off)"
                for (auto param : state)
                    if (param.getProperty ("id").toString() == "oversample")
                        param.setProperty ("value", juce::jmax (0.f, (float) param.getProperty ("value") - 1.f), nullptr);
            // pre-slider session: the Soft/Hard choice "mode" becomes "hardness" (Soft = 0 %, Hard = 100 %)
            if (auto oldMode = state.getChildWithProperty ("id", "mode"); oldMode.isValid())
            {
                if (! state.getChildWithProperty ("id", "hardness").isValid())
                {
                    juce::ValueTree p ("PARAM");
                    p.setProperty ("id", "hardness", nullptr);
                    p.setProperty ("value", (float) oldMode.getProperty ("value") > 0.5f ? 100.f : 0.f, nullptr);
                    state.appendChild (p, nullptr);
                }
                state.removeChild (oldMode, nullptr);
            }
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChromaClipperProcessor(); }
