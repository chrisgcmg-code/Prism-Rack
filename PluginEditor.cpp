#include "PluginEditor.h"

using namespace prism;
using namespace prism::ui;

//==============================================================================
// ModuleStrip
//==============================================================================
ModuleStrip::ModuleStrip (PrismRackProcessor& p, int moduleId, LookAndFeel& lf)
    : module (moduleId), proc (p), laf (lf), led (lf),
      typeAtt (*p.apvts.getParameter (pid (moduleId, "type")), [this] (float v) { currentType = (int) v; refresh(); }),
      bypassAtt (*p.apvts.getParameter (pid (moduleId, "bypass")), [this] (float v) { bypassed = v > 0.5f; refresh(); })
{
    auto& s = p.apvts;
    if (module == Texture)
    {
        top   = std::make_unique<Knob> (s, ids::mix, "MIX", laf);
        small = std::make_unique<Knob> (s, ids::output, "OUTPUT", laf);
    }
    else
    {
        top   = std::make_unique<Knob> (s, pid (module, "knobB"), knobBName (module), laf);
        small = std::make_unique<Knob> (s, pid (module, "sec"), secName (module), laf);
    }
    amount = std::make_unique<Knob> (s, pid (module, "amount"), "AMOUNT", laf);
    small->labelSize = 8.5f;
    for (auto* k : { top.get(), amount.get(), small.get() }) addAndMakeVisible (k);

    const auto names = effectNames (module);
    for (int i = 0; i < 5; ++i)
    {
        buttons[(size_t) i] = std::make_unique<EffectButton> (module, i, names[i], laf);
        buttons[(size_t) i]->onClick = [this, i] { typeAtt.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (*buttons[(size_t) i]);
    }
    left.setTooltip ("Move module earlier in the chain");
    right.setTooltip ("Move module later in the chain");
    left.onClick  = [this] { if (onMove) onMove (module, -1); };
    right.onClick = [this] { if (onMove) onMove (module, +1); };
    addAndMakeVisible (left); addAndMakeVisible (right);
    led.setTooltip ("Module on / off");
    led.onClick = [this] { bypassAtt.setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f); };
    addAndMakeVisible (led);
    typeAtt.sendInitialUpdate();
    bypassAtt.sendInitialUpdate();
}

void ModuleStrip::refresh()
{
    const auto col = effectColour (module, currentType);
    for (int i = 0; i < 5; ++i) buttons[(size_t) i]->setSelected (i == currentType);
    if (amount) amount->setRingColour (bypassed ? laf.theme.ledOff.darker (0.2f) : col);
    led.set (! bypassed, col);
    if (currentType != paintedType || bypassed != paintedBypass || laf.theme.panel != paintedPanel)
    {
        paintedType = currentType; paintedBypass = bypassed; paintedPanel = laf.theme.panel;
        repaint();
    }
}

void ModuleStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (laf.theme.text);
    g.setFont (uiFont (13.0f, true).withExtraKerningFactor (0.18f));
    g.drawText (moduleName (module), r.withHeight (22).withY (8), juce::Justification::centred);
    g.setColour (bypassed ? laf.theme.subtext : effectColour (module, currentType).darker (laf.theme.panel.getBrightness() > 0.5f ? 0.25f : 0.0f));
    g.setFont (uiFont (12.0f, true));
    g.drawText (effectNames (module)[currentType].toUpperCase() + (bypassed ? "  (OFF)" : ""), r.withHeight (20).withY (36), juce::Justification::centred);
}

void ModuleStrip::resized()
{
    left.setBounds (8, 8, 26, 22);
    right.setBounds (getWidth() - 34, 8, 26, 22);
    led.setBounds (14, 36, 20, 20);
    top->setBounds (getWidth() / 2 - 45, 64, 90, 106);
    amount->setBounds (getWidth() / 2 - 62, 176, 124, 140);
    small->setBounds (getWidth() - 62, 150, 54, 70);
    const float bw = (float) (getWidth() - 10) / 5.0f;
    for (int i = 0; i < 5; ++i)
        buttons[(size_t) i]->setBounds (5 + (int) (bw * (float) i), 326, (int) bw, 44);
}

