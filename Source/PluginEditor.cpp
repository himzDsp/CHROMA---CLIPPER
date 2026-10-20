#include "PluginEditor.h"
using namespace colours;

namespace
{
    juce::Font fnt (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // layout in design units (the editor is designed at 400 x 452 and scaled)
    const juce::Rectangle<float> scopeR  { 12.f, 54.f, 376.f, 150.f };
    const juce::Rectangle<float> plateR  { 12.f, 236.f, 376.f, 184.f };
    const juce::Rectangle<float> inMetR  { 20.f, 256.f, 7.f, 70.f };
    const juce::Rectangle<float> outMetR { 373.f, 256.f, 7.f, 70.f };
    const float knobCx[3] = { 75.f, 200.f, 325.f };
    const float knobCy = 270.f, knobSize = 54.f;
    constexpr int meterSegs = 15;

    juce::ColourGradient vgrad (juce::Colour a, float y0, juce::Colour b, float y1)
    {
        return juce::ColourGradient (a, 0.f, y0, b, 0.f, y1, false);
    }
}

// ---------------- Knob ----------------
bool Knob::tick()
{
    bool moving = false;
    auto ease = [&moving] (float& v, float target, float k)
    {
        const float d = target - v;
        if (std::abs (d) < 0.0005f) { if (v != target) { v = target; moving = true; } }
        else                        { v += d * k; moving = true; }
    };
    ease (shown, (float) valueToProportionOfLength (getValue()), 0.35f);
    ease (hover, hoverT ? 1.f : 0.f, 0.2f);
    ease (drag,  dragT  ? 1.f : 0.f, 0.3f);
    return moving;
}

// ---------------- Look and feel ----------------
ChromaLookAndFeel::ChromaLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, white);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff111214));
    setColour (juce::PopupMenu::textColourId, textMid);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a2c31));
    setColour (juce::PopupMenu::highlightedTextColourId, accent);
}

void ChromaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float posIn,
                                          float a0, float a1, juce::Slider& s)
{
    auto* k = dynamic_cast<Knob*> (&s);
    const float pos = k != nullptr ? k->shown : posIn;
    const float act = k != nullptr ? juce::jmax (k->hover * 0.55f, k->drag) : 0.f;
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = a0 + pos * (a1 - a0);

    // --- value arc ---
    const float rt = r * 0.86f, th = r * 0.15f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, rt, rt, 0.f, a0, a1, true);
    g.setColour (juce::Colour (0xff050506));
    g.strokePath (track, juce::PathStrokeType (th * 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff2b2d32));
    g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float from = bipolar ? a0 + 0.5f * (a1 - a0) : a0;
    const float lo = juce::jmin (from, angle), hi = juce::jmax (from, angle);
    if (hi - lo > 0.002f)
    {
        juce::Path v;
        v.addCentredArc (c.x, c.y, rt, rt, 0.f, lo, hi, true);
        for (int i = 3; i >= 1; --i)                               // cheap glow: wide, faint strokes underneath
        {
            g.setColour (accent.withAlpha ((0.05f + 0.12f * act) * (float) (4 - i) / 3.f));
            g.strokePath (v, juce::PathStrokeType (th + r * 0.09f * (float) i, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (accent.interpolatedWith (accentHi, act * 0.6f));
        g.strokePath (v, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- cap ---
    const float rc = r * 0.66f;
    for (int i = 4; i >= 1; --i)                                   // soft drop shadow
    {
        const float e = (float) i * r * 0.035f;
        g.setColour (juce::Colours::black.withAlpha (0.14f));
        g.fillEllipse (c.x - rc - e, c.y - rc - e + r * 0.07f, 2.f * (rc + e), 2.f * (rc + e));
    }
    g.setGradientFill (vgrad (juce::Colour (0xff4c4e55), c.y - rc, juce::Colour (0xff0d0d0f), c.y + rc));
    g.fillEllipse (c.x - rc, c.y - rc, 2.f * rc, 2.f * rc);       // bezel
    const float rf = rc * 0.90f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c3e45), c.x - rf * 0.3f, c.y - rf * 0.4f,
                                             juce::Colour (0xff16171a), c.x + rf * 0.9f, c.y + rf * 0.9f, true));
    g.fillEllipse (c.x - rf, c.y - rf, 2.f * rf, 2.f * rf);       // face
    g.setGradientFill (vgrad (white.withAlpha (0.15f), c.y - rf * 0.85f, white.withAlpha (0.f), c.y - rf * 0.05f));
    g.fillEllipse (c.x - rf * 0.66f, c.y - rf * 0.85f, rf * 1.32f, rf * 0.85f);   // specular sheen

    // --- pointer ---
    const float sn = std::sin (angle), cs = -std::cos (angle);
    juce::Path ptr;
    ptr.startNewSubPath (c.x + sn * rf * 0.28f, c.y + cs * rf * 0.28f);
    ptr.lineTo          (c.x + sn * rf * 0.82f, c.y + cs * rf * 0.82f);
    g.setColour (accent.withAlpha (0.18f + 0.25f * act));
    g.strokePath (ptr, juce::PathStrokeType (r * 0.20f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.interpolatedWith (white, 0.35f + 0.3f * act));
    g.strokePath (ptr, juce::PathStrokeType (r * 0.085f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ChromaLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                          float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f, tH = 3.5f * uiScale;
    const bool hot_ = s.isMouseOverOrDragging();
    const juce::Rectangle<float> groove ((float) x, cy - tH * 0.5f, (float) w, tH);
    g.setColour (juce::Colour (0xff050506));
    g.fillRoundedRectangle (groove.expanded (0.f, 1.f * uiScale), tH);
    g.setColour (juce::Colour (0xff2b2d32));
    g.fillRoundedRectangle (groove, tH * 0.5f);
    g.setColour (accent.withAlpha (hot_ ? 1.f : 0.85f));
    g.fillRoundedRectangle (groove.withRight (sliderPos), tH * 0.5f);

    const float r = (hot_ ? 6.f : 5.f) * uiScale;
    g.setColour (accent.withAlpha (hot_ ? 0.30f : 0.15f));
    g.fillEllipse (sliderPos - r * 1.7f, cy - r * 1.7f, r * 3.4f, r * 3.4f);
    g.setGradientFill (juce::ColourGradient (accentHi, sliderPos - r * 0.3f, cy - r * 0.4f, accent.darker (0.3f), sliderPos + r, cy + r, true));
    g.fillEllipse (sliderPos - r, cy - r, 2.f * r, 2.f * r);
}

void ChromaLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    const juce::Rectangle<float> b (0.f, 0.f, (float) w, (float) h);
    const float rad = 5.f * uiScale;
    const bool hotb = box.isMouseOver (true) || down;
    g.setGradientFill (vgrad (juce::Colour (0xff09090b), 0.f, juce::Colour (0xff18191c), (float) h));
    g.fillRoundedRectangle (b, rad);
    g.setColour (hotb ? accent.withAlpha (0.85f) : hair);
    g.drawRoundedRectangle (b.reduced (0.5f), rad, 1.f);
    g.setColour (white.withAlpha (0.05f));
    g.drawHorizontalLine (1, rad, (float) w - rad);

    const float cx = (float) w - 15.f * uiScale, cy = (float) h * 0.5f, sz = 3.6f * uiScale;
    juce::Path chev;
    chev.startNewSubPath (cx - sz, cy - sz * 0.45f);
    chev.lineTo (cx, cy + sz * 0.55f);
    chev.lineTo (cx + sz, cy - sz * 0.45f);
    g.setColour (accent);
    g.strokePath (chev, juce::PathStrokeType (1.7f * uiScale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ChromaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat();
    const float rad = 5.f * uiScale;
    const bool on = b.getToggleState();
    g.setGradientFill (vgrad (juce::Colour (0xff09090b), 0.f, juce::Colour (0xff18191c), r.getHeight()));
    g.fillRoundedRectangle (r, rad);
    if (on) { g.setColour (accent.withAlpha (0.22f));  g.fillRoundedRectangle (r, rad); }
    g.setColour (down || on ? accent : (highlighted ? accent.withAlpha (0.85f) : hair));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.f);
}

void ChromaLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff111214));
    g.setColour (hair);
    g.drawRect (0, 0, w, h, 1);
}

// ---------------- Meter state ----------------
void ChromaClipperEditor::Meter::update (float lin)
{
    const float db = juce::Decibels::gainToDecibels (lin, -100.f);
    lv = juce::jmax (db, lv - 1.1f);                 // instant attack, ~66 dB/s release
    if (db >= pk)         { pk = db; hold = 40; }
    else if (hold > 0)    { --hold; }
    else                  { pk = juce::jmax (lv, pk - 0.5f); }
}

// ---------------- Editor ----------------
ChromaClipperEditor::ChromaClipperEditor (ChromaClipperProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    constrainer.setSizeLimits (300, 339, 800, 904);
    constrainer.setFixedAspectRatio (400.0 / 452.0);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const int w0 = juce::jlimit (300, 800, proc.editorWidth);
    setSize (w0, (int) std::round ((double) w0 * 452.0 / 400.0));

    auto setupKnob = [this] (Knob& k, bool bipolar)
    {
        k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
        k.setDoubleClickReturnValue (true, 0.0);
        k.getProperties().set ("bipolar", bipolar);
        k.setLookAndFeel (&lnf);
        addAndMakeVisible (k);
    };
    setupKnob (inputKnob, true);
    setupKnob (ceilingKnob, false);
    setupKnob (outputKnob, true);

    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoomSlider.setLookAndFeel (&lnf);
    addAndMakeVisible (zoomSlider);

    for (auto* f : { &hpSlider, &lpSlider })
    {
        f->setSliderStyle (juce::Slider::LinearHorizontal);
        f->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        f->setLookAndFeel (&lnf);
        f->onValueChange = [this] { repaintDesign ({ 10.f, 376.f, 380.f, 44.f }); };
        addAndMakeVisible (*f);
    }
    hpSlider.setDoubleClickReturnValue (true, 20.0);
    lpSlider.setDoubleClickReturnValue (true, 20000.0);

    hardSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    hardSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    hardSlider.setDoubleClickReturnValue (true, 0.0);          // back to Soft
    hardSlider.setLookAndFeel (&lnf);
    hardSlider.onValueChange = [this] { repaintDesign ({ 100.f, 328.f, 100.f, 18.f }); };
    addAndMakeVisible (hardSlider);

    oversampleBox.addItemList (juce::StringArray { "2x", "4x", "8x" }, 1);
    oversampleBox.setLookAndFeel (&lnf);
    addAndMakeVisible (oversampleBox);

    presetBox.setLookAndFeel (&lnf);
    addAndMakeVisible (presetBox);
    for (auto* b : { &saveButton, &deleteButton, &resetButton, &matchButton })
    {
        b->setLookAndFeel (&lnf);
        addAndMakeVisible (*b);
    }
    saveButton.setColour   (juce::TextButton::textColourOffId, textMid);
    deleteButton.setColour (juce::TextButton::textColourOffId, hot);
    resetButton.setColour  (juce::TextButton::textColourOffId, textMid);
    matchButton.setClickingTogglesState (true);
    matchButton.setColour (juce::TextButton::textColourOffId, textMid);
    matchButton.setColour (juce::TextButton::textColourOnId, accent);
    refreshPresetList();
    presetBox.onChange = [this]
    {
        if (! proc.loadPreset (presetBox.getText()))
            refreshPresetList();                       // the file is gone: fall back to the list as it is on disk
    };
    saveButton.onClick   = [this] { promptSavePreset(); };
    deleteButton.onClick = [this] { promptDeletePreset(); };
    resetButton.onClick  = [this] { proc.resetAllParameters(); refreshPresetList(); };
    footerText = "Himz Audio Copyright 2024";

    aMatch = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "match", matchButton);
    aIn   = std::make_unique<SA> (p.apvts, "input",   inputKnob);
    aCeil = std::make_unique<SA> (p.apvts, "ceiling", ceilingKnob);
    aOut  = std::make_unique<SA> (p.apvts, "output",  outputKnob);
    aZoom = std::make_unique<SA> (p.apvts, "zoom",    zoomSlider);
    aOver = std::make_unique<CA> (p.apvts, "oversample", oversampleBox);
    aHp   = std::make_unique<SA> (p.apvts, "hpf",     hpSlider);
    aLp   = std::make_unique<SA> (p.apvts, "lpf",     lpSlider);
    aHard = std::make_unique<SA> (p.apvts, "hardness", hardSlider);

    for (auto* k : { &inputKnob, &ceilingKnob, &outputKnob })
    {
        k->shown = (float) k->valueToProportionOfLength (k->getValue());
        k->onValueChange = [this] { repaintDesign ({ 10.f, 296.f, 380.f, 32.f }); };
    }

    proc.scopeActive = true;
    startTimerHz (60);
}

