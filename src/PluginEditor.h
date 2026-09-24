#pragma once
#include "PluginProcessor.h"
#include "ui/Theme.h"

namespace prism::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
// A rotary knob with its label printed underneath, like screen-printed pedal text.
class Knob : public juce::Component
{
public:
    Knob (APVTS& s, const juce::String& paramId, juce::String labelText, LookAndFeel& lf)
        : label (std::move (labelText)), laf (lf)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        slider.setPopupDisplayEnabled (true, true, nullptr, 900);
        slider.setDoubleClickReturnValue (true, s.getParameter (paramId)->convertFrom0to1 (s.getParameter (paramId)->getDefaultValue()));
        addAndMakeVisible (slider);
        att = std::make_unique<APVTS::SliderAttachment> (s, paramId, slider);
    }
    void setRingColour (juce::Colour c)
    {
        slider.getProperties().set ("ring", (juce::int64) c.getARGB());
        slider.repaint();
    }
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (laf.theme.subtext);
        g.setFont (uiFont (labelSize, true));
        g.drawText (label, getLocalBounds().removeFromBottom (16), juce::Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds(); r.removeFromBottom (16);
        const int d = std::min (r.getWidth(), r.getHeight());
        slider.setBounds (r.withSizeKeepingCentre (d, d));
    }
    juce::Slider slider;
    float labelSize = 10.0f;
private:
    juce::String label;
    LookAndFeel& laf;
    std::unique_ptr<APVTS::SliderAttachment> att;
};

//==============================================================================
class EffectButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    EffectButton (int m, int i, juce::String n, LookAndFeel& lf) : module (m), index (i), name (std::move (n)), laf (lf)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (name);
    }
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        const auto col = effectColour (module, index);
        auto r = getLocalBounds().toFloat();
        auto dot = r.removeFromTop (22).withSizeKeepingCentre (15, 15);
        if (selected)
        {
            g.setColour (col.withAlpha (0.35f)); g.fillEllipse (dot.expanded (4));
            g.setColour (col); g.fillEllipse (dot);
            g.setColour (juce::Colours::white.withAlpha (0.5f)); g.fillEllipse (dot.reduced (4.5f).translated (-1.5f, -1.5f));
        }
        else
        {
            g.setColour (col.withAlpha (hover ? 0.55f : 0.25f)); g.fillEllipse (dot);
            g.setColour (col.withAlpha (0.8f)); g.drawEllipse (dot.reduced (0.5f), 1.2f);
        }
        g.setColour (selected ? laf.theme.text : laf.theme.subtext);
        g.setFont (uiFont (8.5f, selected));
        g.drawFittedText (name.toUpperCase(), r.toNearestInt(), juce::Justification::centredTop, 1, 0.7f);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    std::function<void()> onClick;
private:
    int module, index; juce::String name; LookAndFeel& laf; bool selected = false, hover = false;
};

//==============================================================================
// Small round LED that toggles on click (module bypass).
class Led : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Led (LookAndFeel& lf) : laf (lf) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void set (bool on, juce::Colour c) { if (on != lit || c != colour) { lit = on; colour = c; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (12, 12);
        if (lit) { g.setColour (colour.withAlpha (0.35f)); g.fillEllipse (r.expanded (3)); }
        g.setColour (lit ? colour : laf.theme.ledOff); g.fillEllipse (r);
        g.setColour (laf.theme.panelEdge.withAlpha (0.4f)); g.drawEllipse (r, 1.0f);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    std::function<void()> onClick;
private:
    LookAndFeel& laf; bool lit = false; juce::Colour colour;
};

//==============================================================================
class Footswitch : public juce::Component
{
public:
    Footswitch (juce::String l, LookAndFeel& lf) : label (std::move (l)), laf (lf) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setLed (bool on, juce::Colour c) { if (on != ledOn || c != ledCol) { ledOn = on; ledCol = c; repaint(); } }
    void flash() { flashFrames = 4; repaint(); }
    void tick() { if (flashFrames > 0 && --flashFrames == 0) repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto ledR = r.removeFromTop (18).withSizeKeepingCentre (10, 10);
        const bool lit = ledOn || flashFrames > 0;
        if (lit) { g.setColour (ledCol.withAlpha (0.35f)); g.fillEllipse (ledR.expanded (3)); }
        g.setColour (lit ? ledCol : laf.theme.ledOff); g.fillEllipse (ledR);
        auto lab = r.removeFromBottom (16);
        auto sw = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight())).reduced (2);
        g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillEllipse (sw.translated (0, 2));
        juce::ColourGradient grad (juce::Colour (0xffe9e9ea), sw.getX(), sw.getY(), juce::Colour (0xff8c8d91), sw.getRight(), sw.getBottom(), false);
        g.setGradientFill (grad); g.fillEllipse (sw);
        auto inner = sw.reduced (sw.getWidth() * 0.18f);
        juce::ColourGradient grad2 (juce::Colour (0xff9d9ea2), inner.getX(), inner.getY(), juce::Colour (0xffdedfe1), inner.getRight(), inner.getBottom(), false);
        g.setGradientFill (grad2); g.fillEllipse (inner.translated (0, pressed ? 1.0f : 0.0f));
        g.setColour (juce::Colour (0xff55565a)); g.drawEllipse (sw, 1.0f);
        g.setColour (laf.theme.subtext); g.setFont (uiFont (10.0f, true));
        g.drawText (label, lab, juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { pressed = true; repaint(); if (onPress) onPress(); }
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }
    std::function<void()> onPress;
private:
    juce::String label; LookAndFeel& laf; bool ledOn = false, pressed = false; juce::Colour ledCol; int flashFrames = 0;
};

