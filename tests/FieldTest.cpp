// Offline sanity tests for the flux field DSP (no JUCE needed).
// Build & run:  cmake --build <build-dir> --target FluxFieldTest && <build-dir>/FluxFieldTest
#include "dsp/FluxField.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdio>
#include <random>
#include <vector>

using namespace flux;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double sr = 48000.0;

struct Result { float peak = 0.0f; double rms = 0.0; bool finite = true; };

static Result run (FluxField& f, std::vector<float>& l, std::vector<float>* r, int block = 256)
{
    for (size_t i = 0; i < l.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, l.size() - i);
        f.process (l.data() + i, r ? r->data() + i : nullptr, n);
    }
    Result res; double sum = 0.0;
    for (float v : l) { res.finite &= std::isfinite (v); res.peak = std::max (res.peak, std::abs (v)); sum += (double) v * v; }
    res.rms = std::sqrt (sum / (double) l.size());
    return res;
}

static std::vector<float> noise (size_t n, float amp, unsigned seed = 1)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<float> d (-amp, amp);
    std::vector<float> v (n); for (auto& x : v) x = d (rng); return v;
}

static std::vector<float> sine (size_t n, float hz, float amp)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = amp * (float) std::sin (2.0 * M_PI * hz * (double) i / sr);
    return v;
}

static double rmsTail (const std::vector<float>& v) // skip the attack
{
    double s = 0.0; const size_t start = v.size() / 2;
    for (size_t i = start; i < v.size(); ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (v.size() - start));
}

