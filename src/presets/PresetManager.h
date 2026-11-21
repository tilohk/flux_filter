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
        constexpr float scaleMode = 1, follow = 2;                       // Pitch anchor
        constexpr float keyA = 9, keyD = 2, pentatonic = 3, harmonic = 4;
        constexpr float independent = 1;                                 // Stereo life
        [[maybe_unused]] constexpr float a = 0, e = 1, i = 2, o = 3, u = 4, nasal = 5, growl = 6, schwa = 7, whisper = 8;   // Vowel ring
        constexpr float div8 = 10, bar1 = 2;                             // lfoDivisions
        constexpr float lfoSquare = 3, lfoRandom = 4;

        // v2 factory set: each preset shows one side of the living formant field.
        static const std::vector<Factory> list {
            { "Init", {} },

            // A calm throat that breathes on its own: drift and a little life.
            { "Breathing Throat", { { "density", 9 }, { "vowel", a }, { "resonance", 0.6f },
                                    { "flux", 0.4f }, { "drift", 0.15f }, { "wander", 0.6f }, { "coupling", 0.3f },
                                    { "feed", 0.3f }, { "lifespan", 4.0f } } },

            // Airy high formants, many cells, each channel living on its own.
            { "Whisper Choir", { { "output", 3 }, { "density", 16 }, { "vowel", whisper }, { "resonance", 0.7f }, { "scatter", 0.15f }, { "width", 0.8f },
                                 { "flux", 0.6f }, { "drift", 0.3f }, { "wander", 0.5f }, { "feed", 0.2f }, { "lifespan", 1.5f },
                                 { "stereoLife", independent } } },

            // Big, low, rough: the growl shape with full rasp and some drive.
            { "Growling Beast", { { "density", 9 }, { "vowel", growl }, { "size", 1.2f }, { "resonance", 0.75f },
                                  { "flux", 0.8f }, { "rasp", 0.9f }, { "drift", 0.4f }, { "wander", 0.4f },
                                  { "character", warm }, { "drive", 6 } } },

            // Lives off what you play: cells grow where the input has energy and starve elsewhere.
            // Made for drums and percussive material.
            { "Feeding Swarm", { { "density", 16 }, { "vowel", schwa }, { "resonance", 0.8f }, { "scatter", 0.5f },
                                 { "flux", 1.0f }, { "feed", 1.0f }, { "lifespan", 0.6f }, { "coupling", 0.7f },
                                 { "drift", 0.3f }, { "wander", 0.3f } } },

            // Short-lived scattered cells: granular flicker.
            { "Flicker Grain", { { "output", 3 }, { "density", 16 }, { "vowel", e }, { "resonance", 0.85f }, { "scatter", 0.7f }, { "width", 0.7f },
                                 { "flux", 1.0f }, { "feed", 0.0f }, { "lifespan", 0.08f }, { "drift", 2.0f }, { "wander", 0.4f },
                                 { "stereoLife", independent } } },

            // Narrow formants locked to A minor pentatonic, ringing into each other.
            { "Singing Stones", { { "output", 6 }, { "density", 12 }, { "vowel", o }, { "size", 0.5f }, { "resonance", 0.92f },
                                  { "flux", 0.5f }, { "drift", 0.1f }, { "wander", 0.5f }, { "coupling", 0.5f },
                                  { "feedback", 0.6f }, { "damping", 0.5f },
                                  { "anchor", scaleMode }, { "anchorKey", keyA }, { "anchorScale", pentatonic } } },

            // Follows the input's pitch and puts the cells on its harmonics, under the dry sound.
            { "Harmonic Shadow", { { "output", 2 }, { "density", 12 }, { "vowel", schwa }, { "resonance", 0.9f },
                                   { "flux", 0.4f }, { "wander", 0.3f }, { "drift", 0.2f }, { "anchor", follow }, { "mix", 0.7f } } },

            // A huge, dark throat that moves very slowly.
            { "Cave Mouth", { { "output", 6 }, { "density", 6 }, { "vowel", u }, { "size", 2.0f }, { "resonance", 0.7f },
                              { "flux", 0.5f }, { "drift", 0.05f }, { "wander", 0.8f },
                              { "feedback", 0.4f }, { "damping", 0.7f }, { "character", warm } } },

            // A tiny, nervous throat.
            { "Tiny Creature", { { "output", 4 }, { "density", 9 }, { "vowel", i }, { "size", -1.8f }, { "resonance", 0.7f },
                                 { "flux", 0.8f }, { "rasp", 0.5f }, { "drift", 1.5f }, { "wander", 0.7f },
                                 { "feed", 0.5f }, { "lifespan", 0.5f } } },

            // Long swells through the whole vowel ring, an LFO absorbed loosely by the cells.
            { "Slow Tide", join ({ { "density", 12 }, { "vowel", o }, { "resonance", 0.55f }, { "scatter", 0.1f },
                                   { "flux", 0.7f }, { "drift", 0.05f }, { "wander", 1.0f }, { "coupling", 0.6f },
                                   { lfo (0, "Rate"), 0.05f }, { lfo (0, "Flux"), 1.0f }, { "character", warm } },
                                 slot (0, ModSource::Lfo1, ModTarget::Vowel, 0.1f)) },

            // Broken character, nasal throat, rasp and a tempo-synced square on Resonance.
            { "Broken Radio", join ({ { "character", broken }, { "density", 6 }, { "vowel", nasal }, { "size", -0.5f },
                                      { "resonance", 0.8f }, { "rasp", 0.6f }, { "drive", 12 }, { "flux", 0.4f }, { "drift", 1.5f },
                                      { lfo (1, "Sync"), 1 }, { lfo (1, "Division"), div8 }, { lfo (1, "Shape"), lfoSquare } },
                                    slot (0, ModSource::Lfo2, ModTarget::Resonance, 0.2f)) },

            // Vowels stepping in time: a synced random LFO on Vowel, half absorbed.
            { "Vowel Sequencer", join ({ { "density", 9 }, { "resonance", 0.75f }, { "character", clean },
                                         { "flux", 0.3f }, { "wander", 0.2f },
                                         { lfo (0, "Sync"), 1 }, { lfo (0, "Division"), div8 }, { lfo (0, "Shape"), lfoRandom },
                                         { lfo (0, "Flux"), 0.5f } },
                                       slot (0, ModSource::Lfo1, ModTarget::Vowel, 0.35f)) },

            // Self-oscillating drone on the harmonics of D, slowly alive.
            { "Drone Organism", join ({ { "density", 12 }, { "vowel", schwa }, { "size", 0.8f }, { "resonance", 0.9f },
                                        { "feedback", 0.93f }, { "damping", 0.4f }, { "mix", 0.8f },
                                        { "flux", 0.3f }, { "drift", 0.05f }, { "wander", 0.6f }, { "coupling", 0.6f },
                                        { "anchor", scaleMode }, { "anchorKey", keyD }, { "anchorScale", harmonic },
                                        { lfo (2, "Sync"), 1 }, { lfo (2, "Division"), bar1 } },
                                      slot (0, ModSource::Lfo3, ModTarget::Scatter, 0.15f)) },
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
