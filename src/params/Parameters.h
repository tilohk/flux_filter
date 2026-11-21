#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParamSpec.h"

#include <optional>
#include "mod/ModMatrix.h"
#include "dsp/FormantTable.h"

// Parameter IDs and the layout. Continuous ranges come from ParamSpec.h (shared with the DSP);
// choices and switches are defined here.
namespace flux::params
{
    namespace id
    {
        inline constexpr auto density   = "density";
        inline constexpr auto character = "character";
        inline constexpr auto autoGain  = "autoGain";
        inline constexpr auto limiter   = "limiter";
        inline constexpr auto fluxShape = "fluxShape";
        inline constexpr auto direction = "direction";

        inline juce::String lfo (int i, const char* what)  { return "lfo" + juce::String (i + 1) + what; }   // Sync, Division, Shape, Unipolar, Flux
        inline juce::String slot (int i, const char* what) { return "slot" + juce::String (i + 1) + what; }  // Source, Target, Depth
    }

    inline const char* floatId (spec::FP p) { return spec::get (p).id; }

    // Ranges are exact constants, so exact comparison is what we want here.
    inline bool isUnitRange (const spec::FloatSpec& s)    { return juce::exactlyEqual (s.min, 0.0f) && juce::exactlyEqual (s.max, 1.0f); }
    inline bool isBipolarRange (const spec::FloatSpec& s) { return juce::exactlyEqual (s.min, -1.0f) && juce::exactlyEqual (s.max, 1.0f); }

    // Display text for a continuous parameter, chosen from its unit and range.
    inline juce::String formatValue (const spec::FloatSpec& s, float v)
    {
        using juce::String;
        if (std::abs (v) < 0.0005f) v = 0.0f;   // no "-0.00"
        const juce::String label (s.label);
        if (label == "Hz")
        {
            if (v >= 1000.0f) return String (v / 1000.0f, 2) + " kHz";
            return String (v, v < 10.0f ? 2 : v < 100.0f ? 1 : 0) + " Hz";
        }
        if (label == "ms")   return String (v, v < 10.0f ? 1 : 0) + " ms";
        if (label == "dB")   return String (v, 1) + " dB";
        if (label == "dB/oct") return String (v, 1) + " dB/oct";
        if (label == "s")    return String (v, 2) + " s";
        if (label == "oct")  return (v > 0.0f ? "+" : "") + String (v, 2) + " oct";
        if (isUnitRange (s))    return String (juce::roundToInt (v * 100.0f)) + " %";
        if (isBipolarRange (s)) return (v > 0.0f ? "+" : "") + String (juce::roundToInt (v * 100.0f)) + " %";
        return String (v, 2);
    }

    // Inverse of formatValue for typed input: "50 %", "2.5k", "300 ms", "-3 dB" all work.
    inline float parseValue (const spec::FloatSpec& s, const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        float v = t.getFloatValue();
        if (t.containsChar ('k') && juce::String (s.label) == "Hz") v *= 1000.0f;
        if (t.containsChar ('%') || ((isUnitRange (s) || isBipolarRange (s)) && std::abs (v) > 1.0f))
            v /= 100.0f;
        return juce::jlimit (s.min, s.max, v);
    }

