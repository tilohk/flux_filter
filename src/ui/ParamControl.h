#pragma once

#include "FluxLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace flux::ui
{

// One parameter as a control: knob for numbers, drop-down for choices, switch for on/off.
// The type is taken from the parameter itself, so any parameter ID works.
class ParamControl : public juce::Component
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    ParamControl (APVTS& state, const juce::String& paramId, const juce::String& labelText = {})
    {
        auto* param = state.getParameter (paramId);
        jassert (param != nullptr);

        label.setText (labelText.isNotEmpty() ? labelText : param->getName (40), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, colours::dim);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            kind = Kind::Choice;
            combo.addItemList (choice->choices, 1);
            addAndMakeVisible (combo);
            comboAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, combo);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        {
            kind = Kind::Toggle;
            toggle.setButtonText (label.getText());
            label.setVisible (false);
            addAndMakeVisible (toggle);
            buttonAttachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, toggle);
        }
        else
        {
            kind = Kind::Knob;
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            addAndMakeVisible (slider);
            sliderAttachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        }
    }

    // Preferred size inside a section.
    int preferredWidth() const noexcept { return kind == Kind::Choice ? 112 : kind == Kind::Toggle ? 104 : 74; }
    static constexpr int preferredHeight = 96;

    void resized() override
    {
        auto r = getLocalBounds();
        switch (kind)
        {
            case Kind::Knob:
                label.setBounds (r.removeFromTop (16));
                slider.setBounds (r);
                break;
            case Kind::Choice:
                label.setBounds (r.removeFromTop (16));
                combo.setBounds (r.removeFromTop (26).reduced (2, 0));
                break;
            case Kind::Toggle:
                toggle.setBounds (r.withSizeKeepingCentre (r.getWidth(), 26).withY (r.getY() + 16));
                break;
        }
    }

private:
    enum class Kind { Knob, Choice, Toggle } kind = Kind::Knob;
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;
    std::unique_ptr<APVTS::SliderAttachment> sliderAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> buttonAttachment;
};

// A titled panel of controls that flow left to right and wrap. Extra components (meters, editors)
// can be placed in the area below the controls via setExtra().
class ParamSection : public juce::Component
{
public:
    explicit ParamSection (juce::String sectionTitle) : title (std::move (sectionTitle)) {}

    ParamControl& add (ParamControl::APVTS& state, const juce::String& paramId, const juce::String& labelText = {})
    {
        auto& c = *controls.emplace_back (std::make_unique<ParamControl> (state, paramId, labelText));
        addAndMakeVisible (c);
        return c;
    }

    void setExtra (juce::Component* extraComponent, int heightPx)
    {
        extra = extraComponent;
        extraHeight = heightPx;
        if (extra != nullptr)
            addAndMakeVisible (extra);
    }

    // Width needed to fit every control in one row.
    int singleRowWidth() const
    {
        int w = 2 * pad;
        for (auto& c : controls) w += c->preferredWidth() + gap;
        return w;
    }

    // Height needed for a given width.
    int heightFor (int width) const
    {
        int rows = 1, x = 0;
        const int avail = width - 2 * pad;
        for (auto& c : controls)
        {
            if (x > 0 && x + c->preferredWidth() > avail) { ++rows; x = 0; }
            x += c->preferredWidth() + gap;
        }
        return titleH + pad + rows * ParamControl::preferredHeight + (extra != nullptr ? extraHeight + gap : 0) + pad;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (title, getLocalBounds().removeFromTop (titleH).withTrimmedLeft (pad), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad, 0);
        r.removeFromTop (titleH);
        r.removeFromBottom (pad);
        if (extra != nullptr)
        {
            extra->setBounds (r.removeFromBottom (extraHeight));
            r.removeFromBottom (gap);
        }

        int x = r.getX(), y = r.getY();
        for (auto& c : controls)
        {
            const int w = c->preferredWidth();
            if (x > r.getX() && x + w > r.getRight()) { x = r.getX(); y += ParamControl::preferredHeight; }
            c->setBounds (x, y, w, ParamControl::preferredHeight);
            x += w + gap;
        }
    }

private:
    static constexpr int pad = 10, gap = 6, titleH = 28;
    juce::String title;
    std::vector<std::unique_ptr<ParamControl>> controls;
    juce::Component* extra = nullptr;
    int extraHeight = 0;
};

} // namespace flux::ui
