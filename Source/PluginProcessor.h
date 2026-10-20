#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include "ClipperDSP.h"
#include "BandSplitter.h"

class ChromaClipperProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int histSize = 8192;            // power of 2: ~16 s of history at 2 ms per column
    static constexpr double histChunkSec = 0.002;

    ChromaClipperProcessor();
    ~ChromaClipperProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chroma Clipper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- presets (message thread only) ----
    // "Init" is built in: every parameter at its default. User presets are small XML files in
    // getPresetDirectory(). The scope zoom is a view setting, so presets don't store it.
    static inline const juce::String initPresetName { "Init" };
    static juce::File getPresetDirectory();
    static juce::String sanitisePresetName (const juce::String& raw);
    juce::StringArray getUserPresetNames() const;          // sorted, without "Init"
    bool loadPreset (const juce::String& name);            // "Init" restores the defaults
    bool savePreset (const juce::String& name);            // overwrites a preset of the same name
    bool deletePreset (const juce::String& name);
    void resetAllParameters();                             // everything back to default, scope zoom included
    juce::String currentPreset { initPresetName };

    // scrolling history (channel 0): max/min envelope per 2 ms chunk of the signal after input gain (In) and the clipped signal (Out)
    std::array<float, histSize> histInMax {}, histInMin {}, histOutMax {}, histOutMin {};
    std::atomic<int> histWrite { 0 };            // free-running chunk counter; slot = counter & (histSize - 1)
    std::atomic<int> histChunkSamples { 88 };
    std::atomic<bool> scopeActive { false };   // scope buffers are only written while the editor is open
    std::atomic<double> sampleRateAtomic { 44100.0 };

    // metering (peak since the editor last read it): after input gain, clipped band (in / out of the clipper), final output
    std::atomic<float> meterIn { 0.f }, meterBand { 0.f }, meterClip { 0.f }, meterOut { 0.f };
    int editorWidth = 400;                       // remembered editor size (message thread only)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override { updateLatency(); }
    void updateLatency();                           // message thread: report latency when the oversampling factor changed
    void applyDefaults (bool includeZoom);

    std::atomic<float> *pIn = nullptr, *pCeil = nullptr, *pOut = nullptr, *pHard = nullptr, *pOver = nullptr, *pHpf = nullptr, *pLpf = nullptr, *pMatch = nullptr;
    chroma::BandSplitter split;                      // HP / LP = boundaries of the band the clipper works on
    chroma::DelayLine restDelay[2];                  // aligns the untouched spectrum with the oversampler latency
    chroma::DelayLine bandDelay[2];                  // same delay for the clip-band input, so scope / GR compare In and Out at the same instant
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inSm, outSm;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> ceilSm;   // ceiling as linear gain (smoothed: no zipper noise)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hardSm;           // Soft-to-Hard blend 0..1 (smoothed: no zipper noise / click)
    std::unique_ptr<chroma::Oversampler> os[2][3];   // [channel][0: 2x, 1: 4x, 2: 8x]
    int lastIdx = -1;
    int reportedLatency = -1;
    // gain match: slow, stereo-linked gain (one value for both channels) so there is no phase or image change
    float pwIn = 0.f, pwClip = 0.f, matchGain = 1.f, matchTarget = 1.f, pwAlpha = 0.f, gAlpha = 0.f;
    int chunkSamples = 88, accCount = 0;
    float aInMax = 0.f, aInMin = 0.f, aOutMax = 0.f, aOutMin = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaClipperProcessor)
};