ChromaClipperEditor::~ChromaClipperEditor()
{
    stopTimer();
    proc.scopeActive = false;
    for (auto* c : std::initializer_list<juce::Component*> { &inputKnob, &ceilingKnob, &outputKnob, &zoomSlider, &hpSlider, &lpSlider, &hardSlider, &oversampleBox,
                                                                     &presetBox, &saveButton, &deleteButton, &resetButton, &matchButton })
        c->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void ChromaClipperEditor::repaintDesign (juce::Rectangle<float> r)
{
    repaint ((r * S()).expanded (2.f).getSmallestIntegerContainer());
}

void ChromaClipperEditor::resized()
{
    const float s = S();
    lnf.uiScale = s;
    auto B = [s] (float x, float y, float w, float h)
    {
        return juce::Rectangle<int> ((int) std::round (x * s), (int) std::round (y * s),
                                     (int) std::round (w * s), (int) std::round (h * s));
    };
    inputKnob.setBounds   (B (knobCx[0] - knobSize * 0.5f, knobCy - knobSize * 0.5f, knobSize, knobSize));
    ceilingKnob.setBounds (B (knobCx[1] - knobSize * 0.5f, knobCy - knobSize * 0.5f, knobSize, knobSize));
    outputKnob.setBounds  (B (knobCx[2] - knobSize * 0.5f, knobCy - knobSize * 0.5f, knobSize, knobSize));
    zoomSlider.setBounds  (B (266.f, 210.f, 72.f, 18.f));
    hardSlider.setBounds  (B (24.f, 344.f, 172.f, 16.f));
    oversampleBox.setBounds (B (206.f, 346.f, 170.f, 26.f));
    presetBox.setBounds   (B (206.f, 10.f, 88.f, 24.f));
    saveButton.setBounds  (B (298.f, 10.f, 40.f, 24.f));
    deleteButton.setBounds (B (342.f, 10.f, 44.f, 24.f));
    resetButton.setBounds (B (324.f, 431.f, 64.f, 18.f));
    matchButton.setBounds (B (231.f, 259.f, 62.f, 24.f));
    hpSlider.setBounds    (B (24.f, 396.f, 112.f, 20.f));
    lpSlider.setBounds    (B (206.f, 396.f, 112.f, 20.f));

    bg = juce::Image();                       // static layer is rebuilt on the next paint
    proc.editorWidth = getWidth();
}

void ChromaClipperEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addItem (ChromaClipperProcessor::initPresetName, 1);
    const auto names = proc.getUserPresetNames();
    int sel = 1;
    for (int i = 0; i < names.size(); ++i)
    {
        presetBox.addItem (names[i], i + 2);
        if (names[i] == proc.currentPreset) sel = i + 2;
    }
    presetBox.setSelectedId (sel, juce::dontSendNotification);
}

void ChromaClipperEditor::promptSavePreset()
{
    auto* w = new juce::AlertWindow ("Save preset", "Enter a name for this preset.", juce::MessageBoxIconType::NoIcon, this);
    const auto initial = proc.currentPreset == ChromaClipperProcessor::initPresetName ? juce::String() : proc.currentPreset;
    w->addTextEditor ("name", initial, "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ChromaClipperEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result == 1 && safe != nullptr)
            safe->commitSave (w->getTextEditorContents ("name"));
    }), true);
}

void ChromaClipperEditor::commitSave (const juce::String& rawName)
{
    const auto name = ChromaClipperProcessor::sanitisePresetName (rawName);
    if (name.isEmpty() || name.equalsIgnoreCase (ChromaClipperProcessor::initPresetName))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Save preset",
                                                "Enter a preset name. \"Init\" is reserved for the built-in default.", "OK", this);
        return;
    }
    if (! proc.savePreset (name))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save preset",
                                                "The preset file couldn't be written.", "OK", this);
    refreshPresetList();
}

