#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "dsp/FluxField.h"
#include "params/ParamSpec.h"

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

    // Drawable LFO shapes (used when an LFO's shape is "Custom"). Any non-audio thread.
    flux::CurveShape getCustomShape (int lfoIndex) const;
    void setCustomShape (int lfoIndex, const flux::CurveShape& shape);

    // Live data for the UI (lock-free, any thread).
    const flux::FluxField::Snapshot& getFieldSnapshot() const noexcept { return field.getSnapshot(); }
    float getModSourceValue (flux::ModSource s) const noexcept { return field.getMatrix().publishedSource (s); }

    juce::AudioProcessorValueTreeState apvts;

private:
    flux::FieldSettings readSettings() const noexcept;
    flux::ModSettings readModSettings() const noexcept;
    float value (flux::spec::FP p) const noexcept { return floatParams[(size_t) p]->load(); }

    flux::FluxField field;

    // Cached raw parameter pointers (read on the audio thread without lookups).
    std::array<std::atomic<float>*, (size_t) flux::spec::FP::Count> floatParams {};
    std::atomic<float>* pDensity = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pCharacter = nullptr;
    std::atomic<float>* pAutoGain = nullptr;
    std::atomic<float>* pLimiter = nullptr;
    std::atomic<float>* pFluxShape = nullptr;
    std::atomic<float>* pDirection = nullptr;
    std::atomic<float>* pDriftMode = nullptr;

    struct LfoParams { std::atomic<float>* sync; std::atomic<float>* division; std::atomic<float>* shape; std::atomic<float>* polarity; };
    struct SlotParams { std::atomic<float>* source; std::atomic<float>* target; std::atomic<float>* depth;
                        std::atomic<float>* curve; std::atomic<float>* flux; std::atomic<float>* via; };
    std::array<LfoParams, flux::ModSettings::numLfos> lfoParams {};
    std::array<SlotParams, flux::ModSettings::numSlots> slotParams {};

    mutable juce::CriticalSection shapeLock;
    std::array<flux::CurveShape, flux::ModSettings::numLfos> customShapes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluxFilterAudioProcessor)
};
