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
    Vowel, Shift, Stretch, Resonance, Scatter,
    Feedback, Damping,
    FluxAmount, Wander, Rasp,
    Drive, Mix, Output,
    Lfo1Rate, Lfo2Rate,
    Lfo1Flux, Lfo2Flux,
    Count
};

inline const std::array<FloatSpec, (size_t) FP::Count>& floats()
{
    static const std::array<FloatSpec, (size_t) FP::Count> table {{
        { "mode",       "Mode",            0.0f,  9.0f,     0.0f },                         // ring position (throat shape), shown as modes 1-18; see dsp/FormantTable.h
        { "shift",      "Shift",           -2.0f, 2.0f,     0.0f,   0.0f,   0.0f,  "oct" }, // moves every formant; + = higher
        { "stretch",    "Stretch",         -1.0f, 1.0f,     0.0f },                          // formant spacing: - = squeezed, + = apart
        { "resonance",  "Resonance",       0.0f,  1.0f,     0.6f },
        { "scatter",    "Scatter",         0.0f,  1.0f,     0.0f },
        { "feedback",   "Feedback",        0.0f,  1.0f,     0.0f },
        { "damping",    "Damping",         0.0f,  1.0f,     0.3f },
        { "flux",       "Flux",            0.0f,  1.0f,     0.25f },                         // calm → chaotic: drives every life process
        { "wander",     "Wander",          0.0f,  1.0f,     0.5f },                          // how far the chaos roams
        { "rasp",       "Rasp",            0.0f,  1.0f,     0.0f },                          // throat roughness: jitter + growl
        { "drive",      "Drive",           0.0f,  24.0f,    0.0f,   0.0f,   0.01f, "dB" },
        { "mix",        "Mix",             0.0f,  1.0f,     1.0f },                          // full wet: dry signal would fill the cut mode's notches back in
        { "output",     "Gain",            -24.0f, 24.0f,   0.0f,   0.0f,   0.01f, "dB" },

        { "lfo1Rate",   "LFO 1 Rate",      0.01f, 30.0f,    0.5f,   1.0f,   0.0f,  "Hz" },
        { "lfo2Rate",   "LFO 2 Rate",      0.01f, 30.0f,    1.3f,   1.0f,   0.0f,  "Hz" },
        { "lfo1Flux",   "LFO 1 Flux",      0.0f,  1.0f,     1.0f },
        { "lfo2Flux",   "LFO 2 Flux",      0.0f,  1.0f,     0.0f },
    }};
    return table;
}

inline const FloatSpec& get (FP p) noexcept { return floats()[(size_t) p]; }

// Modulation depth: same range for every connection.
inline const FloatSpec slotDepth { "", "Depth", -1.0f, 1.0f, 0.0f };

} // namespace flux::spec