//==============================================================================
// Panel
//==============================================================================
Panel::Panel (PrismRackProcessor& p, LookAndFeel& lf)
    : proc (p), laf (lf),
      orderAtt (*p.apvts.getParameter (ids::order), [this] (float) { resized(); repaint(); }),
      bypassAtt (*p.apvts.getParameter (ids::bypass), [this] (float) { refresh(); }),
      bpmAtt (*p.apvts.getParameter (ids::manualBpm), [] (float) {}),
      syncAtt (*p.apvts.getParameter (ids::syncMode), [] (float) {}),
      headroomSetAtt (*p.apvts.getParameter (ids::headroom), [] (float) {})
{
    auto& s = p.apvts;
    for (int m = 0; m < NumModules; ++m)
    {
        strips[(size_t) m] = std::make_unique<ModuleStrip> (p, m, laf);
        strips[(size_t) m]->onMove = [this] (int id, int dir) { moveModule (id, dir); };
        addAndMakeVisible (*strips[(size_t) m]);

        volKnobs[(size_t) m] = std::make_unique<Knob> (s, pid (m, "vol"), "FX VOL", laf);
        volKnobs[(size_t) m]->labelSize = 8.5f;
        addChildComponent (*volKnobs[(size_t) m]);
        alwaysOn[(size_t) m] = std::make_unique<juce::TextButton> ("ALWAYS ON");
        alwaysOn[(size_t) m]->setClickingTogglesState (true);
        alwaysOn[(size_t) m]->setTooltip ("Stays on when the main Bypass is engaged");
        alwaysOnAtt[(size_t) m] = std::make_unique<APVTS::ButtonAttachment> (s, pid (m, "alwaysOn"), *alwaysOn[(size_t) m]);
        addChildComponent (*alwaysOn[(size_t) m]);
    }

    headroom.addItemList (s.getParameter (ids::headroom)->getAllValueStrings(), 1);
    headroom.setTooltip ("Input headroom. Low for quiet sources (ukulele pickup), Very High for hot synths");
    headroomAtt = std::make_unique<APVTS::ComboBoxAttachment> (s, ids::headroom, headroom);
    addAndMakeVisible (headroom);

    autoBtn.setTooltip ("Play for 5 seconds and the headroom is set for you");
    autoBtn.onClick = [this]
    {
        if (autoActive) { autoActive = false; autoBtn.setButtonText ("AUTO"); return; }
        proc.autoDone = false; proc.autoProgress = 0; proc.autoRequested = true; autoActive = true;
    };
    addAndMakeVisible (autoBtn);

    setupBtn.setClickingTogglesState (true);
    setupBtn.setTooltip ("Show setup: effect volumes, always-on, filter mode, tempo");
    setupBtn.onClick = [this] { expanded = setupBtn.getToggleState(); resized(); repaint(); if (onExpandChanged) onExpandChanged(); };
    addAndMakeVisible (setupBtn);

    darkBtn.setClickingTogglesState (true);
    darkBtn.setToggleState ((bool) proc.apvts.state.getProperty ("darkMode", false), juce::dontSendNotification);
    darkBtn.onClick = [this] { proc.apvts.state.setProperty ("darkMode", darkBtn.getToggleState(), nullptr); };
    addAndMakeVisible (darkBtn);

    bypassSw.onPress = [this] { bypassAtt.setValueAsCompleteGesture (proc.apvts.getRawParameterValue (ids::bypass)->load() > 0.5f ? 0.0f : 1.0f); };
    tapSw.onPress = [this] { tap(); };
    addAndMakeVisible (bypassSw); addAndMakeVisible (tapSw);

    // setup view
    colorKnob      = std::make_unique<Knob> (s, pid (Texture, "knobB"), "COLOR", laf);
    revSpeedKnob   = std::make_unique<Knob> (s, ids::revSpeed, "REV SPEED", laf);
    driftMacroKnob = std::make_unique<Knob> (s, ids::driftMacro, "DRIFT MACRO", laf);
    for (auto* k : { colorKnob.get(), revSpeedKnob.get(), driftMacroKnob.get() }) { k->labelSize = 8.5f; addChildComponent (k); }
    filterMode.addItemList (s.getParameter (ids::filterMode)->getAllValueStrings(), 1);
    filterModeAtt = std::make_unique<APVTS::ComboBoxAttachment> (s, ids::filterMode, filterMode);
    filterMode.setTooltip ("Texture > Filter mode");
    syncMode.addItemList (s.getParameter (ids::syncMode)->getAllValueStrings(), 1);
    syncModeAtt = std::make_unique<APVTS::ComboBoxAttachment> (s, ids::syncMode, syncMode);
    syncMode.setTooltip ("Free: knobs set time in ms/Hz. Host: follow Live's tempo. Tap: use the TAP footswitch");
    trails.setClickingTogglesState (true);
    trails.setTooltip ("Diffusion tails keep ringing after bypass");
    trailsAtt = std::make_unique<APVTS::ButtonAttachment> (s, ids::trails, trails);
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    bpmSliderAtt = std::make_unique<APVTS::SliderAttachment> (s, ids::manualBpm, bpmSlider);
    for (juce::Component* c : { (juce::Component*) &filterMode, (juce::Component*) &syncMode, (juce::Component*) &trails, (juce::Component*) &bpmSlider })
        addChildComponent (c);

    orderAtt.sendInitialUpdate();
    buildTexture();
}

