// Runs every factory preset through the real plugin (parameters → processor → field) and checks
// the result: finite, never over 0 dBFS with the limiter, not silent, not far louder than the input.
// Build & run:  cmake --build <build-dir> --target FluxPresetCheck && <build-dir>/FluxPresetCheck
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
    // Pink-ish noise and a saw chord, 4 s each, stereo.
    juce::AudioBuffer<float> makeInput (int kind, double sr, int seconds)
    {
        juce::AudioBuffer<float> b (2, (int) sr * seconds);
        std::mt19937 rng (5);
        std::normal_distribution<float> d (0.0f, 0.2f);
        float s0[2] {}, s1[2] {}, s2[2] {};
        double ph[3] {};
        const double hz[3] = { 110.0, 164.81, 220.0 };
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            if (kind == 0)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float w = d (rng);
                    s0[ch] = 0.99765f * s0[ch] + w * 0.0990460f;
                    s1[ch] = 0.96300f * s1[ch] + w * 0.2965164f;
                    s2[ch] = 0.57000f * s2[ch] + w * 1.0526913f;
                    b.setSample (ch, i, (s0[ch] + s1[ch] + s2[ch] + w * 0.1848f) * 0.35f);
                }
            }
            else
            {
                float x = 0.0f;
                for (int k = 0; k < 3; ++k) { ph[k] += hz[k] / sr; ph[k] -= std::floor (ph[k]); x += (float) (2.0 * ph[k] - 1.0) * 0.2f; }
                b.setSample (0, i, x);
                b.setSample (1, i, x);
            }
        }
        return b;
    }

    double rmsDb (const juce::AudioBuffer<float>& b, int from)
    {
        double sum = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = from; i < b.getNumSamples(); ++i)
                sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return 10.0 * std::log10 (sum / (double) (b.getNumChannels() * (b.getNumSamples() - from)) + 1e-20);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;   // message manager for the parameter listeners
    constexpr double sr = 48000.0;
    constexpr int block = 256, seconds = 4;
    int failures = 0;

    FluxFilterAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    std::printf ("%-18s %14s %14s %9s\n", "preset", "noise rms/peak", "saw rms/peak", "vs input");

    for (int p = 0; p < proc.presets.numFactory(); ++p)
    {
        proc.presets.load (p);
        juce::String line;
        double worstGain = -200.0;
        bool ok = true;
        for (int kind = 0; kind < 2; ++kind)
        {
            proc.prepareToPlay (sr, block);
            auto buf = makeInput (kind, sr, seconds);
            const double inDb = rmsDb (buf, (int) sr);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < buf.getNumSamples(); pos += block)
            {
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, juce::jmin (block, buf.getNumSamples() - pos));
                proc.processBlock (view, midi);
            }
            float peak = 0.0f;
            bool finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    const float v = buf.getSample (ch, i);
                    finite &= std::isfinite (v);
                    peak = std::max (peak, std::abs (v));
                }
            const double outDb = rmsDb (buf, (int) sr);
            worstGain = std::max (worstGain, outDb - inDb);
            line << juce::String (outDb, 1) << "/" << juce::String (20.0 * std::log10 (peak + 1e-9f), 1) << " dB  ";
            if (! finite || peak > 1.0001f || outDb < -50.0 || outDb - inDb > 6.0)
            {
                ok = false;
                std::printf ("FAIL: %s on %s: finite %d, peak %.3f, rms %.1f dB (input %.1f dB)\n",
                             proc.presets.nameOf (p).toRawUTF8(), kind == 0 ? "noise" : "saw", (int) finite, peak, outDb, inDb);
            }
        }
        failures += ok ? 0 : 1;
        std::printf ("%-18s %s %+6.1f dB\n", proc.presets.nameOf (p).toRawUTF8(), line.toRawUTF8(), worstGain);
    }
    proc.releaseResources();

    std::printf (failures == 0 ? "ALL PRESETS PASSED\n" : "%d PRESET(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
