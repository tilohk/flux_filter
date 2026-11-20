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

    // Live data for the UI (lock-free, any thread).
    const flux::FluxField::Snapshot& getFieldSnapshot() const noexcept { return field.getSnapshot(); }
    float getModSourceValue (flux::ModSource s) const noexcept { return field.getMatrix().publishedSource (s); }
    float getSampleRateForUi() const noexcept { return uiSampleRate.load (std::memory_order_relaxed); }

    // Output meters: highest peak since the last call (resets it). clip = went over 0 dBFS before the limiter.
    float takeOutputPeak (int ch) noexcept { return outPeak[(size_t) ch].exchange (0.0f, std::memory_order_relaxed); }
    bool takeClip (int ch) noexcept { return clipped[(size_t) ch].exchange (false, std::memory_order_relaxed); }

    // Output spectrum: mono output samples for the UI's analyser (single reader). Returns how many were copied.
    int pullAnalyserSamples (float* dest, int maxSamples) noexcept;

   #if JUCE_DEBUG
    // Dev aid for UI snapshots: feed the analyser without audio running (message thread only).
    void debugPushAnalyserSamples (const float* src, int n) noexcept
    {
        const auto scope = spectrumFifo.write (n);
        std::copy_n (src, scope.blockSize1, spectrumBuffer.data() + scope.startIndex1);
        std::copy_n (src + scope.blockSize1, scope.blockSize2, spectrumBuffer.data() + scope.startIndex2);
    }
   #endif

    juce::AudioProcessorValueTreeState apvts;

private:
    flux::FieldSettings readSettings() const noexcept;
    flux::ModSettings readModSettings() const noexcept;
    float value (flux::spec::FP p) const noexcept { return floatParams[(size_t) p]->load(); }

    flux::FluxField field;

    // Cached raw parameter pointers (read on the audio thread without lookups).
    std::array<std::atomic<float>*, (size_t) flux::spec::FP::Count> floatParams {};
    std::atomic<float>* pDensity = nullptr;
    std::atomic<float>* pCharacter = nullptr;
    std::atomic<float>* pAutoGain = nullptr;
    std::atomic<float>* pLimiter = nullptr;
    std::atomic<float>* pFluxShape = nullptr;
    std::atomic<float>* pDirection = nullptr;

    struct LfoParams { std::atomic<float>* sync; std::atomic<float>* division; std::atomic<float>* shape;
                       std::atomic<float>* unipolar; std::atomic<float>* flux; };
    struct SlotParams { std::atomic<float>* source; std::atomic<float>* target; std::atomic<float>* depth; };
    std::array<LfoParams, flux::ModSettings::numLfos> lfoParams {};
    std::array<SlotParams, flux::ModSettings::numSlots> slotParams {};

    // UI feeds
    static constexpr int spectrumFifoSize = 8192;
    juce::AbstractFifo spectrumFifo { spectrumFifoSize };
    std::array<float, spectrumFifoSize> spectrumBuffer {};
    std::array<std::atomic<float>, 2> outPeak {};
    std::array<std::atomic<bool>, 2> clipped {};
    std::atomic<float> uiSampleRate { 48000.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluxFilterAudioProcessor)
};