int main()
{
    // 1. Stress: extreme settings, loud noise, settings jumping every block.
    {
        std::mt19937 rng (7); std::uniform_real_distribution<float> u (0.0f, 1.0f);
        float worstUnlimited = 0.0f;
        for (bool cut : { false, true })
        for (int character = 0; character < 3; ++character)
            for (bool limiter : { true, false })
            for (int density : { 1, 7, 24 })
            {
                FluxField f; FieldSettings s;
                s.cut = cut; s.density = density; s.resonance = 1.0f; s.driveDb = 24.0f;
                s.character = (Character) character; s.limiter = limiter;
                s.feedback = 1.0f; s.damping = 0.0f;
                s.fluxAmount = 1.0f; s.wander = 1.0f; s.macroOverride.driftHz = 5.0f; s.macroOverride.coupling = 1.0f; s.rasp = 1.0f;
                s.macroOverride.feed = 0.5f; s.macroOverride.lifespanSec = 0.05f;
                f.setSettings (s); f.prepare (sr);

                auto l = noise ((size_t) sr * 2, 1.0f, 1), r = noise ((size_t) sr * 2, 1.0f, 2);
                for (size_t i = 0; i < l.size(); i += 64)
                {
                    s.vowel = 9.0f * u (rng);
                    s.shiftOct = 2.0f - 4.0f * u (rng);
                    s.scatter = u (rng);
                    s.resonance = u (rng);
                    if (u (rng) < 0.01f) s.cut = ! s.cut;   // the topology switches mid-stream now and then
                    f.setSettings (s);
                    f.process (l.data() + i, r.data() + i, 64);
                }
                Result a, b;
                for (float v : l) { a.finite &= std::isfinite (v); a.peak = std::max (a.peak, std::abs (v)); }
                for (float v : r) { b.finite &= std::isfinite (v); b.peak = std::max (b.peak, std::abs (v)); }
                const float peak = std::max (a.peak, b.peak);
                worstUnlimited = limiter ? worstUnlimited : std::max (worstUnlimited, peak);
                CHECK (a.finite && b.finite, "non-finite output (char %d, density %d)", character, density);
                if (limiter)
                    CHECK (peak <= 1.0f, "limiter let %.3f through (char %d, density %d)", peak, character, density);
                else
                    CHECK (peak <= 4.001f, "wet ceiling broken, level %.2f (char %d, density %d)", peak, character, density);
            }
        std::printf ("stress: 36 extreme combinations (boost and cut) finite, limiter holds; worst peak without limiter %.2f\n", worstUnlimited);
    }

    // Field after one control tick, for checking the layout.
    auto settle = [] (FieldSettings s) {
        s.fluxAmount = 0.0f;   // layout checks: no drift
        auto f = std::make_unique<FluxField>(); f->setSettings (s); f->prepare (sr);
        std::vector<float> tmp (64, 0.0f); f->process (tmp.data(), nullptr, 64);
        return f;
    };
    auto octs = [] (float a, float b) { return std::abs (std::log2 (a / b)); };

    // 2. Formant layout: Density sets the formants, each formant's cells sit close around its centre.
    {
        FieldSettings s; s.density = 9; s.vowel = 0.0f;
        auto f = settle (s);
        const float table[3] = { 650.0f, 1080.0f, 2650.0f };   // "a": F1..F3
        float worst = 0.0f;
        for (int c = 0; c < 9; ++c)
        {
            CHECK (f->getCellFormant (c) == c / 3, "cell %d in formant %d, expected %d", c, f->getCellFormant (c), c / 3);
            worst = std::max (worst, octs (f->getCellFrequency (c), table[c / 3]));
        }
        std::printf ("formants: 9 cells → 3 formants, cells at most %.3f oct from their formant\n", worst);
        CHECK (worst <= (0.5f + 0.15f) / 6.0f + 1e-4f, "cells too far from their formant (%.3f oct)", worst);

        int counts[5] = {};
        FieldSettings d = s; d.density = 16;
        auto g = settle (d);
        for (int c = 0; c < 16; ++c) ++counts[g->getCellFormant (c)];
        CHECK (counts[0] == 4 && counts[1] == 3 && counts[2] == 3 && counts[3] == 3 && counts[4] == 3,
               "16 cells split %d/%d/%d/%d/%d", counts[0], counts[1], counts[2], counts[3], counts[4]);
        for (int n = 1; n <= 16; ++n)
        {
            FieldSettings e = s; e.density = n;
            auto h = settle (e);
            CHECK (h->getCellFormant (n - 1) == formants::countFor (n) - 1, "density %d: last cell not in the top formant", n);
        }

        // Size: x2 throat = every cell an octave lower.
        FieldSettings big = s; big.shiftOct = -1.0f;
        auto b = settle (big);
        float sizeErr = 0.0f;
        for (int c = 0; c < 9; ++c) sizeErr = std::max (sizeErr, std::abs (b->getCellFrequency (c) * 2.0f / f->getCellFrequency (c) - 1.0f));
        CHECK (sizeErr < 1e-4f, "size +1 oct should halve every cell (error %.5f)", sizeErr);

        // Vowel: "i" has a low F1; halfway between "a" and "e" sits between them.
        FieldSettings vi = s; vi.vowel = 2.0f;
        CHECK (octs (settle (vi)->getCellFrequency (1), 290.0f) < 0.04f, "vowel i: F1 cell at %.0f Hz", settle (vi)->getCellFrequency (1));
        FieldSettings ae = s; ae.vowel = 0.5f;
        const float mid = settle (ae)->getCellFrequency (1);
        std::printf ("vowel: a/e halfway F1 cell at %.0f Hz (a 650, e 400)\n", mid);
        CHECK (octs (mid, std::sqrt (650.0f * 400.0f)) < 0.04f, "a/e F1 at %.0f Hz", mid);
        FieldSettings wrap = s; wrap.vowel = 9.0f;   // the ring closes: 9 is "a" again
        CHECK (octs (settle (wrap)->getCellFrequency (1), f->getCellFrequency (1)) < 1e-4f, "vowel ring does not wrap");
    }

    // 3. Scatter: cells leave their formants for free, irregular positions.
    {
        auto range = [&] (float scatter) {
            FieldSettings s; s.density = 3; s.scatter = scatter;
            auto f = settle (s);
            float lo = 1e9f, hi = 0.0f;
            for (int c = 0; c < 3; ++c) { lo = std::min (lo, f->getCellFrequency (c)); hi = std::max (hi, f->getCellFrequency (c)); }
            return std::log2 (hi / lo);
        };
        const float r0 = range (0.0f), r1 = range (1.0f);
        std::printf ("scatter: one formant of 3 cells spans %.2f oct, scattered %.2f oct\n", r0, r1);
        CHECK (r0 < 0.25f, "formant cells should stay together (%.2f oct)", r0);
        CHECK (r1 > 1.0f, "scattered cells should spread out (%.2f oct)", r1);
    }

    // 4. A sine on a formant passes far louder than one between formants.
    {
        FieldSettings s; s.density = 9; s.vowel = 0.0f; s.autoGain = false; s.limiter = false; s.fluxAmount = 0.0f;
        const float onFormant = settle (s)->getCellFrequency (1);
        auto measure = [&] (float hz) {
            FluxField f; f.setSettings (s); f.prepare (sr);
            auto v = sine ((size_t) sr, hz, 0.1f); run (f, v, nullptr); return rmsTail (v);
        };
        const double on = measure (onFormant), off = measure (1800.0f), inRms = 0.1 / std::sqrt (2.0);
        std::printf ("formant peak: %.0f Hz %+.1f dB, 1800 Hz (between F2 and F3) %+.1f dB\n",
                     onFormant, 20.0 * std::log10 (on / inRms), 20.0 * std::log10 (off / inRms));
        CHECK (on > off * 4.0, "formant not pronounced (%.4f vs %.4f)", on, off);
        // Makeup lifts the field by at most +12 dB, so a tone on a formant is at most 12 dB louder.
        CHECK (on < inRms * 4.0, "tone on a formant boosted by %+.1f dB", 20.0 * std::log10 (on / inRms));
    }

    // 4b. Makeup: pink noise comes out a few dB under the input (instead of ~20 dB with narrow
    //     formants), and the makeup never goes past +12 dB.
    {
        auto pink = [] (size_t n) {
            std::mt19937 r (3); std::normal_distribution<float> d (0.0f, 0.2f);
            float b0 = 0, b1 = 0, b2 = 0; std::vector<float> v (n);
            for (auto& x : v)
            {
                const float w = d (r);
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                x = (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
            }
            return v;
        };
        for (float res : { 0.3f, 0.6f, 0.9f })
        {
            FieldSettings s; s.resonance = res; s.autoGain = false; s.limiter = false;
            FluxField f; f.setSettings (s); f.prepare (sr);
            auto v = pink (48000 * 3); const double inRms = rmsTail (v); run (f, v, nullptr);
            const double db = 20.0 * std::log10 (rmsTail (v) / inRms);
            std::printf ("makeup: resonance %.0f %% on pink noise %+.1f dB vs input (makeup %+.1f dB)\n", res * 100.0f, db, f.getMakeupDb());
            CHECK (f.getMakeupDb() >= 0.0f && f.getMakeupDb() <= 12.001f, "makeup out of range (%.1f dB)", f.getMakeupDb());
            if (res <= 0.6f)
                CHECK (db > -9.0 && db < -1.0, "pink noise level %+.1f dB at resonance %.1f", db, res);
        }
    }

    // 5. Mix = 0 is a clean bypass; auto gain only ever turns down.
    {
        FieldSettings s; s.mix = 0.0f;
        FluxField f; f.setSettings (s); f.prepare (sr);
        auto in = noise (48000, 0.5f), v = in; run (f, v, nullptr);
        float maxDiff = 0.0f; for (size_t i = 0; i < v.size(); ++i) maxDiff = std::max (maxDiff, std::abs (v[i] - in[i]));
        CHECK (maxDiff < 1e-6f, "mix=0 not transparent (diff %g)", maxDiff);

        // A filtered (quieter) signal stays quiet, a resonant / driven one that gets louder than the
        // input is brought back to ~3 dB under it.
        auto level = [&] (FieldSettings d) {
            FluxField g; g.setSettings (d); g.prepare (sr);
            auto w = noise (48000 * 3, 0.3f); const double inRms = rmsTail (w); run (g, w, nullptr);
            return 20.0 * std::log10 (rmsTail (w) / inRms);
        };
        FieldSettings quiet; quiet.limiter = false;
        FieldSettings loud = quiet; loud.vowel = 6.0f; loud.shiftOct = -1.5f; loud.density = 4;
        loud.resonance = 0.9f; loud.driveDb = 12.0f; loud.feedback = 0.4f;
        for (auto* d : { &quiet, &loud })
        {
            FieldSettings off = *d, on = *d; off.autoGain = false; on.autoGain = true;
            const double dbOff = level (off), dbOn = level (on);
            std::printf ("%s settings on noise: auto gain off %+.1f dB, on %+.1f dB vs input\n", d == &quiet ? "default" : "loud   ", dbOff, dbOn);
            CHECK (dbOn <= dbOff + 0.1, "auto gain must never turn up (%.1f -> %.1f dB)", dbOff, dbOn);
            if (dbOff > -1.0) CHECK (dbOn < -1.0 && dbOn > -6.0, "auto gain should bring a loud field to ~-3 dB (%.1f dB)", dbOn);
        }
    }

    // 6. Life, chaotic drift: Flux 0 or Wander 0 is a still filter; otherwise the cells wander,
    //    smoothly, further with more Wander, faster with more Drift, the same way on every start.
    {
        struct Trace { std::vector<float> f1, f3; };
        auto trace = [&] (FieldSettings s, float seconds) {
            s.density = 9;
            s.macroOverride.lifespanSec = 1.0e4f; s.macroOverride.feed = 0.0f;   // drift only: no cell dies in these traces
            FluxField f; f.setSettings (s); f.prepare (sr);
            Trace t; std::vector<float> buf (FluxField::controlInterval);
            for (int k = 0; k < (int) (seconds * sr) / FluxField::controlInterval; ++k)
            {
                std::fill (buf.begin(), buf.end(), 0.0f); f.process (buf.data(), nullptr, FluxField::controlInterval);
                t.f1.push_back (std::log2 (f.getCellFrequencyNow (1)));
                t.f3.push_back (std::log2 (f.getCellFrequencyNow (7)));
            }
            return t;
        };
        auto range = [] (const std::vector<float>& v) { return *std::max_element (v.begin(), v.end()) - *std::min_element (v.begin(), v.end()); };
        auto maxStep = [] (const std::vector<float>& v) { float m = 0; for (size_t i = 1; i < v.size(); ++i) m = std::max (m, std::abs (v[i] - v[i - 1])); return m; };
        auto travelled = [] (const std::vector<float>& v) { double d = 0; for (size_t i = 1; i < v.size(); ++i) d += std::abs (v[i] - v[i - 1]); return d; };

        for (auto still : { std::pair { 0.0f, 1.0f }, std::pair { 1.0f, 0.0f } })
        {
            FieldSettings s; s.fluxAmount = still.first; s.wander = still.second; s.macroOverride.driftHz = 3.0f;
            const auto t = trace (s, 4.0f);
            CHECK (range (t.f1) < 1e-5f, "flux %.0f wander %.0f: cells move (%.5f oct)", still.first, still.second, range (t.f1));
        }

        FieldSettings live; live.fluxAmount = 1.0f; live.wander = 1.0f; live.macroOverride.driftHz = 0.5f;
        const auto t = trace (live, 20.0f);
        std::printf ("life: flux 1, wander 1, drift 0.5 Hz: F1 cell roams %.2f oct, F3 cell %.2f oct, largest step %.4f oct/tick\n",
                     range (t.f1), range (t.f3), maxStep (t.f1));
        CHECK (range (t.f1) > 0.5f && range (t.f3) > 0.5f, "cells barely wander (%.2f / %.2f oct)", range (t.f1), range (t.f3));
        CHECK (maxStep (t.f1) < 0.01f, "drift is not smooth (%.4f oct per tick)", maxStep (t.f1));
        // F1 and F3 move on their own, not in lockstep.
        double ab = 0, aa = 0, bb = 0;
        float m1 = 0, m3 = 0; for (size_t i = 0; i < t.f1.size(); ++i) { m1 += t.f1[i]; m3 += t.f3[i]; } m1 /= (float) t.f1.size(); m3 /= (float) t.f3.size();
        for (size_t i = 0; i < t.f1.size(); ++i) { ab += (t.f1[i] - m1) * (t.f3[i] - m3); aa += (t.f1[i] - m1) * (t.f1[i] - m1); bb += (t.f3[i] - m3) * (t.f3[i] - m3); }
        const double corr = ab / std::sqrt (aa * bb + 1e-30);
        std::printf ("life: F1/F3 correlation %.2f\n", corr);
        CHECK (std::abs (corr) < 0.8, "formants move in lockstep (correlation %.2f)", corr);

        FieldSettings half = live; half.wander = 0.25f;
        const float rHalf = range (trace (half, 20.0f).f1);
        CHECK (rHalf < range (t.f1) * 0.5f, "wander 0.25 should roam less (%.2f vs %.2f oct)", rHalf, range (t.f1));

        FieldSettings fast = live; fast.macroOverride.driftHz = 4.0f;
        const double slowPath = travelled (t.f1), fastPath = travelled (trace (fast, 20.0f).f1);
        std::printf ("life: path in 20 s at drift 0.5 Hz %.1f oct, at 4 Hz %.1f oct\n", slowPath, fastPath);
        CHECK (fastPath > slowPath * 3.0, "drift 4 Hz should move much faster (%.1f vs %.1f)", fastPath, slowPath);

        const auto again = trace (live, 20.0f);
        CHECK (again.f1 == t.f1, "the same settings should drift the same way on every start");
    }

    // 6b. Life, energy coupling: a tone on one cell excites its neighbours (fully inside its
    //     formant, partly across the border), far cells stay calm; it lets go after the input
    //     stops; it never runs away on its own and barely changes the level.
    {
        FieldSettings s; s.density = 9; s.fluxAmount = 1.0f; s.wander = 0.0f; s.macroOverride.coupling = 1.0f;
        s.autoGain = false; s.limiter = false;
        const float hz = settle (s)->getCellFrequency (2);   // top cell of F1
        auto excite = [&] (FieldSettings d, float onSec, float offSec) {
            auto f = std::make_unique<FluxField>(); f->setSettings (d); f->prepare (sr);
            std::vector<float> buf (480);
            for (int b = 0; b < (int) ((onSec + offSec) * 100.0f); ++b)
            {
                for (size_t i = 0; i < buf.size(); ++i)
                    buf[i] = b < (int) (onSec * 100.0f) ? 0.2f * (float) std::sin (2.0 * M_PI * hz * (double) (b * 480 + (int) i) / sr) : 0.0f;
                f->process (buf.data(), nullptr, (int) buf.size());
            }
            return f;
        };
        auto f = excite (s, 1.0f, 0.0f);
        std::printf ("coupling: tone on cell 2, excitation of cells 0..8:");
        for (int c = 0; c < 9; ++c) std::printf (" %.2f", f->getCellExcitation (c));
        std::printf ("\n");
        CHECK (f->getCellExcitation (1) > 0.5f, "inner neighbour not excited (%.2f)", f->getCellExcitation (1));
        CHECK (f->getCellExcitation (3) > 0.1f, "neighbour across the formant border not excited (%.2f)", f->getCellExcitation (3));
        for (int c = 5; c < 9; ++c)
            CHECK (f->getCellExcitation (c) < 0.05f, "far cell %d excited (%.2f)", c, f->getCellExcitation (c));

        auto released = excite (s, 1.0f, 5.0f);
        CHECK (released->getCellExcitation (1) < 0.1f, "excitation should let go after the input stops (%.2f)", released->getCellExcitation (1));

        for (auto off : { std::pair { 0.0f, -1.0f }, std::pair { 1.0f, 0.0f } })   // Flux 0 (the macro's coupling), coupling 0
        {
            FieldSettings d = s; d.fluxAmount = off.first; d.macroOverride.coupling = off.second;
            auto g = excite (d, 1.0f, 0.0f);
            float most = 0.0f; for (int c = 0; c < 9; ++c) most = std::max (most, g->getCellExcitation (c));
            CHECK (most == 0.0f, "flux %.0f coupling %.0f should not excite (%.3f)", off.first, off.second, most);
        }

        FieldSettings hot = s; hot.resonance = 1.0f; hot.density = 16;
        FluxField g; g.setSettings (hot); g.prepare (sr);
        auto v = noise (48000, 0.3f); v.resize (48000 * 3, 0.0f); run (g, v, nullptr);
        double tail = 0.0; for (size_t i = 48000 * 2; i < v.size(); ++i) tail += (double) v[i] * v[i];
        CHECK (tail < 1e-6, "full coupling without feedback must not ring on (%.2e)", tail);

        auto level = [&] (float coupling) {
            FieldSettings d = s; d.macroOverride.coupling = coupling; d.resonance = 0.8f;
            FluxField h; h.setSettings (d); h.prepare (sr);
            auto w = noise (48000 * 3, 0.3f, 4); const double in = rmsTail (w); run (h, w, nullptr);
            return 20.0 * std::log10 (rmsTail (w) / in);
        };
        const double l0 = level (0.0f), l1 = level (1.0f);
        std::printf ("coupling: noise level %+.1f dB without, %+.1f dB at full coupling\n", l0, l1);
        CHECK (std::abs (l1 - l0) < 1.5, "coupling changes the level by %.1f dB", l1 - l0);
    }

    // 6c. Life cycle. Chance: cells die and are reborn, more often with a shorter Lifespan, while the
    //     level stays even. Energy: cells fed by the input live, starved ones die, silence kills
    //     all; food brings them back. Flux 0: every cell lives forever.
    {
        auto pinkNoise = [] (size_t n) {
            std::mt19937 r (3); std::normal_distribution<float> d (0.0f, 0.2f);
            float b0 = 0, b1 = 0, b2 = 0; std::vector<float> v (n);
            for (auto& x : v)
            {
                const float w = d (r);
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                x = (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
            }
            return v;
        };
        struct Run { uint32_t births; float meanVitality; double minDb, maxDb, levelDb; };
        auto chance = [&] (float flux, float lifespan) {
            FieldSettings s; s.density = 12; s.fluxAmount = flux; s.wander = 0.0f; s.macroOverride.coupling = 0.0f; s.macroOverride.feed = 0.0f;
            s.macroOverride.lifespanSec = lifespan; s.resonance = 0.3f; s.autoGain = false; s.limiter = false;
            FluxField f; f.setSettings (s); f.prepare (sr);
            auto v = pinkNoise (48000 * 10); const double in = rmsTail (v);
            double vit = 0.0; int n = 0;
            for (size_t i = 0; i < v.size(); i += 480)
            {
                f.process (v.data() + i, nullptr, 480);
                for (int c = 0; c < 12; ++c) vit += f.getCellVitality (c);
                n += 12;
            }
            Run r { f.getBirths(), (float) (vit / n), 1e9, -1e9, 20.0 * std::log10 (rmsTail (v) / in) };
            for (int sec = 2; sec < 10; ++sec)
            {
                double a = 0.0; for (int i = sec * 48000; i < (sec + 1) * 48000; ++i) a += (double) v[(size_t) i] * v[(size_t) i];
                const double db = 10.0 * std::log10 (a / 48000.0);
                r.minDb = std::min (r.minDb, db); r.maxDb = std::max (r.maxDb, db);
            }
            return r;
        };
        const auto still = chance (0.0f, 0.2f), fast = chance (1.0f, 0.2f), slow = chance (1.0f, 2.0f);
        std::printf ("life chance: lifespan 0.2 s %u births in 10 s, 2 s %u births; mean vitality %.2f; level %+.1f dB (flux 0: %+.1f), "
                     "per-second swing %.1f / %.1f dB\n", fast.births, slow.births, fast.meanVitality, fast.levelDb, still.levelDb,
                     fast.maxDb - fast.minDb, slow.maxDb - slow.minDb);
        CHECK (fast.births > 100 && slow.births > 5 && fast.births > slow.births * 5, "births %u / %u", fast.births, slow.births);
        CHECK (fast.meanVitality > 0.25f && fast.meanVitality < 0.8f, "chance population %.2f", fast.meanVitality);
        CHECK (std::abs (fast.levelDb - still.levelDb) < 3.0, "life changes the level by %.1f dB", fast.levelDb - still.levelDb);
        CHECK (fast.maxDb - fast.minDb < 3.0 && slow.maxDb - slow.minDb < 3.0, "level swings %.1f / %.1f dB", fast.maxDb - fast.minDb, slow.maxDb - slow.minDb);
        // Vitality runs at Flux 0 too, but has no effect: cells never move or fade.
        {
            FieldSettings a; a.fluxAmount = 0.0f; a.macroOverride.feed = 0.0f; a.macroOverride.lifespanSec = 0.1f; a.density = 9;
            auto f = settle (a);   // settle forces Flux 0
            FluxField g; g.setSettings (a); g.prepare (sr);
            std::vector<float> buf (480, 0.0f);
            float worst = 0.0f;
            for (int b = 0; b < 300; ++b)
            {
                std::fill (buf.begin(), buf.end(), 0.0f); g.process (buf.data(), nullptr, 480);
                for (int c = 0; c < 9; ++c) worst = std::max (worst, std::abs (std::log2 (g.getCellFrequencyNow (c) / f->getCellFrequency (c))));
            }
            CHECK (worst < 1e-5f, "flux 0: reborn cells must not move (%.5f oct)", worst);
        }

        FieldSettings e; e.density = 9; e.fluxAmount = 1.0f; e.wander = 0.0f; e.macroOverride.coupling = 0.3f; e.macroOverride.feed = 1.0f; e.macroOverride.lifespanSec = 0.5f;
        const float hz = settle (e)->getCellFrequency (1);   // middle of F1
        FluxField f; f.setSettings (e); f.prepare (sr);
        std::vector<float> buf (480);
        auto play = [&] (float seconds, bool tone, int& tick) {
            for (int b = 0; b < (int) (seconds * 100.0f); ++b, ++tick)
            {
                for (size_t i = 0; i < buf.size(); ++i)
                    buf[i] = tone ? 0.2f * (float) std::sin (2.0 * M_PI * hz * (double) (tick * 480 + (int) i) / sr) : 0.0f;
                f.process (buf.data(), nullptr, (int) buf.size());
            }
        };
        int tick = 0;
        play (3.0f, true, tick);
        std::printf ("life energy: tone on F1 for 3 s, vitality:");
        for (int c = 0; c < 9; ++c) std::printf (" %.2f", f.getCellVitality (c));
        std::printf ("\n");
        for (int c = 0; c < 3; ++c) CHECK (f.getCellVitality (c) > 0.9f, "fed F1 cell %d died (%.2f)", c, f.getCellVitality (c));
        for (int c = 6; c < 9; ++c) CHECK (f.getCellVitality (c) < 0.1f, "starved F3 cell %d lives (%.2f)", c, f.getCellVitality (c));
        play (3.0f, false, tick);
        float most = 0.0f; for (int c = 0; c < 9; ++c) most = std::max (most, f.getCellVitality (c));
        CHECK (most < 0.1f, "silence should starve the population (%.2f)", most);
        const auto before = f.getBirths();
        play (1.0f, true, tick);
        CHECK (f.getBirths() > before, "food should bring cells back");
        std::printf ("life energy: after 3 s silence the most vital cell %.2f; tone again: %u births\n", most, f.getBirths() - before);
    }

    // 6d. Rasp: growl adds the sub-octave of a tone, jitter shakes the cells quickly but not far,
    //     the level stays; Rasp 0 or Flux 0 is clean.
    {
        auto goertzelDb = [] (const std::vector<float>& v, size_t a, size_t b, double hz) {
            const double w = 2.0 * M_PI * hz / sr, c = 2.0 * std::cos (w); double s1 = 0, s2 = 0;
            for (size_t i = a; i < b; ++i) { const double s0 = v[i] + c * s1 - s2; s2 = s1; s1 = s0; }
            return 10.0 * std::log10 ((s1 * s1 + s2 * s2 - c * s1 * s2) / ((double) (b - a) * (double) (b - a)) + 1e-30);
        };
        FieldSettings base; base.density = 9; base.fluxAmount = 1.0f; base.wander = 0.0f; base.macroOverride.coupling = 0.0f;
        base.macroOverride.feed = 0.0f; base.macroOverride.lifespanSec = 1.0e4f; base.autoGain = false; base.limiter = false;
        const float hz = settle (base)->getCellFrequency (1);
        auto subOctave = [&] (float flux, float rasp) {
            FieldSettings s = base; s.fluxAmount = flux; s.rasp = rasp;
            FluxField f; f.setSettings (s); f.prepare (sr);
            auto v = sine (48000 * 2, hz, 0.1f); run (f, v, nullptr);
            return goertzelDb (v, 48000, 96000, hz * 0.5) - goertzelDb (v, 48000, 96000, hz);
        };
        const double clean = subOctave (1.0f, 0.0f), off = subOctave (0.0f, 1.0f), rough = subOctave (1.0f, 1.0f);
        std::printf ("rasp: sub-octave of a %.0f Hz tone vs fundamental: rasp 0 %+.1f dB, flux 0 %+.1f dB, rasp 1 %+.1f dB\n", hz, clean, off, rough);
        CHECK (clean < -40.0 && off < -40.0, "rasp should be off (%.1f / %.1f dB)", clean, off);
        CHECK (rough > -10.0, "growl too weak (%.1f dB)", rough);

        FieldSettings s = base; s.rasp = 1.0f;
        FluxField f; f.setSettings (s); f.prepare (sr);
        std::vector<float> buf (FluxField::controlInterval);
        std::vector<float> octs;
        for (int k = 0; k < 3000; ++k)
        {
            std::fill (buf.begin(), buf.end(), 0.0f); f.process (buf.data(), nullptr, FluxField::controlInterval);
            octs.push_back (std::log2 (f.getCellFrequencyNow (1)));
        }
        double step = 0.0; for (size_t i = 1; i < octs.size(); ++i) step += std::abs (octs[i] - octs[i - 1]);
        step /= (double) (octs.size() - 1);
        const float spread = *std::max_element (octs.begin(), octs.end()) - *std::min_element (octs.begin(), octs.end());
        std::printf ("rasp: jitter moves cell 1 by %.4f oct per tick on average, %.3f oct in all\n", step, spread);
        CHECK (step > 0.003 && spread < 0.2f, "jitter %.4f oct per tick, range %.3f oct", step, spread);

        auto level = [&] (float rasp) {
            FieldSettings d = base; d.rasp = rasp;
            FluxField g; g.setSettings (d); g.prepare (sr);
            auto w = noise (48000 * 3, 0.3f, 5); const double in = rmsTail (w); run (g, w, nullptr);
            return 20.0 * std::log10 (rmsTail (w) / in);
        };
        const double l0 = level (0.0f), l1 = level (1.0f);
        std::printf ("rasp: noise level %+.1f dB clean, %+.1f dB at full rasp\n", l0, l1);
        CHECK (std::abs (l1 - l0) < 2.0, "rasp changes the level by %.1f dB", l1 - l0);
    }

    // 6e. The stereo image stays as it came in: a mono signal in a stereo track comes out mono
    //     (bit-identical L/R) with everything alive, and a stereo signal keeps its sides apart.
    {
        FieldSettings s; s.density = 16; s.fluxAmount = 1.0f; s.wander = 1.0f; s.rasp = 1.0f; s.scatter = 0.4f;
        s.feedback = 0.5f; s.character = Character::Broken;
        FluxField f; f.setSettings (s); f.prepare (sr);
        auto l = noise (48000 * 2, 0.3f, 3), r = l;
        run (f, l, &r);
        float diff = 0.0f;
        for (size_t i = 0; i < l.size(); ++i) diff = std::max (diff, std::abs (l[i] - r[i]));
        std::printf ("stereo: mono in, everything on: largest L/R difference %.2g\n", diff);
        CHECK (diff == 0.0f, "a mono signal should stay mono (L/R differ by %.3g)", diff);
    }

    // 6f. The Flux macro: calm → chaotic on every curve, and it really drives the life processes.
    {
        const auto calm = FluxMacro::at (0.0f), mid = FluxMacro::at (0.5f), wild = FluxMacro::at (1.0f);
        std::printf ("flux macro: drift %.2f / %.2f / %.2f Hz, lifespan %.2f / %.2f / %.2f s, coupling %.2f / %.2f / %.2f, feed %.2f / %.2f / %.2f\n",
                     calm.driftHz, mid.driftHz, wild.driftHz, calm.lifespanSec, mid.lifespanSec, wild.lifespanSec,
                     calm.coupling, mid.coupling, wild.coupling, calm.feed, mid.feed, wild.feed);
        CHECK (calm.driftHz < mid.driftHz && mid.driftHz < wild.driftHz, "drift should speed up with Flux");
        CHECK (calm.lifespanSec > mid.lifespanSec && mid.lifespanSec > wild.lifespanSec, "lives should shorten with Flux");
        CHECK (calm.coupling == 0.0f && wild.coupling == 1.0f && mid.coupling > 0.0f, "coupling should rise from 0 to 1");
        CHECK (calm.feed > wild.feed, "high Flux should lean more on chance");

        // Births in 4 s of noise: none when calm, many when wild.
        auto births = [&] (float flux) {
            FieldSettings d; d.density = 12; d.fluxAmount = flux;
            FluxField g; g.setSettings (d); g.prepare (sr);
            auto v = noise (48000 * 4, 0.3f, 9); run (g, v, nullptr);
            return g.getBirths();
        };
        const auto b0 = births (0.0f), b5 = births (0.5f), b1 = births (1.0f);
        std::printf ("flux macro: births in 4 s of noise: %u at Flux 0, %u at 0.5, %u at 1\n", b0, b5, b1);
        CHECK (b0 <= b5 && b5 < b1 && b1 > 20, "births should grow with Flux (%u, %u, %u)", b0, b5, b1);
    }

    // 7. Feedback near the top self-oscillates (sustains after the input stops), stays bounded,
    //    and auto gain does not pump the tail up.
    {
        FieldSettings s; s.feedback = 1.0f; s.damping = 0.0f; s.resonance = 0.9f; s.vowel = 7.0f; s.shiftOct = -1.0f;
        FluxField f; f.setSettings (s); f.prepare (sr);
        auto v = noise (48000, 0.3f);                      // 1 s of input...
        v.resize (48000 * 4, 0.0f);                        // ...then 3 s of silence
        run (f, v, nullptr);
        double tail = 0.0; for (size_t i = 48000 * 3; i < v.size(); ++i) tail += (double) v[i] * v[i];
        tail = std::sqrt (tail / 48000.0);
        float peak = 0.0f; for (float x : v) peak = std::max (peak, std::abs (x));
        std::printf ("feedback 1.0: tail rms %.4f after 2 s of silence, peak %.3f, auto gain %.2f\n", tail, peak, f.getAutoGain());
        CHECK (tail > 1e-3, "no self-oscillation at feedback 1.0 (tail %.6f)", tail);
        CHECK (peak <= 1.0f, "feedback peak %.3f", peak);

        FieldSettings q = s; q.feedback = 0.0f;
        FluxField g; g.setSettings (q); g.prepare (sr);
        auto w = noise (48000, 0.3f); w.resize (48000 * 4, 0.0f); run (g, w, nullptr);
        double tail0 = 0.0; for (size_t i = 48000 * 3; i < w.size(); ++i) tail0 += (double) w[i] * w[i];
        CHECK (tail0 < 1e-6, "tail without feedback should die out (%.8f)", tail0);
    }

    // 8. Cut mode: every cell is a notch, the field never boosts.
    {
        FieldSettings s; s.cut = true; s.autoGain = false; s.limiter = false; s.fluxAmount = 0.0f; s.character = Character::Clean;
        auto gainAt = [&] (FieldSettings d, float hz) {
            FluxField f; f.setSettings (d); f.prepare (sr);
            auto v = sine (48000, hz, 0.25f);
            run (f, v, nullptr);
            return (float) (20.0 * std::log10 (rmsTail (v) / (0.25 / std::sqrt (2.0)) + 1e-12));
        };

        auto centreOf = [] (FieldSettings d) {
            FluxField probe; probe.setSettings (d); probe.prepare (sr);
            std::vector<float> tmp (64, 0.0f); probe.process (tmp.data(), nullptr, 64);
            return probe.getCellFrequencyNow (0);
        };

        // One cell: a deep notch at its centre, untouched two octaves away.
        FieldSettings one = s; one.density = 1;
        const float centre = centreOf (one);
        const float atCentre = gainAt (one, centre), far = gainAt (one, centre * 4.0f);
        std::printf ("cut, one cell at %.0f Hz: %.1f dB at the centre, %.2f dB two octaves up\n", centre, atCentre, far);
        CHECK (atCentre < -25.0f, "cut notch too shallow (%.1f dB)", atCentre);
        CHECK (std::abs (far) < 1.0f, "cut changes the sound away from the notch (%.2f dB)", far);

        // Deeper with more Resonance.
        FieldSettings deep = one; deep.resonance = 1.0f;
        FieldSettings shallow = one; shallow.resonance = 0.0f;
        const float dDeep = gainAt (deep, centreOf (deep)), dShallow = gainAt (shallow, centreOf (shallow));
        std::printf ("cut depth: %.1f dB at Resonance 0, %.1f dB at Resonance 1\n", dShallow, dDeep);
        CHECK (dDeep < dShallow - 10.0f, "Resonance should deepen the cut (%.1f vs %.1f dB)", dDeep, dShallow);

        // A full field never lifts a tone by more than a couple of dB anywhere (sweep 50 Hz…12 kHz).
        FieldSettings full = s; full.density = 16;
        float worst = -100.0f, deepest = 0.0f;
        for (float hz = 50.0f; hz < 12000.0f; hz *= 1.04f)
        {
            const float g = gainAt (full, hz);
            worst = std::max (worst, g);
            deepest = std::min (deepest, g);
        }
        std::printf ("cut, 16 cells: loudest point %+.2f dB, deepest %.1f dB\n", worst, deepest);
        CHECK (worst < 0.3f, "cut field boosts by %.2f dB", worst);
        CHECK (deepest < -20.0f, "cut field has no deep notch (%.1f dB)", deepest);

        // Feedback in cut mode never self-oscillates: the tail dies.
        FieldSettings fb = full; fb.feedback = 1.0f; fb.damping = 0.0f; fb.resonance = 1.0f;
        FluxField f; f.setSettings (fb); f.prepare (sr);
        auto v = noise (48000, 0.3f); v.resize (48000 * 3, 0.0f);
        const auto res = run (f, v, nullptr);
        double tail = 0.0; for (size_t i = 48000 * 2; i < v.size(); ++i) tail += (double) v[i] * v[i];
        std::printf ("cut, feedback 1.0: tail energy %.2e, peak %.2f\n", tail, res.peak);
        CHECK (res.finite && tail < 1e-6, "cut mode feedback should not sustain (%.8f)", tail);
    }

    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
