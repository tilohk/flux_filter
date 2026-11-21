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
        for (int character = 0; character < 3; ++character)
            for (bool limiter : { true, false })
            for (int density : { 1, 7, 24 })
            {
                FluxField f; FieldSettings s;
                s.density = density; s.resonance = 1.0f; s.driveDb = 24.0f;
                s.character = (Character) character; s.limiter = limiter;
                s.feedback = 1.0f; s.damping = 0.0f; s.width = 1.0f;
                f.setSettings (s); f.prepare (sr);

                auto l = noise ((size_t) sr * 2, 1.0f, 1), r = noise ((size_t) sr * 2, 1.0f, 2);
                for (size_t i = 0; i < l.size(); i += 64)
                {
                    s.vowel = 9.0f * u (rng);
                    s.sizeOct = -2.0f + 4.0f * u (rng);
                    s.scatter = u (rng);
                    s.resonance = u (rng);
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
        std::printf ("stress: 18 extreme combinations finite, limiter holds; worst peak without limiter %.2f\n", worstUnlimited);
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
        FieldSettings s; s.density = 9; s.vowel = 0.0f; s.width = 0.0f;
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
        FieldSettings big = s; big.sizeOct = 1.0f;
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
            FieldSettings s; s.density = 3; s.width = 0.0f; s.scatter = scatter;
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
        FieldSettings s; s.density = 9; s.vowel = 0.0f; s.width = 0.0f; s.autoGain = false; s.limiter = false; s.fluxAmount = 0.0f;
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
            FieldSettings s; s.resonance = res; s.autoGain = false; s.limiter = false; s.width = 0.0f;
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
        FieldSettings loud = quiet; loud.vowel = 6.0f; loud.sizeOct = 1.5f; loud.density = 4;
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
            s.width = 0.0f; s.density = 9;
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
            FieldSettings s; s.fluxAmount = still.first; s.wander = still.second; s.driftHz = 3.0f;
            const auto t = trace (s, 4.0f);
            CHECK (range (t.f1) < 1e-5f, "flux %.0f wander %.0f: cells move (%.5f oct)", still.first, still.second, range (t.f1));
        }

        FieldSettings live; live.fluxAmount = 1.0f; live.wander = 1.0f; live.driftHz = 0.5f;
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

        FieldSettings fast = live; fast.driftHz = 4.0f;
        const double slowPath = travelled (t.f1), fastPath = travelled (trace (fast, 20.0f).f1);
        std::printf ("life: path in 20 s at drift 0.5 Hz %.1f oct, at 4 Hz %.1f oct\n", slowPath, fastPath);
        CHECK (fastPath > slowPath * 3.0, "drift 4 Hz should move much faster (%.1f vs %.1f)", fastPath, slowPath);

        const auto again = trace (live, 20.0f);
        CHECK (again.f1 == t.f1, "the same settings should drift the same way on every start");
    }

    // 7. Feedback near the top self-oscillates (sustains after the input stops), stays bounded,
    //    and auto gain does not pump the tail up.
    {
        FieldSettings s; s.feedback = 1.0f; s.damping = 0.0f; s.resonance = 0.9f; s.vowel = 7.0f; s.sizeOct = 1.0f;
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

    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
