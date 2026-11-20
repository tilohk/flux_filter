// Offline tests for the mod sources, the matrix and their effect on the field (no JUCE needed).
// Build & run:  cmake --build <build-dir> --target FluxModTest && <build-dir>/FluxModTest
#include "dsp/FluxField.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace flux;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr float sr = 48000.0f;
static constexpr int tickLen = FluxField::controlInterval;
static constexpr float ticksPerSec = sr / (float) tickLen;
static constexpr float dt = 1.0f / ticksPerSec;

using Matrix = ModMatrix<24, 2>;

// Runs the matrix alone for `seconds`, feeding `input(t)` as the per-sample input level.
template <typename Input, typename OnTick>
static void runMatrix (Matrix& m, float seconds, Input input, OnTick onTick, PropagationSettings prop = {}, int cells = 12)
{
    const int ticks = (int) (seconds * ticksPerSec);
    for (int k = 0; k < ticks; ++k)
    {
        for (int i = 0; i < tickLen; ++i)
            m.pushInput (input ((float) (k * tickLen + i) / sr));
        m.tickSources (dt, 0.0f);
        m.computeSlots();
        m.propagate (prop, cells);
        onTick ((float) k * dt);
    }
}

static int countRisingZeroCrossings (const std::vector<float>& v)
{
    int n = 0;
    for (size_t i = 1; i < v.size(); ++i) if (v[i - 1] < 0.0f && v[i] >= 0.0f) ++n;
    return n;
}

