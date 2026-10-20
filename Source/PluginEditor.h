#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace colours
{
    static const juce::Colour bgTop    { 0xff1e1f23 }, bgBot    { 0xff111214 },
                              headTop  { 0xff25272c }, headBot  { 0xff15161a },
                              well     { 0xff08080a }, plateTop { 0xff1f2024 }, plateBot { 0xff141518 },
                              hair     { 0xff34363c }, edge     { 0xff0a0a0c },
                              accent   { 0xfff1c40f }, accentHi { 0xffffe27a },
                              hot      { 0xffff5a36 }, amber    { 0xffff9f1c },
                              white    { 0xffffffff },
                              textDim  { 0xff8a8d94 }, textMid  { 0xffb6b9c0 },
                              textOff  { 0xff62656b }, btnOff   { 0xff7d8088 };
}

// Rotary knob with animation state (advanced by the editor's 60 Hz timer).
class Knob : public juce::Slider
{
public:
    void mouseEnter (const juce::MouseEvent& e) override { hoverT = true;  juce::Slider::mouseEnter (e); }
    void mouseExit  (const juce::MouseEvent& e) override { hoverT = false; juce::Slider::mouseExit  (e); }
    void mouseDown  (const juce::MouseEvent& e) override { dragT  = true;  juce::Slider::mouseDown  (e); }
    void mouseUp    (const juce::MouseEvent& e) override { dragT  = false; juce::Slider::mouseUp    (e); }

    // returns true while something is still moving (=> needs a repaint)
    bool tick();

    float shown = 0.f, hover = 0.f, drag = 0.f;   // displayed position (eased), hover glow, drag glow
private:
    bool hoverT = false, dragT = false;
};

class ChromaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChromaLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                           float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh,
                       juce::ComboBox&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (9.5f * uiScale, juce::Font::bold)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.f * uiScale)); }
    juce::Font getPopupMenuFont() override                { return juce::Font (juce::FontOptions (12.f * uiScale)); }

    float uiScale = 1.f;   // editor size / 400
};

class ChromaClipperEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ChromaClipperEditor (ChromaClipperProcessor&);
    ~ChromaClipperEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    struct Meter
    {
        float lv = -100.f, pk = -100.f;
        int hold = 0;
        void update (float lin);
    };

    void timerCallback() override;
    float S() const noexcept { return (float) getWidth() / 400.f; }
    void repaintDesign (juce::Rectangle<float> r);
    void rebuildBackground (float pixelScale);
    void drawStatic (juce::Graphics&);
    void drawScopeLive (juce::Graphics&, juce::Rectangle<float>);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, const Meter&);
    float zoomSec() const;
    void refreshPresetList();
    void promptSavePreset();
    void commitSave (const juce::String& rawName);
    void promptDeletePreset();

    ChromaClipperProcessor& proc;
    ChromaLookAndFeel lnf;
    Knob inputKnob, ceilingKnob, outputKnob;
    juce::Slider zoomSlider, hpSlider, lpSlider;
    juce::Slider hardSlider;                          // "Soft to Hard": 0 % = Soft, 50 % = 1:1 blend, 100 % = Hard
    juce::ComboBox oversampleBox;
    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save" }, deleteButton { "Delete" }, resetButton { "Reset" }, matchButton { "Gain Match" };
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> aIn, aCeil, aOut, aZoom, aHp, aLp, aHard;
    std::unique_ptr<CA> aOver;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aMatch;

    // cached static layer (rebuilt on resize / DPI change)
    juce::Image bg;
    float bgScale = 0.f;
    int bgW = 0, bgH = 0;

    juce::ComponentBoundsConstrainer constrainer;   // fixed aspect ratio + size limits

    Meter mIn, mOut;
    float grDb = 0.f;
    juce::String footerText;
    std::vector<float> yInT, yInB, yOutT, yOutB, xs;
    // smooth scope scrolling: the displayed end position chases the audio write position
    double shownEnd = 0.0;
    juce::int64 endAbs = 0;
    int lastEndRaw = 0;
    bool scopeInit = false;
    void advanceScope();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaClipperEditor)
};
