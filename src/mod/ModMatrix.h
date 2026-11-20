#pragma once

#include "ModSources.h"
#include "dsp/FluxPropagator.h"
#include "params/ParamSpec.h"

#include <array>
#include <cmath>

namespace flux
{

enum class ModSource : int
{
    None = 0, FluxLfo, Lfo1, Lfo2, Lfo3, Env, Transient, Body, ChaosX, ChaosY, Drift,
    Macro1, Macro2, Macro3, Macro4, Count
};
inline constexpr const char* modSourceNames[] = {
    "None", "Flux LFO", "LFO 1", "LFO 2", "LFO 3", "Envelope", "Transient", "Body",
    "Chaos X", "Chaos Y", "Drift", "Macro 1", "Macro 2", "Macro 3", "Macro 4"
};

// Targets. The first four after None act per cell, so the Flux switch can send them through the field
// as a wave. Everything else is a global parameter (Flux has no effect there).
enum class ModTarget : int
{
    None = 0,
    Cutoff, Resonance, Morph, CellLevel,                       // per cell
    Spread, Tilt, Combine, Width, Drift, Feedback, Damping,
    FluxRate, FluxDepth, FluxLevel, Travel, Viscosity, Wavelength, Turbulence, FluxStereo,
    Drive, Mix, Output,
    Lfo1Rate, Lfo2Rate, Lfo3Rate, ChaosSpeed, DriftRate,
    Count
};
inline constexpr const char* modTargetNames[] = {
    "None", "Cutoff (cells)", "Resonance (cells)", "Morph (cells)", "Level (cells)",
    "Spread", "Tilt", "Parallel/Serial", "Width", "Drift", "Feedback", "Damping",
    "Flux Rate", "Flux Depth", "Flux Level", "Travel", "Viscosity", "Wavelength", "Turbulence", "Flux Stereo",
    "Drive", "Mix", "Output",
    "LFO 1 Rate", "LFO 2 Rate", "LFO 3 Rate", "Chaos Speed", "Drift Rate"
};

inline constexpr int numCellTargets = 4;   // Cutoff, Resonance, Morph, CellLevel
inline constexpr bool isCellTarget (ModTarget t) noexcept { return t >= ModTarget::Cutoff && t <= ModTarget::CellLevel; }

// Which parameter range a target modulates (CellLevel has none: it is a plain gain offset).
inline spec::FP targetParam (ModTarget t) noexcept
{
    using spec::FP;
    switch (t)
    {
        case ModTarget::Cutoff:     return FP::Center;
        case ModTarget::Resonance:  return FP::Resonance;
        case ModTarget::Morph:      return FP::Morph;
        case ModTarget::Spread:     return FP::Spread;
        case ModTarget::Tilt:       return FP::Tilt;
        case ModTarget::Combine:    return FP::Combine;
        case ModTarget::Width:      return FP::Width;
        case ModTarget::Drift:      return FP::Drift;
        case ModTarget::Feedback:   return FP::Feedback;
        case ModTarget::Damping:    return FP::Damping;
        case ModTarget::FluxRate:   return FP::FluxRate;
        case ModTarget::FluxDepth:  return FP::FluxDepth;
        case ModTarget::FluxLevel:  return FP::FluxLevel;
        case ModTarget::Travel:     return FP::Travel;
        case ModTarget::Viscosity:  return FP::Viscosity;
        case ModTarget::Wavelength: return FP::Wavelength;
        case ModTarget::Turbulence: return FP::Turbulence;
        case ModTarget::FluxStereo: return FP::FluxStereo;
        case ModTarget::Drive:      return FP::Drive;
        case ModTarget::Mix:        return FP::Mix;
        case ModTarget::Output:     return FP::Output;
        case ModTarget::Lfo1Rate:   return FP::Lfo1Rate;
        case ModTarget::Lfo2Rate:   return FP::Lfo2Rate;
        case ModTarget::Lfo3Rate:   return FP::Lfo3Rate;
        case ModTarget::ChaosSpeed: return FP::ChaosSpeed;
        case ModTarget::DriftRate:  return FP::DriftRate;
        case ModTarget::None:
        case ModTarget::CellLevel:
        case ModTarget::Count:      return FP::Count;
    }
    return FP::Count;
}

enum class ModCurve : int { Linear = 0, Exp, Log, SCurve };
inline constexpr const char* modCurveNames[] = { "Linear", "Exp", "Log", "S-Curve" };

struct SlotSettings
{
    ModSource source = ModSource::None;
    ModTarget target = ModTarget::None;
    float depth = 0.0f;              // -1..1, in the target's normalised (knob) range
    ModCurve curve = ModCurve::Linear;
    bool flux = false;               // per-cell targets: travel through the field as a wave
    ModSource via = ModSource::None; // scales this slot by another source (mod of mod)
};

struct ModSettings
{
    static constexpr int numLfos = 3, numSlots = 16, numMacros = 4;
    std::array<LfoSettings, numLfos> lfo {};
    float envAttackMs = 5.0f, envReleaseMs = 120.0f, envSensDb = 0.0f;
    float chaosSpeed = 0.3f, chaosShape = 0.3f;
    float driftRateHz = 0.3f, driftSlew = 0.5f;
    bool  driftSampleHold = false;
    std::array<float, numMacros> macros {};
    std::array<SlotSettings, numSlots> slots {};
};

// Mod sources + matrix, evaluated once per control tick by the field.
//   tickSources()  – advance every source (env follower is fed per sample via pushInput)
//   computeSlots() – sum all slots into global offsets, per-cell offsets and flux inputs
//   propagate()    – send the flux inputs through the field (same Travel/Direction/... as the wave)
template <int MaxCells, int MaxChannels = 2>
class ModMatrix
{
public:
    void prepare (float sampleRate, float ticksPerSecond)
    {
        sr = sampleRate;
        env.prepare (sampleRate);
        for (int i = 0; i < ModSettings::numLfos; ++i)
            lfos[(size_t) i].reset (0x1F123BB5u + 977u * (uint32_t) i);
        chaos.reset();
        drift.reset (0x6C8E9CF5u);
        for (auto& p : propagators) p.prepare (ticksPerSecond);
        values.fill (0.0f);
        globalOffset.fill (0.0f);
        cellDirect.fill (0.0f);
        fluxInput.fill (0.0f);
        samplesSinceBlock = 0;
    }

