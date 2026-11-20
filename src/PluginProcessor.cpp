#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/Parameters.h"

using namespace flux;
using spec::FP;

FluxFilterAudioProcessor::FluxFilterAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FluxFilterState", params::createLayout())
{
    auto get = [this] (const juce::String& id) {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (size_t i = 0; i < floatParams.size(); ++i)
        floatParams[i] = get (spec::floats()[i].id);

    pDensity   = get (params::id::density);
    pCharacter = get (params::id::character);
    pAutoGain  = get (params::id::autoGain);
    pLimiter   = get (params::id::limiter);
    pFluxShape = get (params::id::fluxShape);
    pDirection = get (params::id::direction);

    for (int i = 0; i < ModSettings::numLfos; ++i)
        lfoParams[(size_t) i] = { get (params::id::lfo (i, "Sync")), get (params::id::lfo (i, "Division")),
                                  get (params::id::lfo (i, "Shape")), get (params::id::lfo (i, "Unipolar")),
                                  get (params::id::lfo (i, "Flux")) };

    for (int i = 0; i < ModSettings::numSlots; ++i)
        slotParams[(size_t) i] = { get (params::id::slot (i, "Source")), get (params::id::slot (i, "Target")),
                                   get (params::id::slot (i, "Depth")) };
}

FluxFilterAudioProcessor::~FluxFilterAudioProcessor() = default;

bool FluxFilterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void FluxFilterAudioProcessor::prepareToPlay (double sampleRate, int)
{
    field.setSettings (readSettings());
    field.setModSettings (readModSettings());
    field.prepare (sampleRate);
    uiSampleRate.store ((float) sampleRate, std::memory_order_relaxed);
    spectrumFifo.reset();
}

void FluxFilterAudioProcessor::releaseResources() {}

FieldSettings FluxFilterAudioProcessor::readSettings() const noexcept
{
    FieldSettings s;
    s.density   = (int) pDensity->load();
    s.centerHz  = value (FP::Center);
    s.spread    = value (FP::Spread);
    s.scale     = CellScale::Log;
    s.tiltDbOct = 0.0f;   // tilt and serial blend are not exposed (cells form a flat, parallel bank)
    s.resonance = value (FP::Resonance);
    s.morph     = value (FP::Morph);
    s.combine   = 0.0f;
    s.width     = value (FP::Width);
    s.character = (Character) (int) pCharacter->load();
    s.feedback  = value (FP::Feedback);
    s.damping   = value (FP::Damping);
    s.autoGain  = pAutoGain->load() > 0.5f;
    s.limiter   = pLimiter->load() > 0.5f;
    s.fluxRateHz = value (FP::FluxRate);
    s.fluxShape  = (LfoShape) (int) pFluxShape->load();
    s.fluxAmount = value (FP::FluxAmount);
    s.propagation.travelSec  = value (FP::Travel);
    s.propagation.direction  = (FluxDirection) (int) pDirection->load();
    s.propagation.viscosity  = value (FP::Viscosity);
    s.propagation.wavelength = value (FP::Wavelength);
    s.propagation.turbulence = value (FP::Turbulence);
    s.propagation.stereo     = value (FP::FluxStereo);
    s.driveDb   = value (FP::Drive);
    s.mix       = value (FP::Mix);
    s.outputDb  = value (FP::Output);
    return s;
}

ModSettings FluxFilterAudioProcessor::readModSettings() const noexcept
{
    ModSettings m;
    const FP rate[] = { FP::Lfo1Rate, FP::Lfo2Rate, FP::Lfo3Rate };
    for (int i = 0; i < ModSettings::numLfos; ++i)
    {
        const auto& p = lfoParams[(size_t) i];
        auto& l = m.lfo[(size_t) i];
        l.rateHz   = value (rate[i]);
        l.sync     = p.sync->load() > 0.5f;
        l.division = (int) p.division->load();
        l.shape    = (ModLfoShape) (int) p.shape->load();
        l.unipolar = p.unipolar->load() > 0.5f;
        l.flux     = p.flux->load();
    }

    for (int i = 0; i < ModSettings::numSlots; ++i)
    {
        const auto& p = slotParams[(size_t) i];
        auto& s = m.slots[(size_t) i];
        s.source = (ModSource) (int) p.source->load();
        s.target = (ModTarget) (int) p.target->load();
        s.depth  = p.depth->load();
    }
    return m;
}

void FluxFilterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    Transport t;
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
        {
            t.bpm = pos->getBpm().orFallback (120.0);
            t.ppq = pos->getPpqPosition().orFallback (0.0);
            t.playing = pos->getIsPlaying();
        }

    field.setSettings (readSettings());
    field.setModSettings (readModSettings());
    field.setTransport (t);

    const int n = buffer.getNumSamples();
    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    field.process (left, right, n);

    // Filtered output for the analyser (mono). If the UI isn't reading, the FIFO fills up and we just skip.
    {
        const auto scope = spectrumFifo.write (n);
        auto copy = [&] (int start, int count, int offset) {
            for (int i = 0; i < count; ++i)
                spectrumBuffer[(size_t) (start + i)] = right != nullptr ? 0.5f * (left[offset + i] + right[offset + i]) : left[offset + i];
        };
        copy (scope.startIndex1, scope.blockSize1, 0);
        copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        const int src = right != nullptr ? ch : 0;
        const float peak = field.getPeakOut (src);
        if (peak > outPeak[(size_t) ch].load (std::memory_order_relaxed))
            outPeak[(size_t) ch].store (peak, std::memory_order_relaxed);
        if (field.getPeakBeforeLimiter (src) > 1.0f)
            clipped[(size_t) ch].store (true, std::memory_order_relaxed);
    }
}

int FluxFilterAudioProcessor::pullAnalyserSamples (float* dest, int maxSamples) noexcept
{
    const auto scope = spectrumFifo.read (juce::jmin (maxSamples, spectrumFifo.getNumReady()));
    std::copy_n (spectrumBuffer.data() + scope.startIndex1, scope.blockSize1, dest);
    std::copy_n (spectrumBuffer.data() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
    return scope.blockSize1 + scope.blockSize2;
}

juce::AudioProcessorEditor* FluxFilterAudioProcessor::createEditor()
{
    return new FluxFilterAudioProcessorEditor (*this);
}

bool FluxFilterAudioProcessor::hasEditor() const { return true; }

const juce::String FluxFilterAudioProcessor::getName() const { return JucePlugin_Name; }

void FluxFilterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FluxFilterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    state.removeChild (state.getChildWithName ("LfoShapes"), nullptr);   // drawn shapes from older versions
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FluxFilterAudioProcessor();
}
