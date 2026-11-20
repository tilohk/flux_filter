#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/Parameters.h"

using namespace flux;

FluxFilterAudioProcessor::FluxFilterAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FluxFilterState", params::createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pDensity   = get (params::id::density);
    pCenter    = get (params::id::center);
    pSpread    = get (params::id::spread);
    pScale     = get (params::id::scale);
    pTilt      = get (params::id::tilt);
    pResonance = get (params::id::resonance);
    pMorph     = get (params::id::morph);
    pCombine   = get (params::id::combine);
    pWidth     = get (params::id::width);
    pDrift     = get (params::id::drift);
    pCharacter = get (params::id::character);
    pFeedback  = get (params::id::feedback);
    pDamping   = get (params::id::damping);
    pAutoGain  = get (params::id::autoGain);
    pLimiter   = get (params::id::limiter);
    pDrive     = get (params::id::drive);
    pMix       = get (params::id::mix);
    pOutput    = get (params::id::output);
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
    field.prepare (sampleRate);
}

void FluxFilterAudioProcessor::releaseResources() {}

FieldSettings FluxFilterAudioProcessor::readSettings() const noexcept
{
    FieldSettings s;
    s.density   = (int) pDensity->load();
    s.centerHz  = pCenter->load();
    s.spread    = pSpread->load();
    s.scale     = (CellScale) (int) pScale->load();
    s.tiltDbOct = pTilt->load();
    s.resonance = pResonance->load();
    s.morph     = pMorph->load();
    s.combine   = pCombine->load();
    s.width     = pWidth->load();
    s.drift     = pDrift->load();
    s.character = (Character) (int) pCharacter->load();
    s.feedback  = pFeedback->load();
    s.damping   = pDamping->load();
    s.autoGain  = pAutoGain->load() > 0.5f;
    s.limiter   = pLimiter->load() > 0.5f;
    s.driveDb   = pDrive->load();
    s.mix       = pMix->load();
    s.outputDb  = pOutput->load();
    return s;
}

void FluxFilterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    field.setSettings (readSettings());

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    field.process (left, right, buffer.getNumSamples());
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
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FluxFilterAudioProcessor();
}
