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
        [[maybe_unused]] constexpr float a = 0, e = 1, i = 2, o = 3, u = 4, nasal = 5, growl = 6, schwa = 7, whisper = 8;   // Vowel ring
        constexpr float div8 = 10;                                       // 1/8 in lfoDivisions
        constexpr float lfoSquare = 3;

        static const std::vector<Factory> list {
            { "Init", {} },
            { "Breathing Vowels", join ({ { "density", 9 }, { "vowel", e }, { "resonance", 0.75f },
                                          { "flux", 0.35f }, { "drift", 0.15f }, { "wander", 0.50f }, { "fluxShape", sine },
                                          { lfo (0, "Rate"), 0.2f }, { lfo (0, "Flux"), 1.0f } },
                                        slot (0, ModSource::Lfo1, ModTarget::Vowel, 0.15f)) },
            { "Slow Tide", { { "density", 12 }, { "vowel", o }, { "size", 0.5f }, { "resonance", 0.55f }, { "scatter", 0.2f },
                             { "flux", 0.6f }, { "drift", 0.07f }, { "wander", 0.95f }, { "viscosity", 0.7f },
                             { "character", warm }, { "feedback", 0.2f } } },
            { "Glass Rain", { { "density", 16 }, { "vowel", whisper }, { "size", -1.0f }, { "resonance", 0.9f }, { "scatter", 0.6f },
                              { "flux", 0.5f }, { "drift", 2.5f }, { "fluxShape", random }, { "turbulence", 0.8f },
                              { "fluxStereo", 0.8f }, { "wander", 0.30f }, { "width", 0.8f }, { "output", 4 } } },
            { "Deep Body", { { "density", 6 }, { "vowel", growl }, { "size", 1.5f }, { "resonance", 0.85f },
                             { "drive", 6 }, { "character", warm }, { "feedback", 0.35f }, { "damping", 0.6f }, { "flux", 0.15f } } },
            { "Broken Radio", join ({ { "character", broken }, { "density", 6 }, { "vowel", nasal }, { "size", -0.5f }, { "resonance", 0.8f },
                                      { "drive", 12 }, { "flux", 0.4f }, { "fluxShape", ramp }, { "drift", 1.5f },
                                      { lfo (1, "Sync"), 1 }, { lfo (1, "Division"), div8 }, { lfo (1, "Shape"), lfoSquare } },
                                    slot (0, ModSource::Lfo2, ModTarget::Resonance, 0.2f)) },
            { "Singing Drone", { { "feedback", 0.92f }, { "damping", 0.4f }, { "resonance", 0.9f }, { "density", 8 }, { "vowel", schwa },
                                 { "size", 0.8f }, { "flux", 0.2f }, { "drift", 0.05f }, { "mix", 0.8f } } },
            { "Ripple Sweep", { { "direction", outward }, { "wavelength", 1.0f }, { "wander", 0.35f }, { "flux", 0.7f }, { "drift", 0.4f },
                                { "density", 10 }, { "vowel", i }, { "scatter", 0.3f }, { "resonance", 0.7f } } },
            { "Vowel Glide", join ({ { "vowel", u }, { "density", 8 }, { "resonance", 0.5f },
                                      { "flux", 0.5f }, { "drift", 0.25f }, { "wander", 0.28f }, { "fluxShape", triangle },
                                      { lfo (0, "Rate"), 0.12f }, { lfo (0, "Flux"), 0.0f } },
                                    slot (0, ModSource::Lfo1, ModTarget::Vowel, 0.25f)) },
            { "Tempo Pulse", join ({ { lfo (0, "Sync"), 1 }, { lfo (0, "Division"), div8 }, { lfo (0, "Shape"), lfoSquare },
                                     { lfo (0, "Flux"), 1.0f }, { "wander", 0.26f }, { "density", 10 }, { "resonance", 0.8f },
                                     { "flux", 0.2f }, { "direction", up }, { "character", clean } },
                                   slot (0, ModSource::Lfo1, ModTarget::Vowel, 0.2f)) },
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
