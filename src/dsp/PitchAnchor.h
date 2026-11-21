#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Pitch anchor: pulls the cells onto notes.
//   Scale:  the nearest note of a key and scale (or the nearest harmonic of the key's root).
//   Follow: the nearest harmonic of the input's pitch, found by a YIN detector.
namespace flux
{

enum class AnchorMode { Off = 0, Scale, Follow };
enum class AnchorScale { Chromatic = 0, Major, Minor, Pentatonic, Harmonic };

namespace anchor
{
    inline constexpr const char* keyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    inline constexpr const char* scaleNames[] = { "Chromatic", "Major", "Minor", "Pentatonic", "Harmonic" };

    // log2 of a frequency → MIDI note number (A4 = 69 = 440 Hz).
    inline float octToMidi (float oct) noexcept { return 69.0f + 12.0f * (oct - 8.78135971f); }   // log2 (440)
    inline float midiToOct (float midi) noexcept { return 8.78135971f + (midi - 69.0f) / 12.0f; }

    // Nearest harmonic n × f0 (n ≥ 1) of the pitch f0Oct (log2 Hz).
    inline float snapToHarmonic (float oct, float f0Oct) noexcept
    {
        const float ratio = std::exp2 (oct - f0Oct);
        const float lo = std::max (1.0f, std::floor (ratio));
        const float best = std::abs (std::log2 (lo / ratio)) <= std::abs (std::log2 ((lo + 1.0f) / ratio)) ? lo : lo + 1.0f;
        return f0Oct + std::log2 (best);
    }

    // Nearest note (in log2 Hz) of key (0 = C) and scale to oct. Harmonic: nearest n × the key's
    // root in the octave starting at MIDI 24 (C1 ≈ 32.7 Hz).
    inline float snapToScale (float oct, int key, AnchorScale scale) noexcept
    {
        static constexpr int major[] = { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
        static constexpr int penta[] = { 0, 3, 5, 7, 10 };   // minor pentatonic

        if (scale == AnchorScale::Harmonic)
            return snapToHarmonic (oct, midiToOct (24.0f + (float) key));

        const float midi = octToMidi (oct);
        if (scale == AnchorScale::Chromatic)
            return midiToOct (std::round (midi));

        const int* degrees = scale == AnchorScale::Major ? major : scale == AnchorScale::Minor ? minor : penta;
        const int count = scale == AnchorScale::Pentatonic ? 5 : 7;
        const float rel = midi - (float) key;
        const float octave = std::floor (rel / 12.0f);
        float best = 0.0f, bestDist = 1.0e9f;
        for (int o = -1; o <= 1; ++o)
            for (int i = 0; i < count; ++i)
            {
                const float note = 12.0f * (octave + (float) o) + (float) degrees[i];
                const float d = std::abs (note - rel);
                if (d < bestDist) { bestDist = d; best = note; }
            }
        return midiToOct (best + (float) key);
    }

} // namespace anchor

// YIN pitch detector on a decimated mono signal (~12 kHz), 50–1000 Hz. push() every sample;
// analyse() about every 20 ms. Unpitched or quiet passages keep the last pitch.
class PitchDetector
{
public:
    static constexpr float minHz = 50.0f, maxHz = 1000.0f;

    void prepare (float sampleRate)
    {
        decimation = std::max (1, (int) std::lround (sampleRate / 12000.0f));
        rate = sampleRate / (float) decimation;
        maxLag = (int) std::ceil (rate / minHz);
        minLag = std::max (2, (int) std::floor (rate / maxHz));
        window = maxLag;                                    // integration window = longest period
        buffer.assign ((size_t) (window + maxLag + 2), 0.0f);
        diff.assign ((size_t) (maxLag + 2), 0.0f);
        // Anti-alias: two one-poles at ~2.5 kHz before decimating.
        lpCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 2500.0f / sampleRate);
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        lp1 = lp2 = 0.0f; phase = 0; writePos = 0; filled = 0;
        pitchHz = 0.0f;
    }

    void push (float x) noexcept
    {
        lp1 += (x - lp1) * lpCoef;
        lp2 += (lp1 - lp2) * lpCoef;
        if (++phase < decimation) return;
        phase = 0;
        buffer[(size_t) writePos] = lp2;
        writePos = (writePos + 1) % (int) buffer.size();
        filled = std::min (filled + 1, (int) buffer.size());
    }

    // Returns the current pitch (Hz, 0 = none found yet).
    float analyse() noexcept
    {
        const int n = (int) buffer.size();
        if (filled < n) return pitchHz;
        auto at = [&] (int i) { return buffer[(size_t) ((writePos + i) % n)]; };   // oldest first

        float energy = 0.0f;
        for (int i = 0; i < window; ++i) energy += at (i) * at (i);
        if (energy / (float) window < 1.0e-6f)   // below about -60 dBFS: keep the last pitch
            return pitchHz;

        // Cumulative mean normalised difference (YIN steps 2-3).
        diff[0] = 1.0f;
        float running = 0.0f;
        for (int lag = 1; lag <= maxLag; ++lag)
        {
            float d = 0.0f;
            for (int i = 0; i < window; ++i)
            {
                const float delta = at (i) - at (i + lag);
                d += delta * delta;
            }
            running += d;
            diff[(size_t) lag] = running > 0.0f ? d * (float) lag / running : 1.0f;
        }

        // Absolute threshold (step 4): the first dip under the threshold, then its local minimum.
        int best = -1;
        for (int lag = minLag; lag < maxLag; ++lag)
            if (diff[(size_t) lag] < threshold)
            {
                while (lag + 1 < maxLag && diff[(size_t) lag + 1] < diff[(size_t) lag]) ++lag;
                best = lag;
                break;
            }
        if (best < 0)               // unpitched: keep the last pitch
            return pitchHz;

        // Parabolic interpolation (step 5).
        const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
        pitchHz = rate / ((float) best + std::clamp (shift, -0.5f, 0.5f));
        return pitchHz;
    }

    float getPitch() const noexcept { return pitchHz; }

private:
    static constexpr float threshold = 0.15f;
    int decimation = 4, phase = 0, writePos = 0, filled = 0;
    int window = 240, maxLag = 240, minLag = 12;
    float rate = 12000.0f, lpCoef = 0.3f, lp1 = 0.0f, lp2 = 0.0f, pitchHz = 0.0f;
    std::vector<float> buffer, diff;
};

} // namespace flux
