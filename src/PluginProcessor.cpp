#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/Parameters.h"

using namespace flux;
using spec::FP;

namespace
{
    const juce::Identifier shapesTag ("LfoShapes"), shapeTag ("Shape"), indexAttr ("index"), pointsAttr ("points");
}

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
    pScale     = get (params::id::scale);
    pCharacter = get (params::id::character);
    pAutoGain  = get (params::id::autoGain);
    pLimiter   = get (params::id::limiter);
    pFluxShape = get (params::id::fluxShape);
    pDirection = get (params::id::direction);
    pDriftMode = get (params::id::driftMode);

    for (int i = 0; i < ModSettings::numLfos; ++i)
        lfoParams[(size_t) i] = { get (params::id::lfo (i, "Sync")), get (params::id::lfo (i, "Division")),
                                  get (params::id::lfo (i, "Shape")), get (params::id::lfo (i, "Polarity")) };

    for (int i = 0; i < ModSettings::numSlots; ++i)
        slotParams[(size_t) i] = { get (params::id::slot (i, "Source")), get (params::id::slot (i, "Target")),
                                   get (params::id::slot (i, "Depth")), get (params::id::slot (i, "Curve")),
                                   get (params::id::slot (i, "Flux")), get (params::id::slot (i, "Via")) };

    for (int i = 0; i < ModSettings::numLfos; ++i)
        setCustomShape (i, CurveShape::defaultShape());
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

    // prepare() resets the LFOs to the default table; hand them the current shapes again.
    const juce::ScopedLock sl (shapeLock);
    for (int i = 0; i < ModSettings::numLfos; ++i)
        field.setCustomShape (i, customShapes[(size_t) i]);
}

void FluxFilterAudioProcessor::releaseResources() {}

FieldSettings FluxFilterAudioProcessor::readSettings() const noexcept
{
    FieldSettings s;
    s.density   = (int) pDensity->load();
    s.centerHz  = value (FP::Center);
    s.spread    = value (FP::Spread);
    s.scale     = (CellScale) (int) pScale->load();
    s.tiltDbOct = value (FP::Tilt);
    s.resonance = value (FP::Resonance);
    s.morph     = value (FP::Morph);
    s.combine   = value (FP::Combine);
    s.width     = value (FP::Width);
    s.drift     = value (FP::Drift);
    s.character = (Character) (int) pCharacter->load();
    s.feedback  = value (FP::Feedback);
    s.damping   = value (FP::Damping);
    s.autoGain  = pAutoGain->load() > 0.5f;
    s.limiter   = pLimiter->load() > 0.5f;
    s.fluxRateHz = value (FP::FluxRate);
    s.fluxShape  = (LfoShape) (int) pFluxShape->load();
    s.fluxDepth  = value (FP::FluxDepth);
    s.fluxLevel  = value (FP::FluxLevel);
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
    const FP rate[]  = { FP::Lfo1Rate, FP::Lfo2Rate, FP::Lfo3Rate };
    const FP phase[] = { FP::Lfo1Phase, FP::Lfo2Phase, FP::Lfo3Phase };
    for (int i = 0; i < ModSettings::numLfos; ++i)
    {
        const auto& p = lfoParams[(size_t) i];
        auto& l = m.lfo[(size_t) i];
        l.rateHz      = value (rate[i]);
        l.phaseOffset = value (phase[i]);
        l.sync        = p.sync->load() > 0.5f;
        l.division    = (int) p.division->load();
        l.shape       = (ModLfoShape) (int) p.shape->load();
        l.unipolar    = p.polarity->load() > 0.5f;
    }
    m.envAttackMs  = value (FP::EnvAttack);
    m.envReleaseMs = value (FP::EnvRelease);
    m.envSensDb    = value (FP::EnvSens);
    m.chaosSpeed   = value (FP::ChaosSpeed);
    m.chaosShape   = value (FP::ChaosShape);
    m.driftRateHz  = value (FP::DriftRate);
    m.driftSlew    = value (FP::DriftSlew);
    m.driftSampleHold = pDriftMode->load() > 0.5f;
    m.macros = { value (FP::Macro1), value (FP::Macro2), value (FP::Macro3), value (FP::Macro4) };

    for (int i = 0; i < ModSettings::numSlots; ++i)
    {
        const auto& p = slotParams[(size_t) i];
        auto& s = m.slots[(size_t) i];
        s.source = (ModSource) (int) p.source->load();
        s.target = (ModTarget) (int) p.target->load();
        s.depth  = p.depth->load();
        s.curve  = (ModCurve) (int) p.curve->load();
        s.flux   = p.flux->load() > 0.5f;
        s.via    = (ModSource) (int) p.via->load();
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

CurveShape FluxFilterAudioProcessor::getCustomShape (int lfoIndex) const
{
    const juce::ScopedLock sl (shapeLock);
    return customShapes[(size_t) juce::jlimit (0, ModSettings::numLfos - 1, lfoIndex)];
}

void FluxFilterAudioProcessor::setCustomShape (int lfoIndex, const CurveShape& shape)
{
    if (lfoIndex < 0 || lfoIndex >= ModSettings::numLfos)
        return;
    CurveShape s = shape;
    s.sanitise();
    {
        const juce::ScopedLock sl (shapeLock);
        customShapes[(size_t) lfoIndex] = s;
    }
    field.setCustomShape (lfoIndex, s);
}

void FluxFilterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    juce::ValueTree shapes (shapesTag);
    {
        const juce::ScopedLock sl (shapeLock);
        for (int i = 0; i < ModSettings::numLfos; ++i)
            shapes.appendChild (juce::ValueTree (shapeTag, { { indexAttr, i },
                                                             { pointsAttr, juce::String (customShapes[(size_t) i].toString()) } }),
                                nullptr);
    }
    state.removeChild (state.getChildWithName (shapesTag), nullptr);
    state.appendChild (shapes, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FluxFilterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto shapes = state.getChildWithName (shapesTag);
    for (int i = 0; i < ModSettings::numLfos; ++i)
        setCustomShape (i, CurveShape::defaultShape());     // older states have no shapes
    for (const auto& s : shapes)
        setCustomShape ((int) s.getProperty (indexAttr, -1),
                        CurveShape::fromString (s.getProperty (pointsAttr).toString().toStdString()));

    state.removeChild (shapes, nullptr);
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FluxFilterAudioProcessor();
}
