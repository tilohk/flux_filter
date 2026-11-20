#pragma once

#include <cmath>
#include <cstdint>

namespace flux
{

enum class LfoShape { Sine = 0, Triangle, Ramp, Random };

// The built-in flux LFO, evaluated at control rate. Output is bipolar -1..1.
// It drives the main flux wave; LFO 1-3 (mod/ModSources.h) are the user's modulation sources.
class FluxLfo
{
public:
    void reset() noexcept
    {
        phase = 0.0f;
        rngState = 0x2545F491u;
        prevRandom = 0.0f;
        nextRandom = nextBipolar();
    }

    // dt: seconds per call (one control tick).
    float advance (float rateHz, LfoShape shape, float dt) noexcept
    {
        phase += rateHz * dt;
        while (phase >= 1.0f)
        {
            phase -= 1.0f;
            prevRandom = nextRandom;
            nextRandom = nextBipolar();
        }
        constexpr float twoPi = 6.28318530718f;
        switch (shape)
        {
            case LfoShape::Triangle: return 1.0f - 4.0f * std::abs (phase - 0.5f);
            case LfoShape::Ramp:     return 2.0f * phase - 1.0f;
            case LfoShape::Random:
            {
                // New random target every cycle, glided with a cosine curve: wandering, never jumping.
                const float t = 0.5f - 0.5f * std::cos (phase * 3.14159265f);
                return prevRandom + (nextRandom - prevRandom) * t;
            }
            case LfoShape::Sine:
            default:                 return std::sin (twoPi * phase);
        }
    }

private:
    float nextBipolar() noexcept
    {
        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
        return (float) (rngState >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    float phase = 0.0f;
    uint32_t rngState = 0x2545F491u;
    float prevRandom = 0.0f, nextRandom = 0.0f;
};

} // namespace flux
