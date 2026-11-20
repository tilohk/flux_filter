#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "dsp/FluxField.h"

class FluxFilterAudioProcessor : public juce::AudioProcessor
{
public:
    FluxFilterAudioProcessor();
    ~FluxFilterAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; } // high resonance rings

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    flux::FieldSettings readSettings() const noexcept;

    flux::FluxField field;

    // Cached raw parameter pointers (read on the audio thread without lookups).
    std::atomic<float>* pDensity = nullptr;
    std::atomic<float>* pCenter = nullptr;
    std::atomic<float>* pSpread = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pTilt = nullptr;
    std::atomic<float>* pResonance = nullptr;
    std::atomic<float>* pMorph = nullptr;
    std::atomic<float>* pCombine = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pDrift = nullptr;
    std::atomic<float>* pCharacter = nullptr;
    std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pDamping = nullptr;
    std::atomic<float>* pAutoGain = nullptr;
    std::atomic<float>* pLimiter = nullptr;
    std::atomic<float>* pFluxRate = nullptr;
    std::atomic<float>* pFluxShape = nullptr;
    std::atomic<float>* pFluxDepth = nullptr;
    std::atomic<float>* pFluxLevel = nullptr;
    std::atomic<float>* pTravel = nullptr;
    std::atomic<float>* pDirection = nullptr;
    std::atomic<float>* pViscosity = nullptr;
    std::atomic<float>* pWavelength = nullptr;
    std::atomic<float>* pTurbulence = nullptr;
    std::atomic<float>* pFluxStereo = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluxFilterAudioProcessor)
};
