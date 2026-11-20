#include "PluginEditor.h"

FluxFilterAudioProcessorEditor::FluxFilterAudioProcessorEditor (FluxFilterAudioProcessor& p)
    : AudioProcessorEditor (&p), genericEditor (p)
{
    addAndMakeVisible (genericEditor);
    setSize (520, 440);
}

FluxFilterAudioProcessorEditor::~FluxFilterAudioProcessorEditor() = default;

void FluxFilterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (18.0f);
    g.drawText (JucePlugin_Name, getLocalBounds().removeFromTop (32), juce::Justification::centred);
}

void FluxFilterAudioProcessorEditor::resized()
{
    genericEditor.setBounds (getLocalBounds().withTrimmedTop (32));
}
