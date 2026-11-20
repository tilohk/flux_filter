#pragma once

#include "FluxLookAndFeel.h"
#include "PluginProcessor.h"

namespace flux::ui
{

// Live picture of the field: every cell as a resonance peak at its current frequency.
// Peak width follows the cell's Q, height its level (tilt, flux level, matrix level).
// Left channel teal, right channel magenta; where they overlap the image is stereo-identical.
class FieldView : public juce::Component, private juce::Timer
{
public:
    explicit FieldView (FluxFilterAudioProcessor& p) : processor (p) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 8.0f);

        const auto plot = r.reduced (12.0f, 10.0f).withTrimmedBottom (14.0f);
        drawGrid (g, plot);

        const auto& snap = processor.getFieldSnapshot();
        const int cells = juce::jlimit (0, FluxField::maxCells, snap.activeCells.load (std::memory_order_relaxed));
        const juce::Colour chColour[2] = { colours::accent, colours::accent2 };

        for (int ch = 1; ch >= 0; --ch)   // right first, left on top
            for (int c = 0; c < cells; ++c)
            {
                const float f = snap.freq[(size_t) ch][(size_t) c].load (std::memory_order_relaxed);
                const float q = snap.q[(size_t) ch][(size_t) c].load (std::memory_order_relaxed);
                const float level = snap.level[(size_t) ch][(size_t) c].load (std::memory_order_relaxed);
                if (f <= 0.0f || level <= 0.001f)
                    continue;
                drawPeak (g, plot, f, q, level, chColour[ch]);
            }

        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (cells) + " cells", r.reduced (12.0f, 6.0f).toNearestInt(), juce::Justification::topRight);
    }

private:
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;

    static float xFor (const juce::Rectangle<float>& plot, float hz)
    {
        const float t = std::log (hz / minHz) / std::log (maxHz / minHz);
        return plot.getX() + t * plot.getWidth();
    }

    void drawGrid (juce::Graphics& g, const juce::Rectangle<float>& plot)
    {
        g.setFont (juce::FontOptions (10.0f));
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xFor (plot, hz);
            const bool major = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
            g.setColour (colours::panelEdge.withAlpha (major ? 1.0f : 0.5f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            if (major || hz == 500.0f || hz == 5000.0f || hz == 50.0f)
            {
                g.setColour (colours::dim);
                const juce::String txt = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
                g.drawText (txt, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 12.0f), juce::Justification::centred);
            }
        }
        g.setColour (colours::panelEdge);
        g.drawHorizontalLine ((int) plot.getBottom(), plot.getX(), plot.getRight());
    }

    static void drawPeak (juce::Graphics& g, const juce::Rectangle<float>& plot, float hz, float q, float level, juce::Colour colour)
    {
        // Width in octaves: a resonance's -3 dB bandwidth is about 1.44/Q octaves; widened a bit to stay visible.
        const float bwOct = juce::jlimit (0.025f, 2.0f, 1.2f / juce::jmax (q, 0.1f));
        const float height = juce::jlimit (0.0f, 1.0f, level / 2.0f) * plot.getHeight() * 0.92f;
        const float centreOct = std::log2 (hz);

        juce::Path p;
        constexpr int steps = 40;
        bool started = false;
        for (int i = 0; i <= steps; ++i)
        {
            const float o = centreOct + bwOct * 4.0f * ((float) i / (float) steps * 2.0f - 1.0f);
            const float hzAt = std::exp2 (o);
            if (hzAt < minHz || hzAt > maxHz) continue;
            const float d = (o - centreOct) / bwOct;
            const float y = plot.getBottom() - height / (1.0f + d * d);
            const float x = xFor (plot, hzAt);
            if (! started) { p.startNewSubPath (x, plot.getBottom()); started = true; }
            p.lineTo (x, y);
        }
        if (! started) return;
        p.lineTo (p.getCurrentPosition().x, plot.getBottom());
        p.closeSubPath();

        g.setColour (colour.withAlpha (0.18f));
        g.fillPath (p);
        g.setColour (colour.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    void timerCallback() override { repaint(); }

    FluxFilterAudioProcessor& processor;
};

} // namespace flux::ui
