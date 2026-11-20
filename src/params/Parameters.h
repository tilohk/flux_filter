#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParamSpec.h"
#include "mod/ModMatrix.h"

// Parameter IDs and the layout. Continuous ranges come from ParamSpec.h (shared with the DSP);
// choices and switches are defined here.
namespace flux::params
{
    namespace id
    {
        inline constexpr auto density   = "density";
        inline constexpr auto scale     = "scale";
        inline constexpr auto character = "character";
        inline constexpr auto autoGain  = "autoGain";
        inline constexpr auto limiter   = "limiter";
        inline constexpr auto fluxShape = "fluxShape";
        inline constexpr auto direction = "direction";
        inline constexpr auto driftMode = "driftMode";

        inline juce::String lfo (int i, const char* what)  { return "lfo" + juce::String (i + 1) + what; }   // Sync, Division, Shape, Polarity
        inline juce::String slot (int i, const char* what) { return "slot" + juce::String (i + 1) + what; }  // Source, Target, Depth, Curve, Flux, Via
    }

    inline const char* floatId (spec::FP p) { return spec::get (p).id; }

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

        auto floatParam = [] (FP p, AudioParameterFloatAttributes attr = {}) {
            const auto& s = spec::get (p);
            NormalisableRange<float> range (s.min, s.max, s.interval);
            if (s.centre > 0.0f)
                range.setSkewForCentre (s.centre);
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

        auto morphText = AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float m, int) {
                static const char* morphNames[] = { "LP", "BP", "HP", "Notch" };
                const int lo = std::clamp ((int) m, 0, 3);
                const float frac = m - (float) lo;
                if (frac < 0.01f || lo == 3) return String (morphNames[lo]);
                return String (morphNames[lo]) + " > " + morphNames[lo + 1] + " " + String (frac * 100.0f, 0) + "%";
            });

        auto field = std::make_unique<AudioProcessorParameterGroup> ("field", "Field", "|",
            std::make_unique<AudioParameterInt> (ParameterID { id::density, v }, "Density", 1, 24, 12),
            floatParam (FP::Center),
            floatParam (FP::Spread),
            choice (id::scale, "Scale", { "Log", "Harmonic", "Bell", "Pentatonic" }, 0),
            floatParam (FP::Tilt),
            floatParam (FP::Resonance),
            floatParam (FP::Morph, morphText),
            floatParam (FP::Combine),
            floatParam (FP::Width));

        auto character = std::make_unique<AudioProcessorParameterGroup> ("character", "Character", "|",
            floatParam (FP::Drift),
            choice (id::character, "Character", { "Clean", "Warm", "Broken" }, 1),
            floatParam (FP::Feedback),
            floatParam (FP::Damping),
            floatParam (FP::Drive));

        auto fluxGroup = std::make_unique<AudioProcessorParameterGroup> ("flux", "Flux", "|",
            floatParam (FP::FluxRate),
            choice (id::fluxShape, "Flux Shape", { "Sine", "Triangle", "Ramp", "Random" }, 0),
            floatParam (FP::FluxDepth),
            floatParam (FP::FluxLevel),
            floatParam (FP::Travel),
            choice (id::direction, "Direction", { "Up", "Down", "Outward", "Inward" }, 0),
            floatParam (FP::Viscosity),
            floatParam (FP::Wavelength),
            floatParam (FP::Turbulence),
            floatParam (FP::FluxStereo));

        auto output = std::make_unique<AudioProcessorParameterGroup> ("output", "Output", "|",
            toggle (id::autoGain, "Auto Gain", true),
            toggle (id::limiter, "Limiter", true),
            floatParam (FP::Mix),
            floatParam (FP::Output));

        AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::move (field), std::move (character), std::move (fluxGroup), std::move (output));

        // --- Mod sources ---
        const FP lfoRate[]  = { FP::Lfo1Rate, FP::Lfo2Rate, FP::Lfo3Rate };
        const FP lfoPhase[] = { FP::Lfo1Phase, FP::Lfo2Phase, FP::Lfo3Phase };
        StringArray divisions;
        for (const auto& d : lfoDivisions) divisions.add (d.name);
        const int defaultShapes[] = { 0, 1, 6 };   // Sine, Triangle, Custom
        for (int i = 0; i < ModSettings::numLfos; ++i)
        {
            const String n = "LFO " + String (i + 1);
            layout.add (std::make_unique<AudioProcessorParameterGroup> ("lfo" + String (i + 1), n, "|",
                floatParam (lfoRate[i]),
                toggle (id::lfo (i, "Sync"), n + " Sync", false),
                choice (id::lfo (i, "Division"), n + " Division", divisions, 4),
                choice (id::lfo (i, "Shape"), n + " Shape", names (lfoShapeNames), defaultShapes[i]),
                floatParam (lfoPhase[i]),
                choice (id::lfo (i, "Polarity"), n + " Polarity", { "Bipolar", "Unipolar" }, 0)));
        }

        layout.add (std::make_unique<AudioProcessorParameterGroup> ("env", "Envelope", "|",
            floatParam (FP::EnvAttack), floatParam (FP::EnvRelease), floatParam (FP::EnvSens)));
        layout.add (std::make_unique<AudioProcessorParameterGroup> ("chaos", "Chaos", "|",
            floatParam (FP::ChaosSpeed), floatParam (FP::ChaosShape)));
        layout.add (std::make_unique<AudioProcessorParameterGroup> ("driftSrc", "Drift Source", "|",
            floatParam (FP::DriftRate),
            choice (id::driftMode, "Drift Mode", { "Smooth", "Sample & Hold" }, 0),
            floatParam (FP::DriftSlew)));
        layout.add (std::make_unique<AudioProcessorParameterGroup> ("macros", "Macros", "|",
            floatParam (FP::Macro1), floatParam (FP::Macro2), floatParam (FP::Macro3), floatParam (FP::Macro4)));

        // --- Matrix ---
        for (int i = 0; i < ModSettings::numSlots; ++i)
        {
            const String n = "Slot " + String (i + 1);
            NormalisableRange<float> depthRange (spec::slotDepth.min, spec::slotDepth.max);
            layout.add (std::make_unique<AudioProcessorParameterGroup> ("slot" + String (i + 1), n, "|",
                choice (id::slot (i, "Source"), n + " Source", names (modSourceNames), 0),
                choice (id::slot (i, "Target"), n + " Target", names (modTargetNames), 0),
                std::make_unique<AudioParameterFloat> (ParameterID { id::slot (i, "Depth"), v }, n + " Depth", depthRange, 0.0f),
                choice (id::slot (i, "Curve"), n + " Curve", names (modCurveNames), 0),
                toggle (id::slot (i, "Flux"), n + " Flux", false),
                choice (id::slot (i, "Via"), n + " Via", names (modSourceNames), 0)));
        }

        return layout;
    }
}
