#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace flux::ui
{

namespace colours
{
    inline const juce::Colour background { 0xff111316 };
    inline const juce::Colour panel      { 0xff1a1d22 };
    inline const juce::Colour panelEdge  { 0xff272b32 };
    inline const juce::Colour text       { 0xffd8dde3 };
    inline const juce::Colour dim        { 0xff7d8590 };
    inline const juce::Colour accent     { 0xff3ad6c4 };   // left channel / main accent
    inline const juce::Colour accent2    { 0xffd65aa8 };   // right channel
    inline const juce::Colour track      { 0xff2c3138 };
}

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
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineW = juce::jmax (2.5f, radius * 0.14f);
        const float arcR = radius - lineW * 0.5f;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::track);
        g.strokePath (track, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Bipolar parameters fill from the middle.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float fromPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
        const float a0 = startAngle + fromPos * (endAngle - startAngle);
        const float a1 = startAngle + pos * (endAngle - startAngle);

        if (std::abs (a1 - a0) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
            g.setColour (slider.isEnabled() ? colours::accent : colours::dim);
            g.strokePath (value, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        const float dotR = lineW * 0.55f;
        const juce::Point<float> tip (centre.x + (arcR - lineW * 1.6f) * std::sin (a1),
                                      centre.y - (arcR - lineW * 1.6f) * std::cos (a1));
        g.setColour (colours::text);
        g.fillEllipse (juce::Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (tip));
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
        const auto trackArea = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
        g.setColour (colours::track);
        g.fillRoundedRectangle (trackArea, 2.0f);

        // Bipolar sliders (matrix depth) fill from the middle, so 0 looks empty.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float from = bipolar ? r.getX() + (float) slider.valueToProportionOfLength (0.0) * r.getWidth() : r.getX();
        const float a = juce::jmin (from, sliderPos), b = juce::jmax (from, sliderPos);
        g.setColour (slider.isEnabled() ? colours::accent : colours::dim);
        g.fillRoundedRectangle (trackArea.withX (a).withWidth (juce::jmax (0.0f, b - a)), 2.0f);

        if (bipolar)
        {
            g.setColour (colours::dim);
            g.fillRect (juce::Rectangle<float> (1.0f, 8.0f).withCentre ({ from, r.getCentreY() }));
        }
        g.setColour (colours::text);
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ sliderPos, r.getCentreY() }));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return juce::FontOptions (12.0f); }
};

} // namespace flux::ui