void Panel::buildTexture()
{
    textureDark = laf.theme.panel.getBrightness() < 0.5f;
    texture = juce::Image (juce::Image::ARGB, W, H + ExpandH, true);
    juce::Graphics g (texture);
    juce::Random rnd (1977);
    for (int y = 0; y < texture.getHeight(); ++y)
    {
        const float a = rnd.nextFloat() * 0.035f;
        g.setColour ((textureDark ? juce::Colours::white : juce::Colours::black).withAlpha (a));
        g.drawHorizontalLine (y, 0.0f, (float) W);
    }
}

void Panel::paint (juce::Graphics& g)
{
    const auto& t = laf.theme;
    if (textureDark != (t.panel.getBrightness() < 0.5f)) buildTexture();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, 14.0f);
    g.saveState();
    juce::Path clip; clip.addRoundedRectangle (r, 14.0f); g.reduceClipRegion (clip);
    g.drawImageAt (texture, 0, 0);
    g.restoreState();
    g.setColour (t.panelEdge);
    g.drawRoundedRectangle (r, 14.0f, 2.0f);

    // wordmark with a small prism
    juce::Path prism; prism.addTriangle (26, 40, 38, 16, 50, 40);
    juce::ColourGradient pg (effectColour (0, 0), 26, 40, effectColour (2, 2), 50, 16, false);
    pg.addColour (0.5, effectColour (1, 0));
    g.setGradientFill (pg); g.fillPath (prism);
    g.setColour (t.panelEdge); g.strokePath (prism, juce::PathStrokeType (1.2f));
    g.setColour (t.text);
    g.setFont (uiFont (20.0f, true).withExtraKerningFactor (0.25f));
    g.drawText ("PRISM RACK", 60, 14, 260, 28, juce::Justification::centredLeft);
    g.setColour (t.subtext);
    g.setFont (uiFont (10.0f, true).withExtraKerningFactor (0.1f));
    g.drawText ("HEADROOM", 548, 16, 84, 24, juce::Justification::centredRight);

    // dividers
    g.setColour (t.divider);
    g.fillRect (16, 56, W - 32, 1);
    for (int i = 1; i < 4; ++i) g.fillRect (i * 245, 66, 1, 356);
    g.fillRect (16, 430, W - 32, 1);

    // tempo readout
    g.setColour (t.text);
    g.setFont (uiFont (22.0f, true));
    g.drawText (tempoText, 300, 456, 380, 30, juce::Justification::centred);
    g.setColour (t.subtext);
    g.setFont (uiFont (9.5f, true).withExtraKerningFactor (0.12f));
    const int sync = (int) proc.apvts.getRawParameterValue (ids::syncMode)->load();
    const char* src[] = { "FREE RUNNING", "FOLLOWING HOST TEMPO", "TAP TEMPO" };
    g.drawText (src[juce::jlimit (0, 2, sync)], 300, 488, 380, 16, juce::Justification::centred);

    if (expanded)
    {
        g.setColour (t.divider);
        g.fillRect (16, H, W - 32, 1);
        for (int i = 1; i < 4; ++i) g.fillRect (i * 245, H + 10, 1, 100);
        g.fillRect (16, H + 118, W - 32, 1);
        g.setColour (t.subtext);
        g.setFont (uiFont (9.5f, true).withExtraKerningFactor (0.12f));
        g.drawText ("GLOBAL", 24, H + 132, 80, 16, juce::Justification::centredLeft);
        g.drawText ("TEMPO SOURCE", 210, H + 128, 130, 14, juce::Justification::centredLeft);
        g.drawText ("TAP BPM", 360, H + 128, 130, 14, juce::Justification::centredLeft);
        g.setFont (uiFont (10.0f));
        g.drawFittedText ("Drag knobs to turn, double-click to reset. AUTO listens for 5 s of playing.", 690, H + 136, 270, 40, juce::Justification::centredLeft, 2);
    }
}

