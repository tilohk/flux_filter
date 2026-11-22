#pragma once

#include "FluxLookAndFeel.h"
#include "params/Parameters.h"

namespace flux::ui
{

// Header switch: what the cells do to their bands. CUT (the default): notches in series, the field
// carves the sound. BOOST: resonant peaks in parallel, the field sings. Two segments on one track.
class CutSwitch : public juce::Component
{
public:
    explicit CutSwitch (juce::AudioProcessorValueTreeState& state)
    {
        auto* param = state.getParameter (params::id::cut);
        jassert (param != nullptr);
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { show (v > 0.5f); });

        auto setUp = [this] (juce::TextButton& b, const char* text, const char* tip, bool cutOn) {
            b.setButtonText (text);
            b.setTooltip (tip);
            b.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            b.setColour (juce::TextButton::buttonOnColourId, colours::text);
            b.onClick = [this, cutOn] { attachment->setValueAsCompleteGesture (cutOn ? 1.0f : 0.0f); };
            addAndMakeVisible (b);
        };
        setUp (cut, "CUT", "Cut: every cell carves a notch out of the sound (cells in series)", true);
        setUp (boost, "BOOST", "Boost: every cell is a resonant peak (cells in parallel)", false);
        attachment->sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 3.5f);
        g.setColour (colours::track);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 5);
        cut.setBounds (r.removeFromLeft (r.getWidth() / 2));
        boost.setBounds (r);
    }

private:
    void show (bool isCut)
    {
        cut.setToggleState (isCut, juce::dontSendNotification);
        boost.setToggleState (! isCut, juce::dontSendNotification);
    }

    juce::TextButton cut, boost;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

} // namespace flux::ui
