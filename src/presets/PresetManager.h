#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "params/Parameters.h"

namespace flux
{

// Factory presets (built in) and user presets (XML files in ~/Library/Audio/Presets/<company>/<product>).
// A preset is the full parameter state. Factory presets list only what differs from the defaults.
// Everything here runs on the message thread.
class PresetManager
{
public:
    struct Factory
    {
        const char* name;
        std::vector<std::pair<juce::String, float>> values;   // parameter ID -> plain value (choice: index, bool: 0/1)
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : state (s) { scanUserPresets(); }

    static const std::vector<Factory>& factory()
    {
        using namespace params;
        auto lfo = [] (int i, const char* what) { return id::lfo (i, what); };
        auto slot = [] (int i, ModSource src, ModTarget tgt, float depth) {
            return std::vector<std::pair<juce::String, float>> {
                { id::slot (i, "Source"), (float) (int) src }, { id::slot (i, "Target"), (float) (int) tgt }, { id::slot (i, "Depth"), depth } };
        };
        auto join = [] (std::vector<std::pair<juce::String, float>> a, const std::vector<std::pair<juce::String, float>>& b) {
            a.insert (a.end(), b.begin(), b.end());
            return a;
        };
        constexpr float clean = 0, warm = 1, broken = 2;                 // Character
        constexpr float sine = 0, triangle = 1, ramp = 2, random = 3;    // flux wave shape
        constexpr float up = 0, outward = 2;                             // Direction
        constexpr float lp = 0, bp = 1, notch = 3;                       // Morph positions
        constexpr float div8 = 10;                                       // 1/8 in lfoDivisions
        constexpr float lfoSquare = 3;

        static const std::vector<Factory> list {
            { "Init", {} },
            { "Breathing Vowels", join ({ { "density", 6 }, { "center", 700 }, { "spread", 3 }, { "resonance", 0.75f }, { "morph", bp },
                                          { "flux", 0.35f }, { "fluxRate", 0.15f }, { "travel", 1.2f }, { "fluxShape", sine },
                                          { lfo (0, "Rate"), 0.2f }, { lfo (0, "Flux"), 1.0f } },
                                        slot (0, ModSource::Lfo1, ModTarget::Cutoff, 0.15f)) },
            { "Slow Tide", { { "density", 12 }, { "center", 400 }, { "spread", 5 }, { "resonance", 0.55f }, { "morph", 0.6f },
                             { "flux", 0.6f }, { "fluxRate", 0.07f }, { "travel", 3.0f }, { "viscosity", 0.7f },
                             { "character", warm }, { "feedback", 0.2f } } },
            { "Glass Rain", { { "density", 16 }, { "center", 2500 }, { "spread", 4 }, { "resonance", 0.9f }, { "morph", bp },
                              { "flux", 0.5f }, { "fluxRate", 2.5f }, { "fluxShape", random }, { "turbulence", 0.8f },
                              { "fluxStereo", 0.8f }, { "travel", 0.4f }, { "width", 0.8f } } },
            { "Deep Body", { { "density", 4 }, { "center", 120 }, { "spread", 2.5f }, { "resonance", 0.85f }, { "morph", lp },
                             { "drive", 6 }, { "character", warm }, { "feedback", 0.35f }, { "damping", 0.6f }, { "flux", 0.15f } } },
            { "Broken Radio", join ({ { "character", broken }, { "density", 6 }, { "center", 1500 }, { "spread", 2 }, { "resonance", 0.8f },
                                      { "drive", 12 }, { "flux", 0.4f }, { "fluxShape", ramp }, { "fluxRate", 1.5f },
                                      { lfo (1, "Sync"), 1 }, { lfo (1, "Division"), div8 }, { lfo (1, "Shape"), lfoSquare } },
                                    slot (0, ModSource::Lfo2, ModTarget::Resonance, 0.2f)) },
            { "Singing Drone", { { "feedback", 0.92f }, { "damping", 0.4f }, { "resonance", 0.9f }, { "density", 8 }, { "center", 220 },
                                 { "spread", 3 }, { "flux", 0.2f }, { "fluxRate", 0.05f }, { "mix", 0.8f } } },
            { "Ripple Sweep", { { "direction", outward }, { "wavelength", 1.0f }, { "travel", 0.6f }, { "flux", 0.7f }, { "fluxRate", 0.4f },
                                { "density", 10 }, { "center", 900 }, { "spread", 5 }, { "resonance", 0.7f }, { "morph", bp } } },
            { "Notch Phaser", join ({ { "morph", notch }, { "density", 8 }, { "center", 800 }, { "spread", 4 }, { "resonance", 0.5f },
                                      { "flux", 0.5f }, { "fluxRate", 0.25f }, { "travel", 0.3f }, { "fluxShape", triangle },
                                      { lfo (0, "Rate"), 0.12f }, { lfo (0, "Flux"), 0.0f } },
                                    slot (0, ModSource::Lfo1, ModTarget::Cutoff, 0.25f)) },
            { "Tempo Pulse", join ({ { lfo (0, "Sync"), 1 }, { lfo (0, "Division"), div8 }, { lfo (0, "Shape"), lfoSquare },
                                     { lfo (0, "Flux"), 1.0f }, { "travel", 0.25f }, { "density", 10 }, { "resonance", 0.8f },
                                     { "flux", 0.2f }, { "direction", up }, { "character", clean } },
                                   slot (0, ModSource::Lfo1, ModTarget::Cutoff, 0.2f)) },
        };
        return list;
    }