void Panel::resized()
{
    headroom.setBounds (640, 16, 110, 24);
    autoBtn.setBounds (756, 16, 56, 24);
    setupBtn.setBounds (822, 16, 66, 24);
    darkBtn.setBounds (896, 16, 62, 24);

    const auto order = orderFromIndex ((int) proc.apvts.getRawParameterValue (ids::order)->load());
    for (int slot = 0; slot < 4; ++slot)
    {
        const int m = order[(size_t) slot];
        strips[(size_t) m]->setBounds (slot * 245, 56, 245, 374);
        const int x0 = slot * 245, y0 = H + 12;
        volKnobs[(size_t) m]->setBounds (x0 + 10, y0, 64, 80);
        alwaysOn[(size_t) m]->setBounds (x0 + 150, y0 + 6, 86, 22);
        volKnobs[(size_t) m]->setVisible (expanded);
        alwaysOn[(size_t) m]->setVisible (expanded);
        if (m == Texture)   { colorKnob->setBounds (x0 + 80, y0, 64, 80); filterMode.setBounds (x0 + 150, y0 + 40, 86, 22); }
        if (m == Diffusion) { revSpeedKnob->setBounds (x0 + 80, y0, 64, 80); trails.setBounds (x0 + 150, y0 + 40, 86, 22); }
    }
    driftMacroKnob->setBounds (110, H + 124, 64, 72);
    syncMode.setBounds (210, H + 146, 130, 24);
    bpmSlider.setBounds (360, H + 146, 300, 24);
    for (juce::Component* c : { (juce::Component*) colorKnob.get(), (juce::Component*) revSpeedKnob.get(), (juce::Component*) driftMacroKnob.get(),
                                (juce::Component*) &filterMode, (juce::Component*) &syncMode, (juce::Component*) &trails, (juce::Component*) &bpmSlider })
        c->setVisible (expanded);

    bypassSw.setBounds (245 - 42, 436, 84, 100);
    tapSw.setBounds (735 - 42, 436, 84, 100);
}

void Panel::moveModule (int moduleId, int direction)
{
    auto order = orderFromIndex ((int) proc.apvts.getRawParameterValue (ids::order)->load());
    const int slot = (int) (std::find (order.begin(), order.end(), moduleId) - order.begin());
    const int other = slot + direction;
    if (other < 0 || other > 3) return;
    std::swap (order[(size_t) slot], order[(size_t) other]);
    orderAtt.setValueAsCompleteGesture ((float) indexFromOrder (order));
}

