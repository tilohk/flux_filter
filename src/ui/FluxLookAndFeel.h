#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "mod/ModMatrix.h"

namespace flux::ui
{

namespace colours
{
    inline const juce::Colour background { 0xff131110 };   // plugin background: warm near-black
    inline const juce::Colour panel      { 0xff2b2724 };   // "warm charcoal": every section except the field display
    inline const juce::Colour panelEdge  { 0xff3b3531 };
    inline const juce::Colour viewport   { 0xff1b1816 };   // field display background (deep warm brown-black)
    inline const juce::Colour viewportGrid { 0xff2e2925 };
    inline const juce::Colour channelL   { 0xff8fc9a9 };   // field display: left cells (sage)
    inline const juce::Colour channelR   { 0xffe08a78 };   // right cells (clay rose)
    inline const juce::Colour spectrum   { 0xffd8cec2 };
    inline const juce::Colour knobTop    { 0xff4a423c };   // knob body gradient
    inline const juce::Colour knobBottom { 0xff1f1b18 };
    inline const juce::Colour text       { 0xffeee8e2 };
    inline const juce::Colour dim        { 0xffb0a79f };
    inline const juce::Colour accent     { 0xff3ad6c4 };   // left channel / main accent
    inline const juce::Colour accent2    { 0xffd65aa8 };   // right channel
    inline const juce::Colour track      { 0xff1a1d22 };   // the old dark panel colour: knob tracks and buttons look recessed
    inline const juce::Colour flux       { 0xffffa23a };   // the flux highlight: hero knob, LFO flux switches
    inline const juce::Colour clip       { 0xffff4b4b };