int main()
{
    // 1. Parameter normalisation matches the knob: centre value sits at 0.5, round trip is exact.
    {
        const auto& c = spec::get (spec::FP::Center);
        CHECK (std::abs (c.to01 (600.0f) - 0.5f) < 1e-4f, "center skew: 600 Hz at %.4f", c.to01 (600.0f));
        float worst = 0.0f;
        for (float v : { 20.0f, 100.0f, 600.0f, 3000.0f, 12000.0f })
            worst = std::max (worst, std::abs (c.from01 (c.to01 (v)) - v) / v);
        CHECK (worst < 1e-4f, "center round trip error %.6f", worst);
        std::printf ("param spec: center 600 Hz → %.3f, round-trip error %.2e\n", c.to01 (600.0f), worst);
    }

    // 2. LFO: rate, polarity, tempo sync locked to the host position.
    {
        ModLfo lfo; lfo.reset (1);
        LfoSettings s; s.rateHz = 1.0f;
        Transport t;
        std::vector<float> v;
        for (int k = 0; k < (int) (10 * ticksPerSec); ++k) v.push_back (lfo.advance (s, s.rateHz, t, dt));
        const int cycles = countRisingZeroCrossings (v);
        std::printf ("lfo 1 Hz: %d cycles in 10 s\n", cycles);
        CHECK (cycles >= 9 && cycles <= 10, "lfo 1 Hz gave %d cycles in 10 s", cycles);

        s.unipolar = true; float lo = 9, hi = -9;
        for (int k = 0; k < 3000; ++k) { const float x = lfo.advance (s, 1.0f, t, dt); lo = std::min (lo, x); hi = std::max (hi, x); }
        CHECK (lo >= 0.0f && lo < 0.01f && hi <= 1.0f && hi > 0.99f, "unipolar range %.3f..%.3f", lo, hi);

        LfoSettings sy; sy.sync = true; sy.division = 4;   // 1/4
        Transport play; play.playing = true; play.bpm = 120.0;
        ModLfo l2; l2.reset (2);
        play.ppq = 0.25; const float atQuarter = l2.advance (sy, 0.0f, play, dt);
        play.ppq = 3.0;  const float atBeat = l2.advance (sy, 0.0f, play, dt);
        std::printf ("lfo sync 1/4: value %.3f a quarter into the beat, %.3f on the beat\n", atQuarter, atBeat);
        CHECK (std::abs (atQuarter - 1.0f) < 1e-3f && std::abs (atBeat) < 1e-3f, "sync not locked to ppq (%.3f, %.3f)", atQuarter, atBeat);
    }

    // 3. Drawable shapes: evaluation, save/load round trip, custom LFO output follows the curve.
    {
        CurveShape c { { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } } };
        CHECK (std::abs (c.evaluate (0.25f) - 0.5f) < 1e-4f && std::abs (c.evaluate (0.5f) - 1.0f) < 1e-4f, "curve evaluate");
        CurveShape bent = c; bent.points[1].curve = 0.8f;
        CHECK (bent.evaluate (0.25f) < 0.3f, "positive curve should start slowly (%.3f)", bent.evaluate (0.25f));

        const auto d = CurveShape::defaultShape();
        const auto back = CurveShape::fromString (d.toString());
        bool same = back.points.size() == d.points.size();
        for (size_t i = 0; same && i < d.points.size(); ++i)
            same = std::abs (back.points[i].x - d.points[i].x) < 1e-4f && std::abs (back.points[i].y - d.points[i].y) < 1e-4f
                && std::abs (back.points[i].curve - d.points[i].curve) < 1e-4f;
        CHECK (same, "shape string round trip");
        CHECK (CurveShape::fromString ("garbage").points.size() == 2, "bad shape string should fall back to a flat line");

        ModLfo lfo; lfo.reset (3);
        lfo.curveExchange().publish (c);
        LfoSettings s; s.shape = ModLfoShape::Custom; s.rateHz = 1.0f;
        Transport t; float peak = -9.0f, peakAt = 0.0f;
        for (int k = 0; k < (int) ticksPerSec; ++k)
        {
            const float x = lfo.advance (s, 1.0f, t, dt);
            if (x > peak) { peak = x; peakAt = (float) (k + 1) * dt; }
        }
        std::printf ("custom lfo (triangle drawn by points): peak %.3f at %.3f s\n", peak, peakAt);
        CHECK (peak > 0.99f && std::abs (peakAt - 0.5f) < 0.01f, "custom shape not used (peak %.3f at %.3f)", peak, peakAt);
    }

    // 4. Envelope follower: transient spikes on the hit, body follows the sustain.
    {
        Matrix m; m.prepare (sr, ticksPerSec);
        ModSettings s; m.setSettings (s);
        std::mt19937 rng (1); std::uniform_real_distribution<float> u (0.0f, 0.5f);
        float envAt = 0, transAtHit = 0, transLater = 0, bodyEarly = 0, bodyLate = 0;
        runMatrix (m, 2.0f, [&] (float t) { return t >= 0.5f && t < 1.5f ? u (rng) : 0.0f; },
                   [&] (float t) {
                       if (t >= 0.5f && t < 0.53f) transAtHit = std::max (transAtHit, m.source (ModSource::Transient));
                       if (std::abs (t - 1.0f) < dt) { transLater = m.source (ModSource::Transient); envAt = m.source (ModSource::Env); bodyLate = m.source (ModSource::Body); }
                       if (std::abs (t - 0.52f) < dt) bodyEarly = m.source (ModSource::Body);
                   });
        std::printf ("envelope: env %.2f, transient %.2f at the hit → %.2f later, body %.2f early → %.2f later\n",
                     envAt, transAtHit, transLater, bodyEarly, bodyLate);
        CHECK (envAt > 0.8f, "env too low (%.2f)", envAt);
        CHECK (transAtHit > 0.5f && transLater < 0.15f, "transient should spike then fall (%.2f → %.2f)", transAtHit, transLater);
        CHECK (bodyLate > bodyEarly + 0.1f, "body should build up (%.2f → %.2f)", bodyEarly, bodyLate);
    }

    // 5. Chaos stays bounded and keeps moving; drift S&H holds between steps.
    {
        ChaosLorenz ch; ch.reset();
        float lo = 9, hi = -9;
        for (int k = 0; k < (int) (20 * ticksPerSec); ++k) { ch.advance (0.5f, 0.5f, dt); lo = std::min (lo, ch.outX()); hi = std::max (hi, ch.outX()); }
        std::printf ("chaos x range over 20 s: %.2f..%.2f\n", lo, hi);
        CHECK (hi - lo > 0.8f && lo >= -1.0f && hi <= 1.0f, "chaos range %.2f..%.2f", lo, hi);

        DriftSource d; d.reset (5);
        int changes = 0; float prev = d.advance (2.0f, true, 0.0f, dt);
        for (int k = 0; k < (int) (5 * ticksPerSec); ++k) { const float x = d.advance (2.0f, true, 0.0f, dt); if (x != prev) ++changes; prev = x; }
        std::printf ("drift S&H 2 Hz, no slew: %d value changes in 5 s\n", changes);
        CHECK (changes >= 9 && changes <= 11, "S&H should step 10 times in 5 s (%d)", changes);
    }

    // 6. Matrix: depth, curve, via.
    {
        Matrix m; m.prepare (sr, ticksPerSec);
        ModSettings s;
        s.macros = { 1.0f, 0.0f, 0.5f, 0.0f };
        s.slots[0] = { ModSource::Macro1, ModTarget::Mix, 0.5f, ModCurve::Linear, false, ModSource::None };
        s.slots[1] = { ModSource::Macro1, ModTarget::Mix, 0.25f, ModCurve::Linear, false, ModSource::Macro2 };   // via 0 → nothing
        s.slots[2] = { ModSource::Macro3, ModTarget::Tilt, 1.0f, ModCurve::Exp, false, ModSource::None };       // 0.5² = 0.25
        m.setSettings (s);
        runMatrix (m, 0.01f, [] (float) { return 0.0f; }, [] (float) {});
        std::printf ("matrix: mix offset %.3f, tilt offset %.3f\n", m.global (ModTarget::Mix), m.global (ModTarget::Tilt));
        CHECK (std::abs (m.global (ModTarget::Mix) - 0.5f) < 1e-6f, "mix offset %.3f", m.global (ModTarget::Mix));
        CHECK (std::abs (m.global (ModTarget::Tilt) - 0.25f) < 1e-6f, "exp curve offset %.3f", m.global (ModTarget::Tilt));
        CHECK (std::abs (Matrix::modulated (spec::FP::Mix, 0.8f, 0.5f) - 1.0f) < 1e-6f, "modulated value must clamp to range");
    }

    // 7. The Flux switch: a slot on a per-cell target travels through the field; without Flux it doesn't.
    for (bool flux : { false, true })
    {
        Matrix m; m.prepare (sr, ticksPerSec);
        ModSettings s;
        s.lfo[0].rateHz = 0.5f;
        s.slots[0] = { ModSource::Lfo1, ModTarget::Cutoff, 0.3f, ModCurve::Linear, flux, ModSource::None };
        m.setSettings (s);
        PropagationSettings p { 0.5f, FluxDirection::Up, 0.0f, 0.0f, 0.0f, 0.0f };
        std::vector<float> first, last;
        runMatrix (m, 6.0f, [] (float) { return 0.0f; },
                   [&] (float) { first.push_back (m.cell (ModTarget::Cutoff, 0, 0)); last.push_back (m.cell (ModTarget::Cutoff, 11, 0)); }, p);
        int best = 0; double bestC = -1e9;
        for (int lag = 0; lag <= (int) ticksPerSec; ++lag)
        {
            double c = 0; for (size_t i = first.size() / 2; i < first.size(); ++i) c += first[i - (size_t) lag] * last[i];
            if (c > bestC) { bestC = c; best = lag; }
        }
        const float lagSec = (float) best / ticksPerSec;
        std::printf ("matrix LFO → cutoff, flux %s: last cell lags first by %.3f s\n", flux ? "on " : "off", lagSec);
        CHECK (std::abs (lagSec - (flux ? 0.5f : 0.0f)) < 0.01f, "flux %d lag %.3f", (int) flux, lagSec);
    }

    // 8. In the field: a macro can turn the effect off via Mix; LFO → cutoff moves the cells.
    {
        FieldSettings fs; fs.mix = 1.0f; fs.limiter = false; fs.fluxDepth = 0.0f; fs.drift = 0.0f;
        ModSettings ms; ms.macros[0] = 1.0f;
        ms.slots[0] = { ModSource::Macro1, ModTarget::Mix, -1.0f, ModCurve::Linear, false, ModSource::None };
        FluxField f; f.setSettings (fs); f.setModSettings (ms); f.prepare (sr);
        std::mt19937 rng (3); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        std::vector<float> in (48000), out; for (auto& x : in) x = u (rng);
        out = in; for (size_t i = 0; i < out.size(); i += 256) f.process (out.data() + i, nullptr, (int) std::min<size_t> (256, out.size() - i));
        float diff = 0.0f; for (size_t i = 24000; i < in.size(); ++i) diff = std::max (diff, std::abs (out[i] - in[i]));
        std::printf ("field: macro → mix -1 leaves dry signal, max diff %.2e\n", diff);
        CHECK (diff < 1e-5f, "macro to mix -1 should give dry (diff %g)", diff);

        ModSettings lm; lm.lfo[0].rateHz = 1.0f;
        lm.slots[0] = { ModSource::Lfo1, ModTarget::Cutoff, 0.2f, ModCurve::Linear, false, ModSource::None };
        FluxField g; g.setSettings (fs); g.setModSettings (lm); g.prepare (sr);
        float lo = 1e9f, hi = 0.0f; std::vector<float> buf (tickLen);
        for (int k = 0; k < (int) (2 * ticksPerSec); ++k)
        {
            std::fill (buf.begin(), buf.end(), 0.0f); g.process (buf.data(), nullptr, tickLen);
            lo = std::min (lo, g.getCellFrequencyNow (5)); hi = std::max (hi, g.getCellFrequencyNow (5));
        }
        std::printf ("field: LFO → cutoff 0.2 sweeps cell 5 over %.2f octaves\n", std::log2 (hi / lo));
        CHECK (std::log2 (hi / lo) > 1.0f, "LFO → cutoff barely moves the cell (%.2f oct)", std::log2 (hi / lo));
    }

    // 9. Stress: every slot busy with random routings at full depth, all flux, extreme field.
    {
        std::mt19937 rng (11);
        float worst = 0.0f; bool finite = true;
        for (int run = 0; run < 8; ++run)
        {
            ModSettings ms;
            for (auto& sl : ms.slots)
                sl = { (ModSource) (1 + (int) (rng() % ((int) ModSource::Count - 1))),
                       (ModTarget) (1 + (int) (rng() % ((int) ModTarget::Count - 1))),
                       (rng() % 2 ? 1.0f : -1.0f), (ModCurve) (rng() % 4), true,
                       (ModSource) (rng() % (int) ModSource::Count) };
            for (auto& l : ms.lfo) { l.rateHz = 20.0f; l.shape = (ModLfoShape) (rng() % 7); }
            ms.chaosSpeed = 1.0f; ms.driftRateHz = 10.0f; ms.driftSampleHold = true; ms.driftSlew = 0.0f;
            ms.macros = { 1.0f, 0.5f, 0.0f, 1.0f };
            FieldSettings fs; fs.density = 24; fs.resonance = 1.0f; fs.feedback = 1.0f; fs.character = (Character) (run % 3);
            FluxField f; f.setSettings (fs); f.setModSettings (ms); f.prepare (sr);
            std::uniform_real_distribution<float> u (-1.0f, 1.0f);
            std::vector<float> l (48000 * 2), r (48000 * 2); for (auto& x : l) x = u (rng); for (auto& x : r) x = u (rng);
            for (size_t i = 0; i < l.size(); i += 128) f.process (l.data() + i, r.data() + i, 128);
            for (float x : l) { finite &= std::isfinite (x); worst = std::max (worst, std::abs (x)); }
            for (float x : r) { finite &= std::isfinite (x); worst = std::max (worst, std::abs (x)); }
        }
        std::printf ("stress: 8 runs with 16 random full-depth flux slots, peak %.3f\n", worst);
        CHECK (finite && worst <= 1.0f, "stress failed (finite %d, peak %.3f)", (int) finite, worst);
    }

    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
