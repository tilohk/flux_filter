#pragma once

#include "ParamControl.h"

namespace flux::ui
{

// The move icon on every LFO: drag it onto a knob to connect, click it to highlight everything
// this LFO modulates.
class SourceHandle : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    SourceHandle (ModContext& context, ModSource s) : ctx (context), source (s)
    {
        setTooltip ("Drag onto a knob to modulate it. Click to show where it's connected.");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool highlighted = ctx.getHighlighted() == source;
        if (isMouseOver() || highlighted)   // no border; just a soft tile on hover / highlight
        {
            g.setColour (colours::track.withAlpha (highlighted ? 1.0f : 0.7f));
            g.fillRoundedRectangle (r, 5.0f);
        }
        const auto c = isMouseOver() || highlighted ? colours::text : colours::dim;

        // Four arrows: the classic move/drag icon.
        const auto ctr = r.getCentre();
        const float arm = r.getWidth() * 0.32f, head = r.getWidth() * 0.12f;
        juce::Path p;
        for (int i = 0; i < 4; ++i)
        {
            const float ang = (float) i * juce::MathConstants<float>::halfPi;
            const juce::Point<float> dir (std::sin (ang), -std::cos (ang)), side (-dir.y, dir.x);
            const auto tip = ctr + dir * arm;
            p.startNewSubPath (ctr);
            p.lineTo (tip);
            p.startNewSubPath (tip - dir * head + side * head);
            p.lineTo (tip);
            p.lineTo (tip - dir * head - side * head);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 4) return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            container->startDragging (ModContext::dragDescription (source), this);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) ctx.toggleHighlight (source);
        dragging = false;
    }

private:
    void timerCallback() override
    {
        if (ctx.getVersion() != seenVersion) { seenVersion = ctx.getVersion(); repaint(); }
    }

    ModContext& ctx;
    ModSource source;
    bool dragging = false;
    uint32_t seenVersion = 0;
};

// An analog-style LFO: shape buttons, rate knob (note divisions when synced), sync, polarity and
// the Flux amount (how much of its cell modulation the living cells absorb).
class LfoModule : public juce::Component, private juce::Timer
{
public:
    LfoModule (ModContext& context, int lfoIndex)
        : ctx (context), index (lfoIndex), source ((ModSource) ((int) ModSource::Lfo1 + lfoIndex)),
          handle (context, source),
          rate (context, spec::get ((spec::FP) ((int) spec::FP::Lfo1Rate + lfoIndex)).id, "Rate"),
          division (context, params::id::lfo (lfoIndex, "Division"), "Rate", KnobSize::Normal, true),
          flux (context, params::id::lfo (lfoIndex, "Flux"), "Flux"),
          shapes (context, params::id::lfo (lfoIndex, "Shape"), {}, [] (int i, juce::Rectangle<float> a) {
              switch ((ModLfoShape) i)
              {
                  case ModLfoShape::Triangle: return icons::triangle (a);
                  case ModLfoShape::Saw:      return icons::rampUp (a);
                  case ModLfoShape::Square:   return icons::square (a);
                  case ModLfoShape::Random:   return icons::random (a);
                  case ModLfoShape::Sine:     break;
              }
              return icons::sine (a);
          }, colours::source (source))
    {
        auto& state = ctx.state;
        addAndMakeVisible (handle);
        addAndMakeVisible (shapes);

        for (auto* k : { &rate, &division })   // rate knobs carry the LFO's colour
            k->getSlider().setColour (juce::Slider::rotarySliderFillColourId, colours::source (source));
        addAndMakeVisible (rate);
        addChildComponent (division);

        sync.setButtonText ("SYNC");
        sync.setClickingTogglesState (true);
        sync.setTooltip ("Lock the rate to the host tempo");
        addAndMakeVisible (sync);
        syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::id::lfo (index, "Sync"), sync);

        polarity.setClickingTogglesState (true);
        polarity.setColour (juce::TextButton::buttonOnColourId, colours::panelEdge.brighter (0.15f));   // neutral either way
        polarity.setColour (juce::TextButton::textColourOnId, colours::text);
        polarity.setTooltip ("Bipolar (+/-) swings both ways around the knob, unipolar (+) only one way");
        polarity.onStateChange = [this] { updatePolarityText(); };
        addAndMakeVisible (polarity);
        polarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::id::lfo (index, "Unipolar"), polarity);
        updatePolarityText();

        flux.getSlider().getProperties().set (heroKnobProperty, true);   // looks like the big Flux knob
        flux.getSlider().setTooltip ("Flux: how much of this LFO's movement on Vowel, Resonance and Scatter the living cells absorb: "
                                     "each follows it with its own delay and depth, as far as it is alive (0 = all cells exactly together)");
        addAndMakeVisible (flux);

        startTimerHz (15);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        paintPanel (g, getLocalBounds(), "LFO " + juce::String (index + 1), colours::text, colours::panelEdge, 44);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 0);
        auto title = r.removeFromTop (28);
        handle.setBounds (title.removeFromLeft (24).withSizeKeepingCentre (24, 24));

        r.removeFromTop (6);   // breathing room under the caption
        shapes.setBounds (r.removeFromTop (shapes.preferredHeight()));

        r.removeFromTop (4);
        r.removeFromBottom (8);
        // Left: Rate with SYNC and polarity under it. Right: the Flux knob.
        auto left = r.removeFromLeft (r.getWidth() / 2).reduced (6, 0);
        auto buttons = left.removeFromBottom (20);
        sync.setBounds (buttons.removeFromLeft (buttons.getWidth() * 3 / 5));
        buttons.removeFromLeft (4);
        polarity.setBounds (buttons);
        left.removeFromBottom (2);
        rate.setBounds (left.withSizeKeepingCentre (rate.preferredWidth(), juce::jmin (left.getHeight(), rate.preferredHeight())));
        division.setBounds (rate.getBounds());

        flux.setBounds (r.withSizeKeepingCentre (flux.preferredWidth(), juce::jmin (left.getHeight(), flux.preferredHeight()))
                         .withY (rate.getY()));
    }

private:
    void updatePolarityText()
    {
        polarity.setButtonText (polarity.getToggleState() ? juce::String ("+") : juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")));
    }

    void timerCallback() override
    {
        const bool synced = sync.getToggleState();
        if (rate.isVisible() == synced) { rate.setVisible (! synced); division.setVisible (synced); }
    }

    ModContext& ctx;
    int index;
    ModSource source;
    SourceHandle handle;
    ParamControl rate, division, flux;
    ChoiceButtonRow shapes;
    juce::TextButton sync, polarity;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment, polarityAttachment;
};

} // namespace flux::ui