    inline juce::Colour source (ModSource s)
    {
        switch (s)
        {
            case ModSource::Lfo1:  return juce::Colour (0xff4d8dff);   // blue
            case ModSource::Lfo2:  return juce::Colour (0xffff4f6e);   // red
            case ModSource::None:
            case ModSource::Count: break;
        }
        return dim;
    }
}

// Slider properties understood by the look and feel.
inline const juce::Identifier heroKnobProperty ("fluxHero");
inline const juce::Identifier stepsProperty ("steps");
inline const juce::Identifier liveOffsetProperty ("liveOffset");   // hero knob: current modulation offset (0..1 space)   // number of positions: draws a marker per step

class FluxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FluxLookAndFeel()
    {
        using namespace colours;
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, track);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, track);
        setColour (juce::ComboBox::outlineColourId, panelEdge);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, dim);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, dim);
        setColour (juce::TabbedButtonBar::tabTextColourId, dim);
        setColour (juce::TabbedButtonBar::frontTextColourId, text);
        setColour (juce::TabbedComponent::backgroundColourId, background);
        setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonColourId, track);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, dim);
        setColour (juce::TextButton::textColourOnId, background);
        setColour (juce::TooltipWindow::backgroundColourId, panel);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, panelEdge);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = background;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
        else if (down)       c = c.brighter (0.15f);
        else if (highlighted) c = c.brighter (0.08f);
        const float corner = juce::jmin (r.getHeight() * 0.5f, 6.0f);
        if (b.getToggleState() && b.isEnabled())   // soft glow around switched-on pills
        {
            g.setColour (c.withAlpha (0.25f));
            g.fillRoundedRectangle (r.expanded (1.5f), corner + 1.5f);
        }
        g.setColour (c);
        g.fillRoundedRectangle (r, corner);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (12.0f, juce::Font::bold); }

    // Every knob is a round body with a pointer, inside a value arc. The flux knobs (hero) add an
    // orange glow that grows with the amount.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const bool hero = slider.getProperties()[heroKnobProperty];
        const auto bounds = knobBounds (juce::Rectangle<int> (x, y, width, height));
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineW = hero ? juce::jmax (2.5f, radius * 0.1f) : juce::jmax (3.0f, radius * 0.15f);
        const auto valueColour = hero ? colours::flux
                               : slider.isColourSpecified (juce::Slider::rotarySliderFillColourId)
                                     ? slider.findColour (juce::Slider::rotarySliderFillColourId) : colours::accent;

        if (hero)
        {
            const float glow = 0.08f + 0.3f * pos;
            g.setGradientFill (juce::ColourGradient (colours::flux.withAlpha (glow), centre.x, centre.y,
                                                     colours::flux.withAlpha (0.0f), centre.x + radius * 1.15f, centre.y, true));
            g.fillEllipse (bounds.expanded (radius * 0.15f));
        }

        // Value arc on a recessed track.
        const float arcR = radius - lineW * 0.5f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::track);
        g.strokePath (track, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;   // fills from the middle
        const float fromPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
        // The hero knob's value arc and lamp show where the modulation has it right now; its
        // pointer stays on the knob's own setting.
        const float livePos = hero ? juce::jlimit (0.0f, 1.0f, pos + (float) slider.getProperties().getWithDefault (liveOffsetProperty, 0.0f)) : pos;
        const float a0 = startAngle + fromPos * (endAngle - startAngle);
        const float a1 = startAngle + pos * (endAngle - startAngle);
        const float aLive = startAngle + livePos * (endAngle - startAngle);
        if (std::abs (aLive - a0) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (a0, aLive), juce::jmax (a0, aLive), true);
            g.setColour (slider.isEnabled() ? valueColour : colours::dim);
            g.strokePath (value, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // Knob body: soft drop shadow, top-lit gradient, thin rim highlight.
        const float bodyR = arcR - lineW * 0.5f - lineW * (hero ? 1.4f : 0.9f);
        if (hero)
        {
            drawHeroBody (g, centre, bodyR, a1, livePos, slider.isEnabled());
            return;
        }
        const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (body.translated (0.0f, bodyR * 0.08f + 1.0f).expanded (1.0f));
        g.setGradientFill (juce::ColourGradient (colours::knobTop, centre.x, body.getY(),
                                                 colours::knobBottom, centre.x, body.getBottom(), false));
        g.fillEllipse (body);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), centre.x, body.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), centre.x, body.getCentreY(), false));
        g.drawEllipse (body.reduced (0.5f), 1.0f);
        if (hero)
        {
            g.setColour (colours::flux.withAlpha (0.35f));
            g.drawEllipse (body, 1.0f);
        }

        juce::Path pointer;
        pointer.startNewSubPath (centre.x + bodyR * 0.3f * std::sin (a1), centre.y - bodyR * 0.3f * std::cos (a1));
        pointer.lineTo (centre.x + bodyR * 0.82f * std::sin (a1), centre.y - bodyR * 0.82f * std::cos (a1));
        g.setColour (slider.isEnabled() ? colours::text : colours::dim);
        g.strokePath (pointer, { hero ? juce::jmax (2.0f, bodyR * 0.05f) : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        if (const int steps = slider.getProperties()[stepsProperty]; steps > 1)
        {
            const int current = juce::roundToInt (pos * (float) (steps - 1));
            for (int i = 0; i < steps; ++i)
            {
                const float a = startAngle + (float) i / (float) (steps - 1) * (endAngle - startAngle);
                const float rr = arcR + lineW * 0.5f + 3.0f;
                g.setColour (i == current ? colours::text : colours::dim);
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ centre.x + rr * std::sin (a), centre.y - rr * std::cos (a) }));
            }
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
            return;
        }

        const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float thickness = bipolar ? 10.0f : 4.0f;   // matrix depth bars: thick and easy to grab
        const auto trackArea = r.withSizeKeepingCentre (r.getWidth(), thickness);
        g.setColour (colours::track);
        g.fillRoundedRectangle (trackArea, thickness * 0.5f);

        // Bipolar sliders (matrix depth) fill from the middle, so 0 looks empty.
        const float from = bipolar ? r.getX() + (float) slider.valueToProportionOfLength (0.0) * r.getWidth() : r.getX();
        const float a = juce::jmin (from, sliderPos), b = juce::jmax (from, sliderPos);
        const auto fillColour = slider.isColourSpecified (juce::Slider::trackColourId) ? slider.findColour (juce::Slider::trackColourId)
                                                                                       : colours::accent;
        g.setColour (slider.isEnabled() ? fillColour : colours::dim);
        g.fillRoundedRectangle (trackArea.withX (a).withWidth (juce::jmax (0.0f, b - a)), thickness * 0.5f);

        if (bipolar)
        {
            g.setColour (colours::dim);
            g.fillRect (juce::Rectangle<float> (1.5f, thickness + 6.0f).withCentre ({ from, r.getCentreY() }));
        }
        g.setColour (colours::text);
        const float thumb = bipolar ? 14.0f : 11.0f;
        g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre ({ sliderPos, r.getCentreY() }));
    }

    // The big Flux knob: an analog, industrial control, kept clean. A finely knurled grip ring, a
    // smooth dark-steel cap and an engraved pointer ending in a small amber lamp that lights up with
    // the amount (pos: the live, modulated value).
    static void drawHeroBody (juce::Graphics& g, juce::Point<float> c, float bodyR, float pointerAngle, float pos, bool enabled)
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        auto polar = [c] (float r, float a) { return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };
        auto disc = [c] (float r) { return juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c); };

        // Shadow.
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (disc (bodyR + 1.0f).translated (0.0f, bodyR * 0.06f + 1.0f));

        // Grip ring: a dark ring with fine, low-contrast knurling.
        const float capR = bodyR * 0.82f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff36302b), c.x, c.y - bodyR,
                                                 juce::Colour (0xff181513), c.x, c.y + bodyR, false));
        g.fillEllipse (disc (bodyR));
        constexpr int grooves = 96;
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        for (int i = 0; i < grooves; ++i)
        {
            const float a = twoPi * (float) i / (float) grooves;
            g.drawLine ({ polar (capR + 1.5f, a), polar (bodyR - 1.0f, a) }, 0.8f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawEllipse (disc (bodyR - 0.5f), 1.0f);

        // Cap: smooth dark steel, top-lit, with a thin machined edge.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4b443e), c.x, c.y - capR,
                                                 juce::Colour (0xff25211e), c.x, c.y + capR, false));
        g.fillEllipse (disc (capR));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), c.x, c.y - capR,
                                                 juce::Colours::black.withAlpha (0.35f), c.x, c.y + capR, false));
        g.drawEllipse (disc (capR - 0.5f), 1.2f);

        // Engraved pointer and a small amber lamp at its tip.
        const auto inner = polar (capR * 0.25f, pointerAngle), outer = polar (capR * 0.72f, pointerAngle);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine ({ inner, outer }, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (enabled ? 0.18f : 0.08f));
        g.drawLine ({ inner.translated (0.0f, 1.2f), outer.translated (0.0f, 1.2f) }, 0.7f);

        const auto lampPos = polar (capR * 0.84f, pointerAngle);
        const float glow = enabled ? 0.2f + 0.8f * pos : 0.1f;
        g.setGradientFill (juce::ColourGradient (colours::flux.withAlpha (0.4f * glow), lampPos.x, lampPos.y,
                                                 colours::flux.withAlpha (0.0f), lampPos.x + 7.0f, lampPos.y, true));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (lampPos));
        g.setColour (colours::flux.withAlpha (0.3f + 0.7f * glow));
        g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (lampPos));
    }

    // Where the knob is drawn inside a rotary slider's area (leaves room for modulation rings).
    static juce::Rectangle<float> knobBounds (juce::Rectangle<int> sliderArea)
    {
        auto r = sliderArea.toFloat().reduced (7.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        return r.withSizeKeepingCentre (size, size);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return juce::FontOptions (12.0f); }
};

} // namespace flux::ui