void Panel::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.isEmpty() && now - taps.getLast() > 2000.0) taps.clear();
    taps.add (now);
    while (taps.size() > 5) taps.remove (0);
    if (taps.size() >= 2)
    {
        const double avg = (taps.getLast() - taps.getFirst()) / (double) (taps.size() - 1);
        bpmAtt.setValueAsCompleteGesture ((float) juce::jlimit (40.0, 240.0, 60000.0 / avg));
        syncAtt.setValueAsCompleteGesture (2.0f);
    }
    tapSw.flash();
}

void Panel::refresh()
{
    for (auto& s : strips) s->refresh();
    const bool bypassed = proc.apvts.getRawParameterValue (ids::bypass)->load() > 0.5f;
    bypassSw.setLed (! bypassed, juce::Colour (0xff4cd37a));

    const float bpm = proc.displayBpm.load();
    const double beatMs = 60000.0 / std::max (20.0f, bpm);
    const bool beat = std::fmod (juce::Time::getMillisecondCounterHiRes(), beatMs) < 70.0;
    const int sync = (int) proc.apvts.getRawParameterValue (ids::syncMode)->load();
    tapSw.setLed (beat && sync != 0, juce::Colour (0xfff2a541));
    tapSw.tick(); bypassSw.tick();

    const auto txt = sync == 0 ? juce::String ("FREE") : juce::String (bpm, 1) + " BPM";
    if (txt != tempoText) { tempoText = txt; repaint (300, 450, 380, 60); }

    if (autoActive)
    {
        if (proc.autoDone.exchange (false))
        {
            headroomSetAtt.setValueAsCompleteGesture ((float) proc.autoResult.load());
            autoActive = false; autoBtn.setButtonText ("AUTO");
        }
        else autoBtn.setButtonText (juce::String (juce::roundToInt (proc.autoProgress.load() * 100.0f)) + "%");
    }
}

//==============================================================================
// Editor
//==============================================================================
PrismRackEditor::PrismRackEditor (PrismRackProcessor& p)
    : AudioProcessorEditor (&p), proc (p), panel (p, laf)
{
    dark = (bool) proc.apvts.state.getProperty ("darkMode", false);
    laf.setTheme (dark ? prism::ui::darkTheme() : prism::ui::lightTheme());
    setLookAndFeel (&laf);
    addAndMakeVisible (panel);
    panel.onExpandChanged = [this]
    {
        const int h = prism::ui::Panel::H + (panel.expanded ? prism::ui::Panel::ExpandH : 0);
        const double scale = (double) getWidth() / prism::ui::Panel::W;
        constrainer.setFixedAspectRatio ((double) prism::ui::Panel::W / h);
        constrainer.setSizeLimits (640, (int) (640.0 * h / prism::ui::Panel::W), 1960, (int) (1960.0 * h / prism::ui::Panel::W));
        setSize (getWidth(), (int) std::round (h * scale));
    };
    constrainer.setFixedAspectRatio ((double) prism::ui::Panel::W / prism::ui::Panel::H);
    constrainer.setSizeLimits (640, 640 * prism::ui::Panel::H / prism::ui::Panel::W, 1960, 1960 * prism::ui::Panel::H / prism::ui::Panel::W);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (prism::ui::Panel::W, prism::ui::Panel::H);
    startTimerHz (30);
}

PrismRackEditor::~PrismRackEditor() { setLookAndFeel (nullptr); }

void PrismRackEditor::resized()
{
    const int h = prism::ui::Panel::H + (panel.expanded ? prism::ui::Panel::ExpandH : 0);
    const float scale = (float) getWidth() / (float) prism::ui::Panel::W;
    panel.setBounds (0, 0, prism::ui::Panel::W, h);
    panel.setTransform (juce::AffineTransform::scale (scale));
}

void PrismRackEditor::applyTheme()
{
    laf.setTheme (dark ? prism::ui::darkTheme() : prism::ui::lightTheme());
    sendLookAndFeelChange();
    repaint();
}

void PrismRackEditor::timerCallback()
{
    const bool d = (bool) proc.apvts.state.getProperty ("darkMode", false);
    if (d != dark) { dark = d; applyTheme(); }
    panel.refresh();
}