void ChromaClipperEditor::promptDeletePreset()
{
    const auto name = proc.currentPreset;
    if (name == ChromaClipperProcessor::initPresetName)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Delete preset",
                                                "The built-in Init preset can't be deleted.", "OK", this);
        return;
    }
    auto* w = new juce::AlertWindow ("Delete preset", "Delete \"" + name + "\"? This can't be undone.",
                                     juce::MessageBoxIconType::QuestionIcon, this);
    w->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ChromaClipperEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, name] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        safe->proc.deletePreset (name);
        safe->proc.loadPreset (ChromaClipperProcessor::initPresetName);   // back to the Init defaults
        safe->refreshPresetList();
    }), true);
}

float ChromaClipperEditor::zoomSec() const
{
    return 0.375f * std::pow (40.f, (float) zoomSlider.getValue() / 100.f);   // 0.375 s .. 15 s of history across the display (1.5x slower scroll)
}

void ChromaClipperEditor::advanceScope()
{
    const int raw = proc.histWrite.load();
    if (! scopeInit)
    {
        endAbs = (juce::int64) (unsigned) raw;  lastEndRaw = raw;
        shownEnd = (double) endAbs;  scopeInit = true;
        return;
    }
    endAbs += (int) ((unsigned) raw - (unsigned) lastEndRaw);
    lastEndRaw = raw;

    // The audio thread delivers history in block-sized steps. Instead of jumping to each new step, the displayed
    // position glides toward it (it never overtakes it, so only finished chunks are ever drawn).
    const double gap = (double) endAbs - shownEnd;
    if (gap < 0.0 || gap > 2000.0)  shownEnd = (double) endAbs;          // transport stopped / restarted: resync
    else if (gap < 0.02)            shownEnd = (double) endAbs;
    else                            shownEnd += gap * 0.10;
}

void ChromaClipperEditor::timerCallback()
{
    advanceScope();
    for (auto* k : { &inputKnob, &ceilingKnob, &outputKnob })
        if (k->tick()) k->repaint();

    // meters + gain reduction
    const float in = proc.meterIn.exchange (0.f), band = proc.meterBand.exchange (0.f), clip = proc.meterClip.exchange (0.f), out = proc.meterOut.exchange (0.f);
    mIn.update (in);
    mOut.update (out);
    const float gr = (clip > 1e-5f && band > clip * 1.001f) ? juce::Decibels::gainToDecibels (band / clip) : 0.f;   // reduction of the band the clipper works on
    grDb = gr > grDb ? gr : grDb * 0.9f;
    if (grDb < 0.05f) grDb = 0.f;

    repaintDesign ({ 10.f, 52.f, 380.f, 180.f });     // scope + legend row
    repaintDesign (inMetR.expanded (2.f));
    repaintDesign (outMetR.expanded (2.f));
}

// ---------------- Static layer ----------------
void ChromaClipperEditor::rebuildBackground (float ps)
{
    bg = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) std::ceil ((float) getWidth() * ps)),
                      juce::jmax (1, (int) std::ceil ((float) getHeight() * ps)), true);
    juce::Graphics ig (bg);
    ig.addTransform (juce::AffineTransform::scale (ps * S()));
    drawStatic (ig);
    bgScale = ps;  bgW = getWidth();  bgH = getHeight();
}

