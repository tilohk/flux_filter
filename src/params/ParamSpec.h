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

// Index of every continuous parameter. Order of the first block matches phases 1–3.
enum class FP : int
{
    Center, Spread, Tilt, Resonance, Morph, Combine, Width,
    Drift, Feedback, Damping,
    FluxRate, FluxDepth, FluxLevel, Travel, Viscosity, Wavelength, Turbulence, FluxStereo,
    Drive, Mix, Output,
    // Mod sources (phase 4)
    Lfo1Rate, Lfo1Phase, Lfo2Rate, Lfo2Phase, Lfo3Rate, Lfo3Phase,
    EnvAttack, EnvRelease, EnvSens,
    ChaosSpeed, ChaosShape,
    DriftRate, DriftSlew,
    Macro1, Macro2, Macro3, Macro4,
    Count
};

inline const std::array<FloatSpec, (size_t) FP::Count>& floats()
{
    static const std::array<FloatSpec, (size_t) FP::Count> table {{
        { "center",     "Center",          20.0f, 12000.0f, 600.0f, 600.0f, 0.0f,  "Hz" },
        { "spread",     "Spread",          0.0f,  8.0f,     4.0f,   0.0f,   0.01f, "" },
        { "tilt",       "Tilt",            -12.0f, 12.0f,   0.0f,   0.0f,   0.01f, "dB/oct" },
        { "resonance",  "Resonance",       0.0f,  1.0f,     0.6f },
        { "morph",      "Morph",           0.0f,  3.0f,     1.0f },
        { "combine",    "Parallel/Serial", 0.0f,  1.0f,     0.0f },
        { "width",      "Width",           0.0f,  1.0f,     0.5f },
        { "drift",      "Drift",           0.0f,  1.0f,     0.15f },
        { "feedback",   "Feedback",        0.0f,  1.0f,     0.0f },
        { "damping",    "Damping",         0.0f,  1.0f,     0.3f },
        { "fluxRate",   "Flux Rate",       0.02f, 20.0f,    0.3f,   1.0f,   0.0f,  "Hz" },
        { "fluxDepth",  "Flux Depth",      -3.0f, 3.0f,     0.5f,   0.0f,   0.001f, "oct" },
        { "fluxLevel",  "Flux Level",      0.0f,  1.0f,     0.0f },
        { "travel",     "Travel",          0.0f,  4.0f,     0.8f,   0.8f,   0.0f,  "s" },
        { "viscosity",  "Viscosity",       0.0f,  1.0f,     0.2f },
        { "wavelength", "Wavelength",      0.0f,  4.0f,     0.0f,   0.0f,   0.001f, "" },
        { "turbulence", "Turbulence",      0.0f,  1.0f,     0.2f },
        { "fluxStereo", "Flux Stereo",     0.0f,  1.0f,     0.3f },
        { "drive",      "Drive",           0.0f,  24.0f,    0.0f,   0.0f,   0.01f, "dB" },
        { "mix",        "Mix",             0.0f,  1.0f,     1.0f },
        { "output",     "Output",          -24.0f, 24.0f,   0.0f,   0.0f,   0.01f, "dB" },

        { "lfo1Rate",   "LFO 1 Rate",      0.01f, 30.0f,    0.5f,   1.0f,   0.0f,  "Hz" },
        { "lfo1Phase",  "LFO 1 Phase",     0.0f,  1.0f,     0.0f },
        { "lfo2Rate",   "LFO 2 Rate",      0.01f, 30.0f,    1.3f,   1.0f,   0.0f,  "Hz" },
        { "lfo2Phase",  "LFO 2 Phase",     0.0f,  1.0f,     0.0f },
        { "lfo3Rate",   "LFO 3 Rate",      0.01f, 30.0f,    0.17f,  1.0f,   0.0f,  "Hz" },
        { "lfo3Phase",  "LFO 3 Phase",     0.0f,  1.0f,     0.0f },
        { "envAttack",  "Env Attack",      0.1f,  200.0f,   5.0f,   10.0f,  0.0f,  "ms" },
        { "envRelease", "Env Release",     5.0f,  2000.0f,  120.0f, 150.0f, 0.0f,  "ms" },
        { "envSens",    "Env Sensitivity", -24.0f, 24.0f,   0.0f,   0.0f,   0.01f, "dB" },
        { "chaosSpeed", "Chaos Speed",     0.0f,  1.0f,     0.3f },
        { "chaosShape", "Chaos Shape",     0.0f,  1.0f,     0.3f },
        { "driftRate",  "Drift Rate",      0.01f, 10.0f,    0.3f,   0.5f,   0.0f,  "Hz" },
        { "driftSlew",  "Drift Slew",      0.0f,  1.0f,     0.5f },
        { "macro1",     "Macro 1",         0.0f,  1.0f,     0.0f },
        { "macro2",     "Macro 2",         0.0f,  1.0f,     0.0f },
        { "macro3",     "Macro 3",         0.0f,  1.0f,     0.0f },
        { "macro4",     "Macro 4",         0.0f,  1.0f,     0.0f },
    }};
    return table;
}

inline const FloatSpec& get (FP p) noexcept { return floats()[(size_t) p]; }

// Matrix slot depth: same range for every slot.
inline const FloatSpec slotDepth { "", "Depth", -1.0f, 1.0f, 0.0f };

} // namespace flux::spec
