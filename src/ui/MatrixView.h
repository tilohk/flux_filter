#pragma once

#include "FluxLookAndFeel.h"
#include "PluginProcessor.h"
#include "params/Parameters.h"

namespace flux::ui
{

// One matrix slot as a table row: # · Source · Target · Depth · Curve · Flux · Via · live amount.
class MatrixRow : public juce::Component, private juce::Timer
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    MatrixRow (FluxFilterAudioProcessor& p, int slotIndex) : processor (p), index (slotIndex)
    {
        auto& s = p.apvts;
        auto setupCombo = [&] (juce::ComboBox& box, const char* what, std::unique_ptr<APVTS::ComboBoxAttachment>& att) {
            auto* param = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (params::id::slot (index, what)));
            box.addItemList (param->choices, 1);
            addAndMakeVisible (box);
            att = std::make_unique<APVTS::ComboBoxAttachment> (s, params::id::slot (index, what), box);
        };
        setupCombo (source, "Source", sourceAtt);
        setupCombo (target, "Target", targetAtt);
        setupCombo (curve, "Curve", curveAtt);
        setupCombo (via, "Via", viaAtt);

        depth.setSliderStyle (juce::Slider::LinearHorizontal);
        depth.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        depth.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        addAndMakeVisible (depth);
        depthAtt = std::make_unique<APVTS::SliderAttachment> (s, params::id::slot (index, "Depth"), depth);

        flux.setTooltip ("Flux: the movement travels through the field (cell targets only)");
        addAndMakeVisible (flux);
        fluxAtt = std::make_unique<APVTS::ButtonAttachment> (s, params::id::slot (index, "Flux"), flux);

        startTimerHz (30);
    }

    // Column layout shared with the header.
    static std::array<int, 8> columns (int width)
    {
        const int fixed = 28 + 92 + 40 + 70;   // #, curve, flux, amount
        const int rest = juce::jmax (300, width - fixed);
        return { 28, rest * 22 / 100, rest * 28 / 100, rest * 26 / 100, 92, 40, rest * 24 / 100, 70 };
    }

    void paint (juce::Graphics& g) override
    {
        const auto cols = columns (getWidth());
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String (index + 1), 0, 0, cols[0], getHeight(), juce::Justification::centred);

        // Live amount (what this slot adds right now), bipolar bar.
        int x = 0; for (int i = 0; i < 7; ++i) x += cols[(size_t) i];
        auto bar = juce::Rectangle<float> ((float) x + 6.0f, (float) getHeight() * 0.5f - 3.0f, (float) cols[7] - 12.0f, 6.0f);
        g.setColour (colours::track);
        g.fillRoundedRectangle (bar, 3.0f);
        const float v = juce::jlimit (-1.0f, 1.0f, amount);
        const float a = bar.getCentreX(), b = bar.getCentreX() + v * bar.getWidth() * 0.5f;
        g.setColour (active ? colours::accent : colours::dim);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (a, b), bar.getY(), juce::jmax (2.0f, std::abs (b - a)), bar.getHeight()), 3.0f);
    }

    void resized() override
    {
        const auto cols = columns (getWidth());
        auto r = getLocalBounds().reduced (0, 3);
        r.removeFromLeft (cols[0]);
        source.setBounds (r.removeFromLeft (cols[1]).reduced (2, 0));
        target.setBounds (r.removeFromLeft (cols[2]).reduced (2, 0));
        depth.setBounds (r.removeFromLeft (cols[3]).reduced (4, 0));
        curve.setBounds (r.removeFromLeft (cols[4]).reduced (2, 0));
        flux.setBounds (r.removeFromLeft (cols[5]).reduced (8, 0));
        via.setBounds (r.removeFromLeft (cols[6]).reduced (2, 0));
    }

private:
    void timerCallback() override
    {
        const auto src = (ModSource) (source.getSelectedId() - 1);
        const auto tgt = (ModTarget) (target.getSelectedId() - 1);
        const auto viaSrc = (ModSource) (via.getSelectedId() - 1);
        active = src != ModSource::None && tgt != ModTarget::None && depth.getValue() != 0.0;

        float v = 0.0f;
        if (active)
        {
            v = (float) depth.getValue() * ModMatrix<FluxField::maxCells>::applyCurve ((ModCurve) (curve.getSelectedId() - 1),
                                                                                      processor.getModSourceValue (src));
            if (viaSrc != ModSource::None)
                v *= processor.getModSourceValue (viaSrc);
        }
        flux.setEnabled (isCellTarget (tgt));
        if (std::abs (v - amount) > 0.002f || active != wasActive) { amount = v; wasActive = active; repaint(); }
    }

    FluxFilterAudioProcessor& processor;
    int index;
    juce::ComboBox source, target, curve, via;
    juce::Slider depth;
    juce::ToggleButton flux;
    std::unique_ptr<APVTS::ComboBoxAttachment> sourceAtt, targetAtt, curveAtt, viaAtt;
    std::unique_ptr<APVTS::SliderAttachment> depthAtt;
    std::unique_ptr<APVTS::ButtonAttachment> fluxAtt;
    float amount = 0.0f;
    bool active = false, wasActive = false;
};

class MatrixView : public juce::Component
{
public:
    explicit MatrixView (FluxFilterAudioProcessor& p)
    {
        for (int i = 0; i < ModSettings::numSlots; ++i)
            addAndMakeVisible (*rows.emplace_back (std::make_unique<MatrixRow> (p, i)));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

        const auto cols = MatrixRow::columns (getWidth() - 2 * pad);
        const char* names[] = { "#", "Source", "Target", "Depth", "Curve", "Flux", "Via", "Amount" };
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        int x = pad;
        for (size_t i = 0; i < cols.size(); ++i)
        {
            g.drawText (names[i], x + 2, pad, cols[i] - 4, headerH, i == 0 ? juce::Justification::centred : juce::Justification::centredLeft);
            x += cols[i];
        }

        // Zebra stripes so rows are easy to follow across the table.
        g.setColour (colours::background.withAlpha (0.45f));
        for (size_t i = 1; i < rows.size(); i += 2)
            g.fillRect (rows[i]->getBounds().withX (pad).withWidth (getWidth() - 2 * pad));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad);
        r.removeFromTop (headerH);
        const int rowH = juce::jmin (30, r.getHeight() / (int) rows.size());
        for (auto& row : rows)
            row->setBounds (r.removeFromTop (rowH));
    }

private:
    static constexpr int pad = 10, headerH = 22;
    std::vector<std::unique_ptr<MatrixRow>> rows;
};

} // namespace flux::ui