//==============================================================================
class ModuleStrip : public juce::Component
{
public:
    ModuleStrip (PrismRackProcessor& p, int moduleId, LookAndFeel& lf);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();                       // called from the editor timer
    std::function<void (int moduleId, int direction)> onMove;
    int module;
private:
    PrismRackProcessor& proc; LookAndFeel& laf;
    std::unique_ptr<Knob> top, amount, small;
    std::array<std::unique_ptr<EffectButton>, 5> buttons;
    juce::TextButton left { "<" }, right { ">" };
    Led led;
    juce::ParameterAttachment typeAtt, bypassAtt;
    int currentType = 0; bool bypassed = false;
    int paintedType = -1; bool paintedBypass = false; juce::Colour paintedPanel;
};

//==============================================================================
class Panel : public juce::Component
{
public:
    static constexpr int W = 980, H = 540, ExpandH = 200;
    Panel (PrismRackProcessor& p, LookAndFeel& lf);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    bool expanded = false;
    std::function<void()> onExpandChanged;
private:
    void moveModule (int moduleId, int direction);
    void tap();
    void buildTexture();
    PrismRackProcessor& proc; LookAndFeel& laf;
    std::array<std::unique_ptr<ModuleStrip>, NumModules> strips;
    juce::ComboBox headroom; std::unique_ptr<APVTS::ComboBoxAttachment> headroomAtt;
    juce::TextButton autoBtn { "AUTO" }, setupBtn { "SETUP" }, darkBtn { "DARK" };
    Footswitch bypassSw { "BYPASS", laf }, tapSw { "TAP", laf };
    juce::ParameterAttachment orderAtt, bypassAtt, bpmAtt, syncAtt, headroomSetAtt;
    bool autoActive = false;
    juce::String tempoText;
    juce::Array<double> taps;
    juce::Image texture; bool textureDark = false;

    // expanded setup view
    std::array<std::unique_ptr<Knob>, NumModules> volKnobs;
    std::array<std::unique_ptr<juce::TextButton>, NumModules> alwaysOn;
    std::array<std::unique_ptr<APVTS::ButtonAttachment>, NumModules> alwaysOnAtt;
    std::unique_ptr<Knob> colorKnob, revSpeedKnob, driftMacroKnob;
    juce::ComboBox filterMode, syncMode;
    std::unique_ptr<APVTS::ComboBoxAttachment> filterModeAtt, syncModeAtt;
    juce::TextButton trails { "TRAILS" };
    std::unique_ptr<APVTS::ButtonAttachment> trailsAtt;
    juce::Slider bpmSlider;
    std::unique_ptr<APVTS::SliderAttachment> bpmSliderAtt;
    juce::Rectangle<int> setupArea;
};
} // namespace prism::ui

//==============================================================================
class PrismRackEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrismRackEditor (PrismRackProcessor&);
    ~PrismRackEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (laf.theme.panelEdge); }
    void resized() override;
private:
    void timerCallback() override;
    void applyTheme();
    PrismRackProcessor& proc;
    prism::ui::LookAndFeel laf;
    prism::ui::Panel panel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::ComponentBoundsConstrainer constrainer;
    bool dark = false;
};
