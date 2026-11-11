#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
const juce::Colour bg     (0xff0d0e12);
const juce::Colour panel  (0xff161820);
const juce::Colour panel2 (0xff1e212b);
const juce::Colour line   (0xff2b2f3b);
const juce::Colour text   (0xfff1f2f6);
const juce::Colour dim    (0xff8b90a0);
const juce::Colour accent (0xffff6a2b);

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

class TriadLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TriadLookAndFeel()
    {
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider&) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        const auto c = r.getCentre();
        const auto circle = juce::Rectangle<float> (d, d).withCentre (c);

        g.setColour (panel2);
        g.fillEllipse (circle);
        g.setColour (line);
        g.drawEllipse (circle, 1.0f);

        const float angle = a0 + pos * (a1 - a0);
        const float rad = d * 0.5f + 2.5f;

        juce::Path track, value;
        track.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
        value.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, angle, true);
        g.setColour (line);
        g.strokePath (track, juce::PathStrokeType (2.0f));
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (2.5f));

        juce::Path p;
        p.addRoundedRectangle (-1.5f, -d * 0.5f + 4.0f, 3.0f, d * 0.24f, 1.5f);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        g.setColour (text);
        g.fillPath (p);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (14.0f, true); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = b.isEnabled() ? panel2 : panel;
        if (b.isEnabled() && down) fill = fill.darker (0.3f);
        else if (b.isEnabled() && highlighted) fill = fill.brighter (0.12f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (b.getToggleState() ? accent : line);
        g.drawRoundedRectangle (r, 9.0f, b.getToggleState() ? 1.5f : 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto r = b.getLocalBounds().toFloat();
        const bool on = b.isEnabled();
        g.setFont (font (14.0f, true));

        if (b.getComponentID() == "preview")
        {
            const float cy = r.getCentreY();
            const float x = r.getX() + 16.0f;
            juce::Path p;
            if (b.getToggleState()) p.addRoundedRectangle (x, cy - 6.0f, 12.0f, 12.0f, 2.0f);
            else                    p.addTriangle (x, cy - 7.0f, x, cy + 7.0f, x + 12.0f, cy);
            g.setColour (b.getToggleState() ? accent : (on ? text : dim));
            g.fillPath (p);
            g.setColour (on ? text : dim);
            g.drawText (b.getButtonText(), r.withTrimmedLeft (42.0f), juce::Justification::centredLeft);
            return;
        }

        if (b.getComponentID() == "chip")
        {
            g.setFont (font (13.0f, true));
            g.setColour (b.getToggleState() ? accent : text);
            g.drawText (b.getButtonText(), r, juce::Justification::centred);
            return;
        }

        g.setColour (on ? text : dim);
        g.drawText (b.getButtonText(), r, juce::Justification::centred);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        const auto pill = r.withSizeKeepingCentre (r.getWidth(), 24.0f);

        g.setColour (on ? accent.withAlpha (0.18f) : panel2);
        g.fillRoundedRectangle (pill, 12.0f);
        g.setColour (on ? accent : line);
        g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.0f);
        g.setColour (on ? accent : dim);
        g.setFont (font (12.0f, true));
        g.drawText (b.getButtonText() + (on ? " on" : " off"), pill, juce::Justification::centred);
    }
};
}
