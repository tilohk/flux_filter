#pragma once
#include "PluginProcessor.h"

// Phase 1: a generic parameter list, so the sound can be tuned before the real UI exists (phase 5).
class FluxFilterAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit FluxFilterAudioProcessorEditor (FluxFilterAudioProcessor&);
    ~FluxFilterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::GenericAudioProcessorEditor genericEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluxFilterAudioProcessorEditor)
};