    int numFactory() const noexcept { return (int) factory().size(); }
    const juce::Array<juce::File>& userPresets() const noexcept { return userFiles; }

    // Index space: factory presets first, then user presets.
    int numPresets() const noexcept { return numFactory() + userFiles.size(); }
    juce::String nameOf (int index) const
    {
        if (index < numFactory()) return factory()[(size_t) index].name;
        return userFiles[index - numFactory()].getFileNameWithoutExtension();
    }

    int getCurrent() const noexcept { return current; }
    juce::String getCurrentName() const { return state.state.getProperty (nameProperty, "Init").toString(); }

    void load (int index)
    {
        if (index < 0 || index >= numPresets()) return;
        if (index < numFactory())
        {
            resetToDefaults();
            for (const auto& [paramId, value] : factory()[(size_t) index].values)
            {
                auto* p = state.getParameter (paramId);
                jassert (p != nullptr);   // a factory preset names a parameter that doesn't exist
                if (p == nullptr) continue;
                jassert (juce::approximatelyEqual (p->convertFrom0to1 (p->convertTo0to1 (value)), value)
                         || std::abs (p->convertFrom0to1 (p->convertTo0to1 (value)) - value) < 0.01f * (1.0f + std::abs (value)));   // out of range
                setPlain (*p, value);
            }
        }
        else if (auto xml = juce::XmlDocument::parse (userFiles[index - numFactory()]);
                 xml != nullptr && xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
        }
        current = index;
        state.state.setProperty (nameProperty, nameOf (index), nullptr);
    }

    // Saves the current state as a user preset (overwrites one with the same name). Returns its index.
    int save (const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName.trim());
        if (name.isEmpty()) return -1;
        folder().createDirectory();
        state.state.setProperty (nameProperty, name, nullptr);
        if (auto xml = state.copyState().createXml())
            xml->writeTo (folder().getChildFile (name + extension));
        scanUserPresets();
        for (int i = 0; i < userFiles.size(); ++i)
            if (userFiles[i].getFileNameWithoutExtension() == name)
                current = numFactory() + i;
        return current;
    }

    void scanUserPresets()
    {
        userFiles = folder().findChildFiles (juce::File::findFiles, false, "*" + juce::String (extension));
        userFiles.sort();
    }

    // After a project reload: find the preset index by its stored name (or -1 if it no longer exists).
    void syncFromState()
    {
        current = -1;
        const auto name = getCurrentName();
        for (int i = 0; i < numPresets(); ++i)
            if (nameOf (i) == name) { current = i; break; }
    }

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Presets").getChildFile (JucePlugin_Manufacturer).getChildFile (JucePlugin_Name);
    }

private:
    static constexpr const char* extension = ".fluxpreset";
    inline static const juce::Identifier nameProperty { "presetName" };

    void resetToDefaults()
    {
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rp->getDefaultValue());
    }

    static void setPlain (juce::RangedAudioParameter& p, float plain)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
        p.endChangeGesture();
    }

    juce::AudioProcessorValueTreeState& state;
    juce::Array<juce::File> userFiles;
    int current = 0;
};

} // namespace flux
