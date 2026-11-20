#pragma once

#include <algorithm>
#include <cmath>

namespace flux
{

// Cheap, smooth tanh approximation (Pade), clamped so it never overshoots ±1.
inline float softClip (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// One resonant cell of the field: a TPT / zero-delay-feedback state-variable filter
// (Simper/Cytomic topology) that stays stable under fast cutoff modulation.
// The band state is softly saturated, so high resonance thickens instead of exploding.
//
// Morph: 0 = lowpass, 1 = bandpass (unity peak), 2 = highpass, 3 = notch, linear crossfades between.
class FluxCell
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0f; }

    // cutoffHz must already be clamped below Nyquist by the caller.
    void setCoefficients (float cutoffHz, float q, float sampleRate) noexcept
    {
        constexpr float pi = 3.14159265358979f;
        const float g = std::tan (pi * cutoffHz / sampleRate);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void setMorph (float morph) noexcept
    {
        morph = std::clamp (morph, 0.0f, 3.0f);
        wLp = wBp = wHp = wNotch = 0.0f;
        if (morph <= 1.0f)      { wBp = morph;            wLp = 1.0f - morph; }
        else if (morph <= 2.0f) { wHp = morph - 1.0f;     wBp = 1.0f - wHp; }
        else                    { wNotch = morph - 2.0f;  wHp = 1.0f - wNotch; }
    }

    float process (float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;          // band
        const float v2 = ic2 + a2 * ic1 + a3 * v3;    // low

        // Saturate the band state: this is what gives the "growl" at high resonance
        // and keeps self-oscillation bounded.
        ic1 = stateLimit * softClip ((2.0f * v1 - ic1) * invStateLimit);
        ic2 = 2.0f * v2 - ic2;

        const float lp = v2;
        const float bp = k * v1;                       // normalised: unity gain at the peak
        const float hp = v0 - k * v1 - v2;
        const float notch = lp + hp;

        return wLp * lp + wBp * bp + wHp * hp + wNotch * notch;
    }

private:
    float ic1 = 0.0f, ic2 = 0.0f;
    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float wLp = 0.0f, wBp = 1.0f, wHp = 0.0f, wNotch = 0.0f;

    static constexpr float stateLimit = 4.0f;
    static constexpr float invStateLimit = 1.0f / stateLimit;
};

} // namespace flux
