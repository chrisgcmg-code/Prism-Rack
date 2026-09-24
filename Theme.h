#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace prism::ui
{
struct Theme
{
    juce::Colour panel, panelEdge, text, subtext, divider, knob, knobEdge, pointer, ledOff;
};

inline Theme lightTheme()
{
    return { juce::Colour (0xffece8df), juce::Colour (0xff2a2a2a), juce::Colour (0xff1e1e1e), juce::Colour (0xff6b665e),
             juce::Colour (0x22000000), juce::Colour (0xff1b1b1c), juce::Colour (0xff3a3a3c), juce::Colours::white, juce::Colour (0xffcfc9bd) };
}

inline Theme darkTheme()
{
    return { juce::Colour (0xff26272b), juce::Colour (0xff0e0e10), juce::Colour (0xffedeae3), juce::Colour (0xff9a968e),
             juce::Colour (0x22ffffff), juce::Colour (0xff121213), juce::Colour (0xff444448), juce::Colours::white, juce::Colour (0xff3a3b40) };
}

// Original palette: one colour per effect, per module.
inline juce::Colour effectColour (int module, int effect)
{
    static const juce::uint32 c[4][5] = {
        { 0xfff2a541, 0xffe8735a, 0xffc0396b, 0xff7b4fa0, 0xffd9c45a },   // Drive Sweeten Fuzz Howl Swell
        { 0xff3aa59a, 0xff4f9ddb, 0xff8cc152, 0xffe27396, 0xff5a5fc9 },   // Doubler Vibrato Phaser Tremolo Pitch
        { 0xff7cc4d6, 0xff5bb87a, 0xff8c8fe0, 0xfff08a3c, 0xffd9534f },   // Cascade Reels Space Collage Reverse
        { 0xff7f8c99, 0xffc9a227, 0xffa67b5b, 0xff8e5a8c, 0xff2fb5c8 } }; // Filter Squash Cassette Broken Interference
    return juce::Colour (c[juce::jlimit (0, 3, module)][juce::jlimit (0, 4, effect)]);
}

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    Theme theme = lightTheme();

    void setTheme (const Theme& t)
    {
        theme = t;
        setColour (juce::ComboBox::backgroundColourId, t.panel.darker (0.06f));
        setColour (juce::ComboBox::textColourId, t.text);
        setColour (juce::ComboBox::outlineColourId, t.panelEdge.withAlpha (0.4f));
        setColour (juce::ComboBox::arrowColourId, t.subtext);
        setColour (juce::PopupMenu::backgroundColourId, t.panel);
        setColour (juce::PopupMenu::textColourId, t.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, t.text.withAlpha (0.12f));
        setColour (juce::PopupMenu::highlightedTextColourId, t.text);
        setColour (juce::TextButton::buttonColourId, t.panel.darker (0.06f));
        setColour (juce::TextButton::buttonOnColourId, t.text);
        setColour (juce::TextButton::textColourOffId, t.text);
        setColour (juce::TextButton::textColourOnId, t.panel);
        setColour (juce::BubbleComponent::backgroundColourId, t.text);
        setColour (juce::BubbleComponent::outlineColourId, t.text);
        setColour (juce::TooltipWindow::backgroundColourId, t.text);
        setColour (juce::TooltipWindow::textColourId, t.panel);
        setColour (juce::Slider::trackColourId, t.text);
        setColour (juce::Slider::backgroundColourId, t.divider);
        setColour (juce::Slider::thumbColourId, t.knob);
        setColour (juce::Slider::textBoxTextColourId, t.text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::textColourId, t.text);
        setColour (juce::TextEditor::backgroundColourId, t.panel);
        setColour (juce::TextEditor::highlightColourId, t.text.withAlpha (0.2f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (12.0f); }
    juce::Font getPopupMenuFont() override { return uiFont (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return uiFont (11.0f, true); }

    void drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body) override
    {
        g.setColour (theme.text);
        g.fillRoundedRectangle (body, 4.0f);
    }
    void setComponentEffectForBubbleComponent (juce::BubbleComponent& b) override { b.setComponentEffect (nullptr); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        g.setColour (on ? theme.text : (over ? theme.text.withAlpha (0.08f) : juce::Colours::transparentBlack));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (theme.text.withAlpha (down ? 0.9f : 0.45f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float size = std::min (area.getWidth(), area.getHeight());
        auto knob = area.withSizeKeepingCentre (size, size);
        const auto c = knob.getCentre();
        const float ang = a0 + pos * (a1 - a0);

        // optional LED ring (Amount knobs): colour stored in the "ring" property
        if (s.getProperties().contains ("ring"))
        {
            const auto col = juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["ring"]);
            const float rr = size * 0.5f - 2.0f;
            juce::Path track; track.addCentredArc (c.x, c.y, rr, rr, 0, a0, a1, true);
            g.setColour (theme.ledOff);
            g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path val; val.addCentredArc (c.x, c.y, rr, rr, 0, a0, ang, true);
            g.setColour (col.withAlpha (0.35f));
            g.strokePath (val, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (col);
            g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            knob = knob.reduced (size * 0.13f);
        }
        else
        {
            // tick marks around plain knobs
            g.setColour (theme.subtext.withAlpha (0.5f));
            for (int i = 0; i <= 10; ++i)
            {
                const float t = a0 + (a1 - a0) * (float) i / 10.0f;
                const float r1 = size * 0.5f - 1.0f, r2 = size * 0.5f - (i % 5 == 0 ? 6.0f : 4.0f);
                g.drawLine (c.x + r1 * std::sin (t), c.y - r1 * std::cos (t), c.x + r2 * std::sin (t), c.y - r2 * std::cos (t), 1.0f);
            }
            knob = knob.reduced (size * 0.12f);
        }

        // shadow, body, subtle top highlight
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (knob.translated (0, 2.0f));
        juce::ColourGradient grad (theme.knob.brighter (0.25f), knob.getX(), knob.getY(), theme.knob, knob.getRight(), knob.getBottom(), false);
        g.setGradientFill (grad);
        g.fillEllipse (knob);
        g.setColour (theme.knobEdge);
        g.drawEllipse (knob.reduced (0.5f), 1.0f);

        // pointer line
        const float r = knob.getWidth() * 0.5f;
        juce::Path ptr;
        ptr.addRoundedRectangle (-1.5f, -r + 3.0f, 3.0f, r * 0.45f, 1.5f);
        g.setColour (theme.pointer);
        g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (c));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (theme.divider.withAlpha (1.0f).interpolatedWith (theme.subtext, 0.3f));
        g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
        g.setColour (theme.text);
        g.fillRoundedRectangle ((float) x, cy - 2.0f, pos - (float) x, 4.0f, 2.0f);
        g.setColour (theme.knob);
        g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        g.setColour (theme.pointer);
        g.fillEllipse (pos - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }
};
} // namespace prism::ui