void ChromaClipperEditor::drawStatic (juce::Graphics& g)
{
    // body
    g.setGradientFill (vgrad (bgTop, 0.f, bgBot, 452.f));
    g.fillAll();

    // header
    g.setGradientFill (vgrad (headTop, 0.f, headBot, 44.f));
    g.fillRect (0.f, 0.f, 400.f, 44.f);
    g.setColour (white.withAlpha (0.07f));  g.fillRect (0.f, 0.f, 400.f, 1.f);
    g.setColour (edge);                     g.fillRect (0.f, 44.f, 400.f, 1.f);
    g.setColour (hair.withAlpha (0.8f));    g.fillRect (0.f, 45.f, 400.f, 1.f);
    {
        juce::ColourGradient gl (accent.withAlpha (0.f), 0.f, 0.f, accent.withAlpha (0.f), 400.f, 0.f, false);
        gl.addColour (0.5, accent.withAlpha (0.45f));
        g.setGradientFill (gl);
        g.fillRect (0.f, 44.f, 400.f, 1.f);
    }

    // logo: a sine wave flattened at the ceiling
    {
        const float lk = 16.f / 26.f;                                   // logo is as tall as the 16 pt title font
        const juce::Rectangle<float> r (14.f, 14.f, 16.f, 16.f);
        juce::Path rr;  rr.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 6.f * lk);
        juce::DropShadow (accent.withAlpha (0.35f), 5, { 0, 0 }).drawForPath (g, rr);
        g.setGradientFill (vgrad (accent.withAlpha (0.30f), r.getY(), accent.withAlpha (0.08f), r.getBottom()));
        g.fillPath (rr);
        g.setGradientFill (vgrad (accentHi, r.getY(), accent.darker (0.4f), r.getBottom()));
        g.strokePath (rr, juce::PathStrokeType (1.f));
        const float cy = r.getCentreY(), lim = 6.f * lk, x0 = r.getX() + 4.f * lk, w = r.getWidth() - 8.f * lk;
        g.setColour (accent.withAlpha (0.45f));
        g.fillRect (x0, cy - lim - 0.5f, w, 1.f);
        g.fillRect (x0, cy + lim - 0.5f, w, 1.f);
        juce::Path p;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = (float) i / 40.f;
            const float y = juce::jlimit (-lim, lim, -std::sin (t * juce::MathConstants<float>::twoPi) * lim * 1.9f);
            if (i == 0) p.startNewSubPath (x0, cy + y); else p.lineTo (x0 + t * w, cy + y);
        }
        g.setColour (white.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.4f * lk, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (white);
        g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // title (with a soft drop shadow)
    for (int pass = 0; pass < 2; ++pass)
    {
        juce::AttributedString as;
        as.setJustification (juce::Justification::centredLeft);
        as.setWordWrap (juce::AttributedString::none);
        as.append ("CHROMA ", fnt (16.f, true), pass == 0 ? juce::Colours::black.withAlpha (0.7f) : white);
        as.append ("CLIPPER", fnt (16.f, true), pass == 0 ? juce::Colours::black.withAlpha (0.7f) : accent);
        as.draw (g, juce::Rectangle<float> (38.f, pass == 0 ? 1.5f : 0.f, 152.f, 44.f));
    }

    // scope well
    {
        const auto r = scopeR;
        juce::Path outer;  outer.addRoundedRectangle (r.getX() - 2.f, r.getY() - 2.f, r.getWidth() + 4.f, r.getHeight() + 4.f, 8.f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 10, { 0, 3 }).drawForPath (g, outer);
        g.setGradientFill (vgrad (juce::Colour (0xff35373d), r.getY(), juce::Colour (0xff0c0d0f), r.getBottom()));
        g.fillPath (outer);
        g.setGradientFill (vgrad (juce::Colour (0xff040405), r.getY(), juce::Colour (0xff0e0f12), r.getBottom()));
        g.fillRoundedRectangle (r, 6.f);

        const float cy = r.getCentreY(), yScale = (r.getHeight() * 0.5f) / 1.25f;
        g.setColour (white.withAlpha (0.045f));
        for (int k = 1; k <= 2; ++k)
            for (int s = -1; s <= 1; s += 2)
                g.fillRect (r.getX(), cy - (float) s * 0.5f * (float) k * yScale - 0.25f, r.getWidth(), 0.5f);
        for (int i = 1; i < 8; ++i)
            g.fillRect (r.getX() + r.getWidth() * (float) i / 8.f - 0.25f, r.getY(), 0.5f, r.getHeight());
        g.setColour (white.withAlpha (0.10f));
        g.fillRect (r.getX(), cy - 0.5f, r.getWidth(), 1.f);
    }

    // legend
    g.setFont (fnt (10.5f));
    g.setColour (white.withAlpha (0.8f));  g.fillRoundedRectangle (14.f, 217.f, 10.f, 3.f, 1.5f);
    g.setColour (textDim);                 g.drawText ("Pre-clip", 30, 210, 56, 18, juce::Justification::centredLeft);
    g.setColour (accent);                  g.fillRoundedRectangle (88.f, 217.f, 10.f, 3.f, 1.5f);
    g.setColour (textDim);                 g.drawText ("Clipped", 104, 210, 50, 18, juce::Justification::centredLeft);
    g.setFont (fnt (10.5f, true));
    g.drawText ("GR", 156, 210, 16, 18, juce::Justification::centredLeft);
    g.drawText ("Zoom", 236, 210, 30, 18, juce::Justification::centredLeft);

    // controls plate
    {
        juce::Path pp;  pp.addRoundedRectangle (plateR.getX(), plateR.getY(), plateR.getWidth(), plateR.getHeight(), 10.f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 12, { 0, 4 }).drawForPath (g, pp);
        g.setGradientFill (vgrad (plateTop, plateR.getY(), plateBot, plateR.getBottom()));
        g.fillPath (pp);
        g.setColour (white.withAlpha (0.06f));
        g.fillRect (plateR.getX() + 10.f, plateR.getY() + 1.f, plateR.getWidth() - 20.f, 1.f);   // top edge highlight
        g.setColour (edge);
        g.strokePath (pp, juce::PathStrokeType (1.f));
    }

    // knob wells, ticks and names
    const char* names[3] = { "Input", "Ceiling", "Output" };
    for (int i = 0; i < 3; ++i)
    {
        const float cx = knobCx[i], cy = knobCy, wr = 29.f;
        g.setColour (white.withAlpha (0.05f));
        g.fillEllipse (cx - wr, cy - wr + 1.2f, 2.f * wr, 2.f * wr);                    // lower rim light
        g.setGradientFill (vgrad (juce::Colour (0xff050506), cy - wr, juce::Colour (0xff202226), cy + wr));
        g.fillEllipse (cx - wr, cy - wr, 2.f * wr, 2.f * wr);                            // recessed well

        const float a0 = juce::degreesToRadians (225.f), a1 = juce::degreesToRadians (495.f);
        for (int t = 0; t <= 10; ++t)
        {
            const float a = a0 + (a1 - a0) * (float) t / 10.f;
            const bool major = (t == 0 || t == 10 || t == 5);
            const float r0 = 30.5f, r1 = major ? 33.2f : 32.2f;
            g.setColour (white.withAlpha (major ? 0.32f : 0.14f));
            g.drawLine (cx + std::sin (a) * r0, cy - std::cos (a) * r0, cx + std::sin (a) * r1, cy - std::cos (a) * r1, major ? 1.2f : 0.9f);
        }
        g.setColour (textMid);  g.setFont (fnt (11.f, true));
        g.drawText (names[i], (int) cx - 40, 299, 80, 14, juce::Justification::centred);
    }

    // meter wells
    for (auto m : { inMetR, outMetR })
    {
        const auto w = m.expanded (2.f);
        g.setColour (white.withAlpha (0.05f));  g.fillRoundedRectangle (w.translated (0.f, 1.f), 3.f);
        g.setColour (well);                     g.fillRoundedRectangle (w, 3.f);
    }
    g.setColour (textOff);  g.setFont (fnt (9.f, true));
    g.drawText ("IN",  (int) inMetR.getX() - 8,  243, 23, 11, juce::Justification::centred);
    g.drawText ("OUT", (int) outMetR.getX() - 8, 243, 23, 11, juce::Justification::centred);

    g.setColour (textDim);  g.setFont (fnt (11.f, true));
    g.drawText ("Soft to Hard",  24, 330, 100, 14, juce::Justification::centredLeft);
    g.drawText ("Oversampling",  206, 330, 170, 14, juce::Justification::centredLeft);
    g.drawText ("High-pass",      24, 380, 172, 14, juce::Justification::centredLeft);
    g.drawText ("Low-pass",      206, 380, 170, 14, juce::Justification::centredLeft);

    // Soft to Hard: end labels and a mark at the 1:1 midpoint (the slider itself is a live child component)
    g.setColour (white.withAlpha (0.28f));
    g.fillRect (24.f + 86.f - 0.5f, 347.f, 1.f, 10.f);
    g.setColour (textOff);  g.setFont (fnt (9.f, true));
    g.drawText ("Soft", 24,  360, 60, 11, juce::Justification::centredLeft);
    g.drawText ("Hard", 136, 360, 60, 11, juce::Justification::centredRight);

    // footer
    g.setGradientFill (vgrad (juce::Colour (0xff16171a), 428.f, juce::Colour (0xff0f1012), 452.f));
    g.fillRect (0.f, 428.f, 400.f, 24.f);
    g.setColour (edge);               g.fillRect (0.f, 428.f, 400.f, 1.f);
    g.setColour (hair.withAlpha (0.7f)); g.fillRect (0.f, 429.f, 400.f, 1.f);

    // frame
    g.setColour (juce::Colours::black.withAlpha (0.8f));  g.drawRect (0.f, 0.f, 400.f, 452.f, 1.f);
    g.setColour (white.withAlpha (0.05f));                g.drawRect (1.f, 1.f, 398.f, 450.f, 1.f);
}

