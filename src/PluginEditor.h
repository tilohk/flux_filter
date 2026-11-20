#pragma once

#include "PluginProcessor.h"
#include "ui/FieldView.h"
#include "ui/FluxLookAndFeel.h"
#include "ui/MatrixView.h"
#include "ui/ModWidgets.h"
#include "ui/ParamControl.h"

// Layout: header · live field display (always visible) · tabs for Field, Modulation and Matrix.
class FluxFilterAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit FluxFilterAudioProcessorEditor (FluxFilterAudioProcessor&);
    ~FluxFilterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
   #if JUCE_DEBUG
    void writeSnapshots (const juce::File& dir);
   #endif

    // Lays out sections in columns; each column stacks its sections top to bottom.
    class SectionPage : public juce::Component
    {
    public:
        flux::ui::ParamSection& addSection (const juce::String& title, int column, float widthWeight = 1.0f);
        void resized() override;
    private:
        struct Entry { std::unique_ptr<flux::ui::ParamSection> section; int column; float weight; };
        std::vector<Entry> entries;
    };

    class ModPage : public juce::Component
    {
    public:
        explicit ModPage (FluxFilterAudioProcessor&);
        void resized() override;
    private:
        struct Stack : juce::Component   // vertical stack of extra widgets below a section's controls
        {
            std::vector<std::unique_ptr<juce::Component>> items;
            std::vector<int> heights;
            void add (std::unique_ptr<juce::Component> c, int h) { addAndMakeVisible (*c); items.push_back (std::move (c)); heights.push_back (h); }
            int total() const { int t = 0; for (int h : heights) t += h + 4; return t; }
            void resized() override
            {
                auto r = getLocalBounds();
                for (size_t i = 0; i < items.size(); ++i) { items[i]->setBounds (r.removeFromTop (heights[i])); r.removeFromTop (4); }
            }
        };
        std::vector<std::unique_ptr<flux::ui::ParamSection>> lfoSections, lowerSections;
        std::vector<std::unique_ptr<Stack>> stacks;
    };

    flux::ui::FluxLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this };

    flux::ui::FieldView fieldView;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    SectionPage fieldPage;
    ModPage modPage;
    flux::ui::MatrixView matrixView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluxFilterAudioProcessorEditor)
};