    void setSettings (const ModSettings& s) noexcept { settings = s; }

    // Once per audio block, before processing it.
    void setTransport (const Transport& t) noexcept { blockTransport = t; samplesSinceBlock = 0; }

    // Thread-safe: may be called from the message thread while audio runs.
    void setCustomShape (int lfoIndex, const CurveShape& shape)
    {
        if (lfoIndex >= 0 && lfoIndex < ModSettings::numLfos)
            lfos[(size_t) lfoIndex].curveExchange().publish (shape);
    }

    // Per input sample (max of |L|, |R|).
    void pushInput (float absLevel) noexcept
    {
        env.push (absLevel);
        ++samplesSinceBlock;
    }

    void tickSources (float dt, float fluxLfoValue) noexcept
    {
        Transport t = blockTransport;
        t.ppq += (double) samplesSinceBlock / (double) sr * t.bpm / 60.0;

        auto rate = [this] (spec::FP p, float base, ModTarget tgt) {
            return modulated (p, base, globalOffset[(size_t) tgt]);   // last tick's offsets
        };

        values[(size_t) ModSource::None] = 0.0f;
        values[(size_t) ModSource::FluxLfo] = fluxLfoValue;

        const spec::FP rateParams[] = { spec::FP::Lfo1Rate, spec::FP::Lfo2Rate, spec::FP::Lfo3Rate };
        const ModTarget rateTargets[] = { ModTarget::Lfo1Rate, ModTarget::Lfo2Rate, ModTarget::Lfo3Rate };
        for (int i = 0; i < ModSettings::numLfos; ++i)
        {
            const auto& ls = settings.lfo[(size_t) i];
            const float hz = rate (rateParams[i], ls.rateHz, rateTargets[i]);
            values[(size_t) ModSource::Lfo1 + (size_t) i] = lfos[(size_t) i].advance (ls, hz, t, dt);
        }

        env.setTimes (settings.envAttackMs, settings.envReleaseMs);
        env.compute (settings.envSensDb, values[(size_t) ModSource::Env], values[(size_t) ModSource::Body],
                     values[(size_t) ModSource::Transient]);

        chaos.advance (rate (spec::FP::ChaosSpeed, settings.chaosSpeed, ModTarget::ChaosSpeed), settings.chaosShape, dt);
        values[(size_t) ModSource::ChaosX] = chaos.outX();
        values[(size_t) ModSource::ChaosY] = chaos.outY();

        values[(size_t) ModSource::Drift] = drift.advance (rate (spec::FP::DriftRate, settings.driftRateHz, ModTarget::DriftRate),
                                                           settings.driftSampleHold, settings.driftSlew, dt);

        for (int i = 0; i < ModSettings::numMacros; ++i)
            values[(size_t) ModSource::Macro1 + (size_t) i] = settings.macros[(size_t) i];
    }