    template <size_t N>
    inline juce::StringArray names (const char* const (&list)[N])
    {
        juce::StringArray a;
        for (auto* n : list) a.add (n);
        return a;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        using spec::FP;
        constexpr int v = 1; // parameter version hint for AU/VST3

        auto floatParam = [] (FP p, std::optional<AudioParameterFloatAttributes> custom = std::nullopt) {
            const auto& s = spec::get (p);
            NormalisableRange<float> range (s.min, s.max, s.interval);
            if (s.centre > 0.0f)
                range.setSkewForCentre (s.centre);
            auto attr = custom.value_or (AudioParameterFloatAttributes()
                .withStringFromValueFunction ([&s] (float x, int) { return formatValue (s, x); })
                .withValueFromStringFunction ([&s] (const String& t) { return parseValue (s, t); }));
            if (*s.label != 0)
                attr = attr.withLabel (s.label);
            return std::make_unique<AudioParameterFloat> (ParameterID { s.id, v }, s.name, range, s.def, attr);
        };
        auto choice = [] (const String& pid, const String& name, const StringArray& items, int def) {
            return std::make_unique<AudioParameterChoice> (ParameterID { pid, v }, name, items, def);
        };
        auto toggle = [] (const String& pid, const String& name, bool def) {
            return std::make_unique<AudioParameterBool> (ParameterID { pid, v }, name, def);
        };

        // Vowel: the throat shape's name near each shape, "a/e" style in between (the ring wraps).
        auto vowelText = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float pos, int) {
                const int n = formants::numShapes;
                const int nearest = (int) std::lround (pos) % n;
                if (std::abs (pos - std::round (pos)) < 0.15f)
                    return String (formants::shapes[(size_t) nearest].name);
                const int lo = std::clamp ((int) pos, 0, n - 1);
                return String (formants::shapes[(size_t) lo].name) + "/" + formants::shapes[(size_t) ((lo + 1) % n)].name;
            })
            .withValueFromStringFunction ([] (const String& t) {
                const auto name = t.trim().toLowerCase();
                for (int i = 0; i < formants::numShapes; ++i)
                    if (name == formants::shapes[(size_t) i].name)
                        return (float) i;
                return jlimit (0.0f, (float) formants::numShapes, name.getFloatValue());
            });

        // Size: how big the throat is, as a factor (x2 = twice as big = formants an octave lower).
        auto sizeText = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float oct, int) { return "x" + String (std::exp2 (oct), 2); })
            .withValueFromStringFunction ([] (const String& t) {
                const float f = t.trim().trimCharactersAtStart ("xX*").getFloatValue();
                return f > 0.0f ? jlimit (-2.0f, 2.0f, std::log2 (f)) : 0.0f;
            });

        auto field = std::make_unique<AudioProcessorParameterGroup> ("cells", "Cells", "|",
            std::make_unique<AudioParameterInt> (ParameterID { id::density, v }, "Density", 1, 16, 8),
            floatParam (FP::Vowel, vowelText),
            floatParam (FP::Size, sizeText),
            floatParam (FP::Resonance),
            floatParam (FP::Scatter),
            floatParam (FP::Width));

        auto character = std::make_unique<AudioProcessorParameterGroup> ("character", "Character", "|",
            choice (id::character, "Character", { "Clean", "Warm", "Broken" }, 1),
            floatParam (FP::Feedback),
            floatParam (FP::Damping),
            floatParam (FP::Drive));

        auto fluxGroup = std::make_unique<AudioProcessorParameterGroup> ("flux", "Flux", "|",
            floatParam (FP::Drift),
            choice (id::fluxShape, "Flux Shape", { "Sine", "Triangle", "Ramp", "Random" }, 0),
            floatParam (FP::FluxAmount),
            floatParam (FP::Wander),
            choice (id::direction, "Direction", { "Up", "Down", "Outward", "Inward" }, 0),
            floatParam (FP::Coupling),
            floatParam (FP::Feed),
            floatParam (FP::Lifespan),
            floatParam (FP::Rasp));

        auto output = std::make_unique<AudioProcessorParameterGroup> ("output", "Output", "|",
            toggle (id::autoGain, "Auto Gain", true),
            toggle (id::limiter, "Limiter", true),
            floatParam (FP::Mix),
            floatParam (FP::Output));

        AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::move (field), std::move (character), std::move (fluxGroup), std::move (output));

        // --- Mod sources ---
        const FP lfoRate[] = { FP::Lfo1Rate, FP::Lfo2Rate, FP::Lfo3Rate };
        const FP lfoFlux[] = { FP::Lfo1Flux, FP::Lfo2Flux, FP::Lfo3Flux };
        StringArray divisions;
        for (const auto& d : lfoDivisions) divisions.add (d.name);
        const int defaultShapes[] = { 0, 1, 0 };   // Sine, Triangle, Sine
        for (int i = 0; i < ModSettings::numLfos; ++i)
        {
            const String n = "LFO " + String (i + 1);
            layout.add (std::make_unique<AudioProcessorParameterGroup> ("lfo" + String (i + 1), n, "|",
                floatParam (lfoRate[i]),
                toggle (id::lfo (i, "Sync"), n + " Sync", false),
                choice (id::lfo (i, "Division"), n + " Division", divisions, defaultLfoDivision),
                choice (id::lfo (i, "Shape"), n + " Shape", names (lfoShapeNames), defaultShapes[i]),
                toggle (id::lfo (i, "Unipolar"), n + " Unipolar", false),
                floatParam (lfoFlux[i])));
        }

        // --- Connections (made by drag and drop in the editor; automatable like everything else) ---
        for (int i = 0; i < ModSettings::numSlots; ++i)
        {
            const String n = "Mod " + String (i + 1);
            NormalisableRange<float> depthRange (spec::slotDepth.min, spec::slotDepth.max);
            const auto depthText = AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float x, int) { return formatValue (spec::slotDepth, x); })
                .withValueFromStringFunction ([] (const String& t) { return parseValue (spec::slotDepth, t); });
            layout.add (std::make_unique<AudioProcessorParameterGroup> ("slot" + String (i + 1), n, "|",
                choice (id::slot (i, "Source"), n + " Source", names (modSourceNames), 0),
                choice (id::slot (i, "Target"), n + " Target", names (modTargetNames), 0),
                std::make_unique<AudioParameterFloat> (ParameterID { id::slot (i, "Depth"), v }, n + " Depth", depthRange, 0.0f, depthText)));
        }

        return layout;
    }
}
