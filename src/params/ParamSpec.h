#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Ranges of every continuous parameter, in plain C++ so the DSP (mod matrix) and the JUCE
// parameter layout share one definition. Normalisation matches juce::NormalisableRange with
// setSkewForCentre, so a modulation offset in 0..1 space means the same as a knob turn.
namespace flux::spec
{

struct FloatSpec
{
    const char* id;
    const char* name;
    float min, max, def;
    float centre = 0.0f;     // value at the knob's middle; 0 = linear
    float interval = 0.0f;   // host snapping, 0 = continuous
    const char* label = "";

    float skew() const noexcept
    {
        return centre > min && centre < max ? std::log (0.5f) / std::log ((centre - min) / (max - min)) : 1.0f;
    }

    float to01 (float v) const noexcept
    {
        const float p = std::clamp ((v - min) / (max - min), 0.0f, 1.0f);
        return centre > 0.0f ? std::pow (p, skew()) : p;
    }

    float from01 (float p) const noexcept
    {
        p = std::clamp (p, 0.0f, 1.0f);
        if (centre > 0.0f)
            p = p > 0.0f ? std::exp (std::log (p) / skew()) : 0.0f;
        return min + (max - min) * p;
    }
};

// Index of every continuous parameter.
enum class FP : int
{
    Vowel, Size, Resonance, Scatter, Width,
    Feedback, Damping,
    FluxRate, FluxAmount, Travel, Viscosity, Wavelength, Turbulence, FluxStereo,
    Drive, Mix, Output,
    Lfo1Rate, Lfo2Rate, Lfo3Rate,
    Lfo1Flux, Lfo2Flux, Lfo3Flux,
    Count
};

inline const std::array<FloatSpec, (size_t) FP::Count>& floats()
{
    static const std::array<FloatSpec, (size_t) FP::Count> table {{
        { "vowel",      "Vowel",           0.0f,  9.0f,     0.0f },                         // ring position, see dsp/FormantTable.h
        { "size",       "Size",            -2.0f, 2.0f,     0.0f,   0.0f,   0.0f,  "oct" }, // throat size; + = bigger = lower formants
        { "resonance",  "Resonance",       0.0f,  1.0f,     0.6f },
        { "scatter",    "Scatter",         0.0f,  1.0f,     0.0f },
        { "width",      "Width",           0.0f,  1.0f,     0.5f },
        { "feedback",   "Feedback",        0.0f,  1.0f,     0.0f },
        { "damping",    "Damping",         0.0f,  1.0f,     0.3f },
        { "fluxRate",   "Flux Rate",       0.02f, 20.0f,    0.3f,   1.0f,   0.0f,  "Hz" },
        { "flux",       "Flux",            0.0f,  1.0f,     0.25f },
        { "travel",     "Travel",          0.0f,  4.0f,     0.8f,   0.8f,   0.0f,  "s" },
        { "viscosity",  "Viscosity",       0.0f,  1.0f,     0.2f },
        { "wavelength", "Wavelength",      0.0f,  4.0f,     0.0f,   0.0f,   0.001f, "" },
        { "turbulence", "Turbulence",      0.0f,  1.0f,     0.2f },
        { "fluxStereo", "Flux Stereo",     0.0f,  1.0f,     0.3f },
        { "drive",      "Drive",           0.0f,  24.0f,    0.0f,   0.0f,   0.01f, "dB" },
        { "mix",        "Mix",             0.0f,  1.0f,     1.0f },
        { "output",     "Gain",            -24.0f, 24.0f,   0.0f,   0.0f,   0.01f, "dB" },

        { "lfo1Rate",   "LFO 1 Rate",      0.01f, 30.0f,    0.5f,   1.0f,   0.0f,  "Hz" },
        { "lfo2Rate",   "LFO 2 Rate",      0.01f, 30.0f,    1.3f,   1.0f,   0.0f,  "Hz" },
        { "lfo3Rate",   "LFO 3 Rate",      0.01f, 30.0f,    0.17f,  1.0f,   0.0f,  "Hz" },
        { "lfo1Flux",   "LFO 1 Flux",      0.0f,  1.0f,     1.0f },
        { "lfo2Flux",   "LFO 2 Flux",      0.0f,  1.0f,     0.0f },
        { "lfo3Flux",   "LFO 3 Flux",      0.0f,  1.0f,     0.0f },
    }};
    return table;
}

inline const FloatSpec& get (FP p) noexcept { return floats()[(size_t) p]; }

// Modulation depth: same range for every connection.
inline const FloatSpec slotDepth { "", "Depth", -1.0f, 1.0f, 0.0f };

} // namespace flux::spec