// ---------------- Live layer ----------------
void ChromaClipperEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, const Meter& m)
{
    const float segH = r.getHeight() / (float) meterSegs;
    const int pkSeg = juce::jlimit (-1, meterSegs - 1, (int) std::floor ((m.pk + 42.f) / 3.f));
    for (int i = 0; i < meterSegs; ++i)
    {
        const float thr = -42.f + 3.f * (float) i;                       // -42 dB ... 0 dB (top segment lights above 0)
        const juce::Rectangle<float> s (r.getX(), r.getBottom() - (float) (i + 1) * segH + 0.7f, r.getWidth(), segH - 1.4f);
        const juce::Colour on = i >= 14 ? hot : (i >= 12 ? amber : accent.interpolatedWith (accentHi, (float) i / 14.f * 0.4f));
        const bool lit = m.lv > thr, peak = (i == pkSeg);
        if (lit)
        {
            g.setColour (on.withAlpha (0.20f));
            g.fillRoundedRectangle (s.expanded (1.5f), 2.f);
        }
        g.setColour (lit ? on : (peak ? on.withAlpha (0.9f) : juce::Colour (0xff1b1c1f)));
        g.fillRoundedRectangle (s, 1.f);
    }
}

void ChromaClipperEditor::drawScopeLive (juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::Path clipP;
    clipP.addRoundedRectangle (r.getX() + 1.f, r.getY() + 1.f, r.getWidth() - 2.f, r.getHeight() - 2.f, 5.f);
    g.saveState();
    g.reduceClipRegion (clipP, juce::AffineTransform());

    const float cy = r.getCentreY(), yScale = (r.getHeight() * 0.5f) / 1.25f;
    const float ceilG = juce::Decibels::decibelsToGain ((float) ceilingKnob.getValue());
    const float yTop = cy - ceilG * yScale, yBot = cy + ceilG * yScale;

    // "above the ceiling" zones
    g.setGradientFill (vgrad (hot.withAlpha (0.14f), r.getY(), hot.withAlpha (0.f), yTop));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), juce::jmax (0.f, yTop - r.getY()));
    g.setGradientFill (vgrad (hot.withAlpha (0.f), yBot, hot.withAlpha (0.14f), r.getBottom()));
    g.fillRect (r.getX(), yBot, r.getWidth(), juce::jmax (0.f, r.getBottom() - yBot));

    // ceiling guides
    const float dash[] = { 4.f, 4.f };
    for (float y : { yTop, yBot })
    {
        g.setColour (accent.withAlpha (0.10f));
        g.fillRect (r.getX(), y - 1.5f, r.getWidth(), 3.f);
        g.setColour (accent.withAlpha (0.55f));
        g.drawDashedLine (juce::Line<float> (r.getX(), y, r.getRight(), y), dash, 2, 1.f);
    }

    // Where the clip curve starts to bend for the CURRENT Soft-to-Hard position and ceiling: the input level at which the
    // output is 0.1 dB below the input. Found on the real transfer curve (bisection), so it moves exactly with the slider
    // (about -1.9 dB under the ceiling at Soft, sitting on the ceiling at Hard). The zone between it and the ceiling is the knee.
    {
        const chroma::ClipParams cpv = chroma::makeClipParams (ceilG, (float) hardSlider.getValue() * 0.01f);
        float lo = 1.0e-3f * ceilG, hi = 4.f * ceilG;
        for (int it = 0; it < 28; ++it)
        {
            const float mid = 0.5f * (lo + hi);
            const float dev = 20.f * std::log10 (juce::jmax (1.0e-9f, chroma::clip (mid, cpv) / mid));
            if (dev > -0.1f) lo = mid; else hi = mid;
        }
        const float onset = juce::jmin (lo, ceilG);
        if (onset < ceilG * 0.995f)
        {
            const float yOT = cy - onset * yScale, yOB = cy + onset * yScale;
            g.setColour (accent.withAlpha (0.07f));
            g.fillRect (r.getX(), yTop, r.getWidth(), juce::jmax (0.f, yOT - yTop));                 // knee zone, top
            g.fillRect (r.getX(), yOB, r.getWidth(), juce::jmax (0.f, yBot - yOB));                  // knee zone, bottom
            const float dots[] = { 1.5f, 3.f };
            g.setColour (white.withAlpha (0.30f));
            for (float y : { yOT, yOB })
                g.drawDashedLine (juce::Line<float> (r.getX(), y, r.getRight(), y), dots, 2, 1.f);
        }
    }

    // scrolling history: newest audio enters at the right edge and moves left.
    // Chunks are grouped into bins of a fixed (integer) number of chunks, and bins are anchored to the absolute chunk
    // counter, so a bin always holds the same audio while it scrolls (no shimmering of the envelope). The scroll
    // position is fractional, so it moves by sub-pixel amounts every frame.
    const int size = ChromaClipperProcessor::histSize, mask = size - 1;
    const double sr = juce::jmax (1.0, proc.sampleRateAtomic.load());
    const double chunkSec = (double) proc.histChunkSamples.load() / sr;
    const int nChunks = juce::jlimit (16, size - 400, (int) std::lround (zoomSec() / chunkSec));
    const int targetCols = juce::jlimit (32, 1200, (int) (r.getWidth() * S() * 1.5f));
    const int cpc  = juce::jmax (1, nChunks / targetCols);                  // chunks per bin
    const int cols = juce::jmax (8, nChunks / cpc);                         // bins across the display
    const int nDraw = cols + 2;                                             // one extra on each side, hidden by the clip
    const double endBins = shownEnd / (double) cpc;
    const juce::int64 bEnd = (juce::int64) std::floor (endBins);            // bin holding the newest chunks (may be partial)
    const juce::int64 endChunk = (juce::int64) std::floor (shownEnd);       // chunks below this are finished
    const float dx = r.getWidth() / (float) cols;

    yInT.assign ((size_t) nDraw, 0.f);  yInB.assign ((size_t) nDraw, 0.f);
    yOutT.assign ((size_t) nDraw, 0.f); yOutB.assign ((size_t) nDraw, 0.f);
    xs.assign ((size_t) nDraw, 0.f);

    auto Y = [&] (float v) { return juce::jlimit (r.getY(), r.getBottom(), cy - v * yScale); };
    for (int c = 0; c < nDraw; ++c)
    {
        const juce::int64 bin = bEnd - (juce::int64) (nDraw - 1 - c);
        const juce::int64 k0 = bin * cpc, k1 = std::min (k0 + cpc, endChunk);
        float iMax = 0.f, iMin = 0.f, oMax = 0.f, oMin = 0.f;
        if (k1 > k0)
        {
            for (juce::int64 k = k0; k < k1; ++k)
            {
                const size_t slot = (size_t) (k & (juce::int64) mask);
                iMax = juce::jmax (iMax, proc.histInMax[slot]);   iMin = juce::jmin (iMin, proc.histInMin[slot]);
                oMax = juce::jmax (oMax, proc.histOutMax[slot]);  oMin = juce::jmin (oMin, proc.histOutMin[slot]);
            }
            yInT[(size_t) c] = Y (iMax);  yInB[(size_t) c] = Y (iMin);
            yOutT[(size_t) c] = Y (oMax); yOutB[(size_t) c] = Y (oMin);
        }
        else if (c > 0)                                                      // newest bin still empty: hold the previous one
        {
            yInT[(size_t) c] = yInT[(size_t) c - 1];   yInB[(size_t) c] = yInB[(size_t) c - 1];
            yOutT[(size_t) c] = yOutT[(size_t) c - 1]; yOutB[(size_t) c] = yOutB[(size_t) c - 1];
        }
        else { yInT[0] = yInB[0] = yOutT[0] = yOutB[0] = cy; }
        xs[(size_t) c] = r.getRight() - (float) (endBins - (double) bin - 0.5) * dx;
    }

    // light smoothing across neighbouring bins ([1 2 1] twice) so the outlines are smooth curves, not stair-steps
    auto smooth = [nDraw] (std::vector<float>& v)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            float prev = v[0];
            for (int c = 0; c < nDraw; ++c)
            {
                const float cur = v[(size_t) c], next = v[(size_t) std::min (c + 1, nDraw - 1)];
                v[(size_t) c] = 0.25f * prev + 0.5f * cur + 0.25f * next;
                prev = cur;
            }
        }
    };
    smooth (yInT);  smooth (yInB);  smooth (yOutT);  smooth (yOutB);

    const int ncol = nDraw;
    auto X = [&] (int c) { return xs[(size_t) c]; };

    // envelope shape of one signal: top edge left->right, bottom edge right->left
    auto shape = [&] (const std::vector<float>& top, const std::vector<float>& bot)
    {
        juce::Path p;
        p.startNewSubPath (X (0), top[0]);
        for (int c = 1; c < ncol; ++c) p.lineTo (X (c), top[(size_t) c]);
        for (int c = ncol - 1; c >= 0; --c) p.lineTo (X (c), bot[(size_t) c]);
        p.closeSubPath();
        return p;
    };
    // band between two edges (what the clipper removed)
    auto band = [&] (const std::vector<float>& a, const std::vector<float>& b)
    {
        juce::Path p;
        p.startNewSubPath (X (0), a[0]);
        for (int c = 1; c < ncol; ++c) p.lineTo (X (c), a[(size_t) c]);
        for (int c = ncol - 1; c >= 0; --c) p.lineTo (X (c), b[(size_t) c]);
        p.closeSubPath();
        return p;
    };
    auto edge = [&] (const std::vector<float>& v)
    {
        juce::Path p;
        p.startNewSubPath (X (0), v[0]);
        for (int c = 1; c < ncol; ++c) p.lineTo (X (c), v[(size_t) c]);
        return p;
    };

    // 1) original signal (grey body)
    g.setColour (white.withAlpha (0.13f));
    g.fillPath (shape (yInT, yInB));
    g.setColour (white.withAlpha (0.45f));
    g.strokePath (edge (yInT), juce::PathStrokeType (1.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.strokePath (edge (yInB), juce::PathStrokeType (1.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 2) clipped signal (amber body) on top
    g.setColour (accent.withAlpha (0.30f));
    g.fillPath (shape (yOutT, yOutB));

    // 3) exactly what the clipper shaved off, top and bottom (red)
    g.setColour (hot.withAlpha (0.65f));
    g.fillPath (band (yInT, yOutT));
    g.fillPath (band (yOutB, yInB));

    // 4) clipped outline with glow
    for (auto* e : { &yOutT, &yOutB })
    {
        const auto p = edge (*e);
        g.setColour (accent.withAlpha (0.12f));
        g.strokePath (p, juce::PathStrokeType (5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent.interpolatedWith (accentHi, 0.25f));
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // glass: top sheen and inner shadow
    g.setGradientFill (vgrad (white.withAlpha (0.055f), r.getY(), white.withAlpha (0.f), r.getY() + r.getHeight() * 0.45f));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), r.getHeight() * 0.5f);
    g.setGradientFill (vgrad (juce::Colours::black.withAlpha (0.55f), r.getY(), juce::Colours::black.withAlpha (0.f), r.getY() + 12.f));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 12.f);
    g.restoreState();
}

void ChromaClipperEditor::paint (juce::Graphics& g)
{
    const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! bg.isValid() || bgW != getWidth() || bgH != getHeight() || std::abs (bgScale - ps) > 0.01f)
        rebuildBackground (ps);

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (bg, juce::AffineTransform::scale (1.f / ps));

    g.addTransform (juce::AffineTransform::scale (S()));          // everything below is in design units

    drawScopeLive (g, scopeR);

    // gain-reduction LED + readout, zoom value
    const float grAmt = juce::jlimit (0.f, 1.f, grDb / 3.f);
    g.setColour (hot.withAlpha (0.08f + 0.30f * grAmt));
    g.fillEllipse (170.f, 215.f, 10.f, 10.f);
    g.setColour (juce::Colour (0xff2a1612).interpolatedWith (hot, grAmt));
    g.fillEllipse (172.f, 217.f, 6.f, 6.f);
    g.setFont (fnt (10.5f, true));
    g.setColour (grDb > 0.f ? hot.interpolatedWith (textMid, 0.35f) : textOff);
    g.drawText ((grDb > 0.f ? "-" : "") + juce::String (grDb, 1) + " dB", 184, 210, 48, 18, juce::Justification::centredLeft);

    g.setColour (accent);  g.setFont (fnt (10.f));
    const float sec = zoomSec();
    g.drawText (sec < 1.f ? juce::String ((int) std::round (sec * 1000.f)) + " ms" : juce::String (sec, 1) + " s",
                340, 210, 48, 18, juce::Justification::centredRight);

    // knob values
    const Knob* knobs[3] = { &inputKnob, &ceilingKnob, &outputKnob };
    for (int i = 0; i < 3; ++i)
    {
        const double v = knobs[i]->getValue();
        const juce::String txt = (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB";
        g.setColour (accent.interpolatedWith (white, knobs[i]->drag * 0.5f));
        g.setFont (fnt (10.5f, true));
        g.drawText (txt, (int) knobCx[i] - 40, 313, 80, 13, juce::Justification::centred);
    }

    // Soft to Hard readout
    {
        const double hv = hardSlider.getValue();
        const juce::String txt = hv < 0.05 ? juce::String ("Soft") : (hv > 99.95 ? juce::String ("Hard")
                               : juce::String ((int) std::round (hv)) + "%" + (std::abs (hv - 50.0) < 0.05 ? juce::String (" (1:1)") : juce::String()));
        g.setColour (hv > 0.05 ? accent : textOff);
        g.setFont (fnt (10.5f, true));
        g.drawText (txt, 100, 330, 96, 14, juce::Justification::centredRight);
    }

    // filter readouts
    auto hz = [] (double v) { return v < 995.0 ? juce::String ((int) std::round (v)) + " Hz" : juce::String (v / 1000.0, v < 9995.0 ? 2 : 1) + " kHz"; };
    const double hpV = hpSlider.getValue(), lpV = lpSlider.getValue();
    g.setFont (fnt (10.5f, true));
    g.setColour (hpV > 20.5 ? accent : textOff);
    g.drawText (hpV > 20.5 ? hz (hpV) : juce::String ("Off"), 142, 396, 54, 20, juce::Justification::centredLeft);
    g.setColour (lpV < 19990.0 ? accent : textOff);
    g.drawText (lpV < 19990.0 ? hz (lpV) : juce::String ("Off"), 324, 396, 52, 20, juce::Justification::centredLeft);

    drawMeter (g, inMetR, mIn);
    drawMeter (g, outMetR, mOut);

    g.setColour (textOff);  g.setFont (fnt (10.f));
    g.drawText (footerText, 0, 428, 400, 24, juce::Justification::centred);
}
