#pragma once

#include "FormantTable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// The field's autonomous movement: smooth, bounded, never repeating. Runs at control rate.
//   - Throat position (vowel, openness): a slowed-down Lorenz attractor. It circles one lobe for
//     a while, then switches to the other at unpredictable moments, so the throat wanders between
//     two vowel neighbourhoods instead of sweeping.
//   - Size and each formant's own offset: smooth value noise (random targets, cosine-interpolated,
//     two layers at unrelated rates), decorrelated from each other.
// All outputs are about -1..1. Drift (Hz) sets the speed of everything.
namespace flux
{

class SmoothNoise
{
public:
    void seed (uint32_t s) noexcept
    {
        state = s != 0 ? s : 0x2545F491u;
        for (auto& l : layers) { l.a = next(); l.b = next(); l.phase = 0.5f * (next() + 1.0f); }
    }

    float advance (float rateHz, float dt) noexcept
    {
        constexpr float pi = 3.14159265358979f;
        constexpr float mult[2] = { 1.0f, 1.618034f }, weight[2] = { 0.65f, 0.35f };
        float out = 0.0f;
        for (size_t i = 0; i < layers.size(); ++i)
        {
            auto& l = layers[i];
            l.phase += rateHz * mult[i] * dt;
            while (l.phase >= 1.0f) { l.phase -= 1.0f; l.a = l.b; l.b = next(); }
            out += weight[i] * (l.a + (l.b - l.a) * (0.5f - 0.5f * std::cos (pi * l.phase)));
        }
        return out;
    }

private:
    float next() noexcept   // uniform -1..1 (xorshift32)
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    struct Layer { float a = 0.0f, b = 0.0f, phase = 0.0f; };
    std::array<Layer, 2> layers {};
    uint32_t state = 0x2545F491u;
};

class Lorenz
{
public:
    void reset (float x0 = 1.0f, float y0 = 1.0f, float z0 = 20.0f) noexcept { x = x0; y = y0; z = z0; }

    // One orbit around a lobe takes about 0.7 time units; rateHz orbits per second.
    void advance (float rateHz, float dt) noexcept
    {
        float h = 0.7f * rateHz * dt;
        const int steps = std::max (1, (int) std::ceil (h / 0.004f));   // keep the integration step small
        h /= (float) steps;
        for (int i = 0; i < steps; ++i)
        {
            // Midpoint (RK2) step.
            const float dx1 = sigma * (y - x), dy1 = x * (rho - z) - y, dz1 = x * y - beta * z;
            const float xm = x + 0.5f * h * dx1, ym = y + 0.5f * h * dy1, zm = z + 0.5f * h * dz1;
            x += h * sigma * (ym - xm);
            y += h * (xm * (rho - zm) - ym);
            z += h * (xm * ym - beta * zm);
        }
    }

    float outX() const noexcept { return std::clamp (x / 18.0f, -1.0f, 1.0f); }            // lobe and swing
    float outZ() const noexcept { return std::clamp ((z - 24.0f) / 14.0f, -1.0f, 1.0f); }  // height

private:
    static constexpr float sigma = 10.0f, rho = 28.0f, beta = 8.0f / 3.0f;
    float x = 1.0f, y = 1.0f, z = 20.0f;
};

class FluxChaos
{
public:
    struct Output
    {
        float vowel = 0.0f, openness = 0.0f, size = 0.0f;
        std::array<float, formants::numFormants> formant {};
    };

    // Deterministic start: the same settings move the same way every time playback starts.
    void reset() noexcept
    {
        throat.reset (1.0f, 1.0f, 20.0f);
        size.seed (0x6A09E667u);
        for (size_t k = 0; k < formantNoise.size(); ++k)
            formantNoise[k].seed (0xBB67AE85u + 0x9E3779B9u * (uint32_t) k);
        out = {};
    }

    const Output& advance (float driftHz, float dt) noexcept
    {
        throat.advance (driftHz, dt);
        out.vowel = throat.outX();
        out.openness = throat.outZ();
        out.size = size.advance (0.7f * driftHz, dt);
        for (size_t k = 0; k < formantNoise.size(); ++k)
            out.formant[k] = formantNoise[k].advance (driftHz * (1.0f + 0.13f * (float) k), dt);
        return out;
    }

    const Output& get() const noexcept { return out; }

private:
    Lorenz throat;
    SmoothNoise size;
    std::array<SmoothNoise, formants::numFormants> formantNoise;
    Output out;
};

} // namespace flux
