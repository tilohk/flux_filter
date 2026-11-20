#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// All parameter IDs and the layout live here, so DSP, UI and (later) the mod matrix share one source.
namespace flux::params
{
    namespace id
    {
        inline constexpr auto density   = "density";
        inline constexpr auto center    = "center";
        inline constexpr auto spread    = "spread";
        inline constexpr auto scale     = "scale";
        inline constexpr auto tilt      = "tilt";
        inline constexpr auto resonance = "resonance";
        inline constexpr auto morph     = "morph";
        inline constexpr auto combine   = "combine";
        inline constexpr auto width     = "width";
        inline constexpr auto drift     = "drift";
        inline constexpr auto character = "character";
        inline constexpr auto feedback  = "feedback";
        inline constexpr auto damping   = "damping";
        inline constexpr auto autoGain  = "autoGain";
        inline constexpr auto limiter   = "limiter";
        inline constexpr auto fluxRate   = "fluxRate";
        inline constexpr auto fluxShape  = "fluxShape";
        inline constexpr auto fluxDepth  = "fluxDepth";
        inline constexpr auto fluxLevel  = "fluxLevel";
        inline constexpr auto travel     = "travel";
        inline constexpr auto direction  = "direction";
        inline constexpr auto viscosity  = "viscosity";
        inline constexpr auto wavelength = "wavelength";
        inline constexpr auto turbulence = "turbulence";
        inline constexpr auto fluxStereo = "fluxStereo";
        inline constexpr auto drive     = "drive";
        inline constexpr auto mix       = "mix";
        inline constexpr auto output    = "output";
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        using Float  = AudioParameterFloat;
        using Int    = AudioParameterInt;
        using Choice = AudioParameterChoice;
        using Bool   = AudioParameterBool;
        constexpr int v = 1; // parameter version hint for AU/VST3

        auto hz = AudioParameterFloatAttributes().withLabel ("Hz");
        auto db = AudioParameterFloatAttributes().withLabel ("dB");

        NormalisableRange<float> centerRange (20.0f, 12000.0f);
        centerRange.setSkewForCentre (600.0f);
        NormalisableRange<float> rateRange (0.02f, 20.0f);
        rateRange.setSkewForCentre (1.0f);
        NormalisableRange<float> travelRange (0.0f, 4.0f);
        travelRange.setSkewForCentre (0.8f);

        auto morphText = AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float m, int) {
                static const char* names[] = { "LP", "BP", "HP", "Notch" };
                const int lo = std::clamp ((int) m, 0, 3);
                const float frac = m - (float) lo;
                if (frac < 0.01f || lo == 3) return String (names[lo]);
                return String (names[lo]) + " > " + names[lo + 1] + " " + String (frac * 100.0f, 0) + "%";
            });

        AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (
            std::make_unique<Int>    (ParameterID { id::density, v }, "Density", 1, 24, 12),
            std::make_unique<Float>  (ParameterID { id::center, v }, "Center", centerRange, 600.0f, hz),
            std::make_unique<Float>  (ParameterID { id::spread, v }, "Spread", NormalisableRange<float> (0.0f, 8.0f, 0.01f), 4.0f),
            std::make_unique<Choice> (ParameterID { id::scale, v }, "Scale",
                                      StringArray { "Log", "Harmonic", "Bell", "Pentatonic" }, 0),
            std::make_unique<Float>  (ParameterID { id::tilt, v }, "Tilt", NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                      AudioParameterFloatAttributes().withLabel ("dB/oct")),
            std::make_unique<Float>  (ParameterID { id::resonance, v }, "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.6f),
            std::make_unique<Float>  (ParameterID { id::morph, v }, "Morph", NormalisableRange<float> (0.0f, 3.0f), 1.0f, morphText),
            std::make_unique<Float>  (ParameterID { id::combine, v }, "Parallel/Serial", NormalisableRange<float> (0.0f, 1.0f), 0.0f),
            std::make_unique<Float>  (ParameterID { id::width, v }, "Width", NormalisableRange<float> (0.0f, 1.0f), 0.5f),
            std::make_unique<Float>  (ParameterID { id::drift, v }, "Drift", NormalisableRange<float> (0.0f, 1.0f), 0.15f),
            std::make_unique<Choice> (ParameterID { id::character, v }, "Character",
                                      StringArray { "Clean", "Warm", "Broken" }, 1),
            std::make_unique<Float>  (ParameterID { id::feedback, v }, "Feedback", NormalisableRange<float> (0.0f, 1.0f), 0.0f),
            std::make_unique<Float>  (ParameterID { id::damping, v }, "Damping", NormalisableRange<float> (0.0f, 1.0f), 0.3f),
            std::make_unique<Bool>   (ParameterID { id::autoGain, v }, "Auto Gain", true),
            std::make_unique<Bool>   (ParameterID { id::limiter, v }, "Limiter", true),
            std::make_unique<Float>  (ParameterID { id::fluxRate, v }, "Flux Rate", rateRange, 0.3f, hz),
            std::make_unique<Choice> (ParameterID { id::fluxShape, v }, "Flux Shape",
                                      StringArray { "Sine", "Triangle", "Ramp", "Random" }, 0),
            std::make_unique<Float>  (ParameterID { id::fluxDepth, v }, "Flux Depth", NormalisableRange<float> (-3.0f, 3.0f, 0.001f), 0.5f,
                                      AudioParameterFloatAttributes().withLabel ("oct")),
            std::make_unique<Float>  (ParameterID { id::fluxLevel, v }, "Flux Level", NormalisableRange<float> (0.0f, 1.0f), 0.0f),
            std::make_unique<Float>  (ParameterID { id::travel, v }, "Travel", travelRange, 0.8f,
                                      AudioParameterFloatAttributes().withLabel ("s")),
            std::make_unique<Choice> (ParameterID { id::direction, v }, "Direction",
                                      StringArray { "Up", "Down", "Outward", "Inward" }, 0),
            std::make_unique<Float>  (ParameterID { id::viscosity, v }, "Viscosity", NormalisableRange<float> (0.0f, 1.0f), 0.2f),
            std::make_unique<Float>  (ParameterID { id::wavelength, v }, "Wavelength", NormalisableRange<float> (0.0f, 4.0f, 0.001f), 0.0f),
            std::make_unique<Float>  (ParameterID { id::turbulence, v }, "Turbulence", NormalisableRange<float> (0.0f, 1.0f), 0.2f),
            std::make_unique<Float>  (ParameterID { id::fluxStereo, v }, "Flux Stereo", NormalisableRange<float> (0.0f, 1.0f), 0.3f),
            std::make_unique<Float>  (ParameterID { id::drive, v }, "Drive", NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f, db),
            std::make_unique<Float>  (ParameterID { id::mix, v }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f),
            std::make_unique<Float>  (ParameterID { id::output, v }, "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, db));
        return layout;
    }
}