    void computeSlots() noexcept
    {
        globalOffset.fill (0.0f);
        cellDirect.fill (0.0f);
        fluxInput.fill (0.0f);

        for (const auto& slot : settings.slots)
        {
            if (slot.source == ModSource::None || slot.target == ModTarget::None || slot.depth == 0.0f)
                continue;

            float amount = slot.depth * applyCurve (slot.curve, source (slot.source));
            if (slot.via != ModSource::None)
                amount *= source (slot.via);

            if (isCellTarget (slot.target))
            {
                const auto k = (size_t) slot.target - (size_t) ModTarget::Cutoff;
                (slot.flux ? fluxInput[k] : cellDirect[k]) += amount;
            }
            else
            {
                globalOffset[(size_t) slot.target] += amount;
            }
        }
    }

    void propagate (const PropagationSettings& p, int numCells) noexcept
    {
        for (int k = 0; k < numCellTargets; ++k)
            propagators[(size_t) k].process (fluxInput[(size_t) k], p, numCells);
    }

    // Normalised offset for a global target.
    float global (ModTarget t) const noexcept { return globalOffset[(size_t) t]; }

    // Normalised offset for a per-cell target (direct slots + the flux wave at this cell).
    float cell (ModTarget t, int c, int ch) const noexcept
    {
        const auto k = (size_t) t - (size_t) ModTarget::Cutoff;
        return cellDirect[k] + propagators[k].get (c, ch);
    }

    float source (ModSource s) const noexcept { return values[(size_t) s]; }

    // base (natural units) + offset (normalised) → natural units, clamped to the parameter's range.
    static float modulated (spec::FP p, float base, float offset) noexcept
    {
        if (offset == 0.0f || p == spec::FP::Count)
            return base;
        const auto& s = spec::get (p);
        return s.from01 (s.to01 (base) + offset);
    }

    static float applyCurve (ModCurve c, float v) noexcept
    {
        v = std::clamp (v, -1.0f, 1.0f);
        switch (c)
        {
            case ModCurve::Exp:    return v * std::abs (v);
            case ModCurve::Log:    return std::copysign (std::sqrt (std::abs (v)), v);
            case ModCurve::SCurve: return v * (1.5f - 0.5f * v * v);
            case ModCurve::Linear:
            default:               return v;
        }
    }

private:
    float sr = 48000.0f;
    ModSettings settings;
    Transport blockTransport;
    int samplesSinceBlock = 0;

    std::array<ModLfo, ModSettings::numLfos> lfos;
    EnvelopeFollower env;
    ChaosLorenz chaos;
    DriftSource drift;

    std::array<float, (size_t) ModSource::Count> values {};
    std::array<float, (size_t) ModTarget::Count> globalOffset {};
    std::array<float, numCellTargets> cellDirect {}, fluxInput {};
    std::array<FluxPropagator<MaxCells, MaxChannels>, numCellTargets> propagators;
};

static_assert (sizeof (modSourceNames) / sizeof (modSourceNames[0]) == (size_t) ModSource::Count);
static_assert (sizeof (modTargetNames) / sizeof (modTargetNames[0]) == (size_t) ModTarget::Count);

} // namespace flux
