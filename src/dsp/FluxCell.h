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

// One resonant cell of the field: two TPT / zero-delay-feedback state-variable filters in series
// (Simper/Cytomic topology), stable under fast cutoff modulation. Two stages make the skirts twice as
// steep (24 dB/oct), so a cell passes its band and clearly removes the rest.
//   Bandpass: both stages resonate at the requested Q, so bands are narrower and the gaps between
//             cells deeper than with one stage; the peak stays at unity.
//   LP / HP / notch: only the first stage resonates; the second is a plain Butterworth stage, and the
//             resonant bump is tamed to sqrt(Q) (instead of Q), like an analog lowpass that gets
//             thinner rather than much louder as resonance rises.
// The first stage's band state is softly saturated, so high resonance thickens instead of exploding.
//
// Morph: 0 = lowpass, 1 = bandpass (unity peak), 2 = highpass, 3 = notch, linear crossfades between.
class FluxCell
{
public:
    void reset() noexcept { first.reset(); second.reset(); }

    // cutoffHz must already be clamped below Nyquist by the caller.
    void setCoefficients (float cutoffHz, float q, float sampleRate) noexcept
    {
        constexpr float pi = 3.14159265358979f;
        g = std::tan (pi * cutoffHz / sampleRate);
        const float q1 = q * stageQ;
        c1.set (g, 1.0f / q1);
        lpHpScale = 1.0f / std::sqrt (std::max (1.0f, q1));
        updateSecondStage();
    }

    void setMorph (float morph) noexcept
    {
        morph = std::clamp (morph, 0.0f, 3.0f);
        wLp = wBp = wHp = wNotch = 0.0f;
        if (morph <= 1.0f)      { wBp = morph;            wLp = 1.0f - morph; }
        else if (morph <= 2.0f) { wHp = morph - 1.0f;     wBp = 1.0f - wHp; }
        else                    { wNotch = morph - 2.0f;  wHp = 1.0f - wNotch; }
        updateSecondStage();
    }

    // limit: how far the band state can swing before it saturates (high = cleaner).
    // asymmetry: the negative half saturates earlier, which adds even harmonics (0 = symmetric).
    void setSaturation (float limit, float asymmetry) noexcept
    {
        stateLimit = limit;
        invStateLimit = 1.0f / limit;
        negDrive = 1.0f + asymmetry;
        invNegDrive = 1.0f / negDrive;
    }

    // Steady-state magnitude of a cell at frequency ratio x = f / cutoff (no saturation; the
    // morph crossfade is summed as magnitudes, so this never under-estimates). Used by the field
    // to keep its loudest point at unity.
    static float magnitude (float x, float q, float morph) noexcept
    {
        float wLp_ = 0, wBp_ = 0, wHp_ = 0, wNotch_ = 0;
        morph = std::clamp (morph, 0.0f, 3.0f);
        if (morph <= 1.0f)      { wBp_ = morph;            wLp_ = 1.0f - morph; }
        else if (morph <= 2.0f) { wHp_ = morph - 1.0f;     wBp_ = 1.0f - wHp_; }
        else                    { wNotch_ = morph - 2.0f;  wHp_ = 1.0f - wNotch_; }

        const float q1 = q * stageQ, k1 = 1.0f / q1;
        const float k2 = wBp_ * k1 + (1.0f - wBp_) * butterworthK;
        auto stageMag = [&] (float k, float lpHpGain) {
            const float x2 = x * x, d = std::sqrt ((1.0f - x2) * (1.0f - x2) + k * k * x2) + 1.0e-9f;
            return (lpHpGain * (wLp_ + wHp_ * x2) + wBp_ * k * x + wNotch_ * std::abs (1.0f - x2)) / d;
        };
        return stageMag (k1, 1.0f / std::sqrt (std::max (1.0f, q1))) * stageMag (k2, 1.0f);
    }

    float process (float v0) noexcept
    {
        return stage<false> (second, c2, 1.0f, stage<true> (first, c1, lpHpScale, v0));
    }

private:
    struct Coeffs
    {
        float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        void set (float tanG, float damping) noexcept
        {
            k  = damping;
            a1 = 1.0f / (1.0f + tanG * (tanG + k));
            a2 = tanG * a1;
            a3 = tanG * a2;
        }
    };

    static constexpr float stageQ = 1.0f;          // both stages at the full Q: bands ~0.64x as wide as one stage
    static constexpr float butterworthK = 1.41421356f;

    // Second stage: resonant like the first for bandpass, flat (Butterworth) for the other modes.
    void updateSecondStage() noexcept
    {
        c2.set (g, wBp * c1.k + (1.0f - wBp) * butterworthK);
    }

    struct State
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        void reset() noexcept { ic1 = ic2 = 0.0f; }
    };

    template <bool saturated>
    float stage (State& st, const Coeffs& c, float lpHpGain, float v0) noexcept
    {
        const float a1 = c.a1, a2 = c.a2, a3 = c.a3, k = c.k;
        const float v3 = v0 - st.ic2;
        const float v1 = a1 * st.ic1 + a2 * v3;          // band
        const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3; // low

        // Saturate the band state (first stage only): this is what gives the "growl" at high
        // resonance and keeps self-oscillation bounded.
        if constexpr (saturated)
            st.ic1 = stateLimit * saturate ((2.0f * v1 - st.ic1) * invStateLimit);
        else
            st.ic1 = 2.0f * v1 - st.ic1;
        st.ic2 = 2.0f * v2 - st.ic2;

        const float lp = v2;
        const float bp = k * v1;                       // normalised: unity gain at the peak
        const float hp = v0 - k * v1 - v2;
        const float notch = lp + hp;                   // unity passband, no bump to tame

        return lpHpGain * (wLp * lp + wHp * hp) + wBp * bp + wNotch * notch;
    }

    // Slope is exactly 1 at zero and never above 1 anywhere: a slope < 1 near zero would add
    // damping, a slope > 1 would inject energy (a quiet endless limit cycle).
    float saturate (float x) const noexcept
    {
        return x >= 0.0f ? softClip (x) : softClip (x * negDrive) * invNegDrive;
    }

    State first, second;
    Coeffs c1, c2;
    float g = 0.0f, lpHpScale = 1.0f;
    float wLp = 0.0f, wBp = 1.0f, wHp = 0.0f, wNotch = 0.0f;
    float stateLimit = 4.0f, invStateLimit = 0.25f;
    float negDrive = 1.0f, invNegDrive = 1.0f;
};

} // namespace flux
