#pragma once

#include "PluginProcessor.h"
#include "ui/FieldView.h"
#include "ui/FluxLookAndFeel.h"
#include "ui/ModContext.h"
#include "ui/ParamControl.h"
#include "ui/PresetBar.h"
#include "ui/Sections.h"
#include "ui/SourceModules.h"

// One panel: header · field display with output spectrum + Output · Cells | Flux | Character ·
// LFO 1-3 (drag their move icons onto knobs) + the selected knob's modulation.
// The panel is laid out at a fixed design size and scaled as a whole when the window is resized.
class FluxFilterAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer
{
public:
    explicit FluxFilterAudioProcessorEditor (FluxFilterAudioProcessor&);
    ~FluxFilterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 880, designHeight = 756;

private:
   #if JUCE_DEBUG
    void writeSnapshot (const juce::File& dir);
   #endif

    class Panel : public juce::Component
    {
    public:
        explicit Panel (FluxFilterAudioProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        flux::ui::ModContext ctx;
    private:
        flux::ui::PresetBar presetBar;
        flux::ui::FieldView fieldView;
        flux::ui::ParamSection cells { "Shape", 3 }, character { "Character", 2 };
        flux::ui::FluxSection flux;
        flux::ui::OutputSection output;
        std::vector<std::unique_ptr<flux::ui::LfoModule>> lfos;
        flux::ui::ModPanel modPanel;
        flux::ui::FluxAura aura { flux.getFluxSlider() };   // on top of everything, takes no clicks
    };

    flux::ui::FluxLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this };
    Panel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluxFilterAudioProcessorEditor)
};
