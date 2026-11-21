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
        [[maybe_unused]] constexpr float a = 0, e = 1, i = 2, o = 3, u = 4, nasal = 5, growl = 6, schwa = 7, whisper = 8;   // Vowel ring
        constexpr float div8 = 10, bar1 = 2;                             // lfoDivisions
        constexpr float lfoSquare = 3, lfoRandom = 4;

        // Factory set: each preset shows one side of the living formant field. Flux sets how alive
        // (drift speed, lifespan, coupling, what feeds the cells); Wander and Rasp add to it.
        static const std::vector<Factory> list {
            { "Init", {} },

            // A calm throat that breathes on its own: slow drift and a little life.
            { "Breathing Throat", { { "density", 9 }, { "vowel", a }, { "resonance", 0.6f },
                                    { "flux", 0.35f }, { "wander", 0.6f } } },

            // Airy high formants, many cells, lively.
            { "Whisper Choir", { { "output", 1 }, { "density", 16 }, { "vowel", whisper }, { "resonance", 0.7f }, { "scatter", 0.15f },
                                 { "flux", 0.6f }, { "wander", 0.5f } } },

            // Big, low, rough: the growl shape with full rasp and some drive.
            { "Growling Beast", { { "density", 9 }, { "vowel", growl }, { "shift", -1.2f }, { "resonance", 0.75f },
                                  { "flux", 0.7f }, { "rasp", 0.9f }, { "wander", 0.4f },
                                  { "character", warm }, { "drive", 6 } } },

            // Everything on: short lives, cells exciting each other, fast drift. Made for drums.
            { "Feeding Swarm", { { "density", 16 }, { "vowel", schwa }, { "resonance", 0.8f }, { "scatter", 0.5f },
                                 { "flux", 0.9f }, { "wander", 0.3f } } },

            // Scattered cells at full Flux: granular flicker.
            { "Flicker Grain", { { "output", 1 }, { "density", 16 }, { "vowel", e }, { "resonance", 0.85f }, { "scatter", 0.7f },
                                 { "flux", 1.0f }, { "wander", 0.4f } } },

            // Narrow formants ringing into each other through the feedback loop.
            { "Singing Stones", { { "output", 2 }, { "density", 12 }, { "vowel", o }, { "shift", -0.5f }, { "resonance", 0.92f },
                                  { "flux", 0.45f }, { "wander", 0.5f },
                                  { "feedback", 0.6f }, { "damping", 0.5f } } },

            // A slowly moving vowel under the dry sound.
            { "Vowel Shadow", { { "density", 12 }, { "vowel", schwa }, { "resonance", 0.85f },
                                { "flux", 0.35f }, { "wander", 0.5f }} },

            // A huge, dark throat that moves very slowly.
            { "Cave Mouth", { { "output", 2 }, { "density", 6 }, { "vowel", u }, { "shift", -2.0f }, { "resonance", 0.7f },
                              { "flux", 0.3f }, { "wander", 0.9f },
                              { "feedback", 0.4f }, { "damping", 0.7f }, { "character", warm } } },

            // A tiny, nervous throat.
            { "Tiny Creature", { { "output", 2 }, { "density", 9 }, { "vowel", i }, { "shift", 1.8f }, { "resonance", 0.7f },
                                 { "flux", 0.85f }, { "rasp", 0.5f }, { "wander", 0.7f } } },

            // Long swells through the whole vowel ring, an LFO absorbed loosely by the cells.
            { "Slow Tide", join ({ { "density", 12 }, { "vowel", o }, { "resonance", 0.55f }, { "scatter", 0.1f },
                                   { "flux", 0.5f }, { "wander", 1.0f },
                                   { lfo (0, "Rate"), 0.05f }, { lfo (0, "Flux"), 1.0f }, { "character", warm } },
                                 slot (0, ModSource::Lfo1, ModTarget::Vowel, 0.1f)) },

            // Broken character, nasal throat, rasp and a tempo-synced square on Resonance.
            { "Broken Radio", join ({ { "character", broken }, { "density", 6 }, { "vowel", nasal }, { "shift", 0.5f },
                                      { "resonance", 0.8f }, { "rasp", 0.6f }, { "drive", 12 }, { "flux", 0.5f },
                                      { lfo (1, "Sync"), 1 }, { lfo (1, "Division"), div8 }, { lfo (1, "Shape"), lfoSquare } },
                                    slot (0, ModSource::Lfo2, ModTarget::Resonance, 0.2f)) },

            // Vowels stepping in time: a synced random LFO on Vowel, half absorbed.
            { "Vowel Sequencer", join ({ { "density", 9 }, { "resonance", 0.75f }, { "character", clean },
                                         { "flux", 0.3f }, { "wander", 0.2f },
                                         { lfo (0, "Sync"), 1 }, { lfo (0, "Division"), div8 }, { lfo (0, "Shape"), lfoRandom },
                                         { lfo (0, "Flux"), 0.5f } },
                                       slot (0, ModSource::Lfo1, ModTarget::Vowel, 0.35f)) },

            // Self-oscillating drone, slowly alive.
            { "Drone Organism", join ({ { "density", 12 }, { "vowel", schwa }, { "shift", -0.8f }, { "resonance", 0.9f },
                                        { "feedback", 0.93f }, { "damping", 0.4f },
                                        { "flux", 0.3f }, { "wander", 0.6f },
                                        { lfo (1, "Sync"), 1 }, { lfo (1, "Division"), bar1 } },
                                      slot (0, ModSource::Lfo2, ModTarget::Scatter, 0.15f)) },
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
