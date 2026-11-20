#pragma once

#include "FluxLookAndFeel.h"
#include "PluginProcessor.h"

namespace flux::ui
{

// Live bar for one mod source: bipolar sources fill from the middle, unipolar from the left.
class SourceMeter : public juce::Component, private juce::Timer
{
public:
    SourceMeter (FluxFilterAudioProcessor& p, ModSource s, juce::String name, bool isBipolar)
        : processor (p), source (s), label (std::move (name)), bipolar (isBipolar)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (label, r.removeFromLeft (64.0f), juce::Justification::centredLeft);

        auto bar = r.withSizeKeepingCentre (r.getWidth(), 6.0f);
        g.setColour (colours::track);
        g.fillRoundedRectangle (bar, 3.0f);

        const float v = juce::jlimit (-1.0f, 1.0f, value);
        const float from = bipolar ? 0.5f : 0.0f;
        const float to = bipolar ? 0.5f + 0.5f * v : v;
        const float x0 = bar.getX() + bar.getWidth() * juce::jmin (from, to);
        const float x1 = bar.getX() + bar.getWidth() * juce::jmax (from, to);
        g.setColour (colours::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (x0, bar.getY(), juce::jmax (2.0f, x1 - x0), bar.getHeight()), 3.0f);
    }

private:
    void timerCallback() override
    {
        const float v = processor.getModSourceValue (source);
        if (std::abs (v - value) > 0.002f) { value = v; repaint(); }
    }

    FluxFilterAudioProcessor& processor;
    ModSource source;
    juce::String label;
    bool bipolar;
    float value = 0.0f;
};

// Editor for a drawable LFO shape.
//   drag a point          move it (first and last point only move up/down)
//   shift + drag a point  bend the segment that ends at it
//   double-click empty    add a point
//   double-click a point  remove it
class CurveEditor : public juce::Component, private juce::Timer
{
public:
    CurveEditor (FluxFilterAudioProcessor& p, int lfo) : processor (p), lfoIndex (lfo)
    {
        shape = processor.getCustomShape (lfoIndex);
        lastSaved = shape.toString();
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::background);
        g.fillRoundedRectangle (r, 6.0f);

        const auto plot = area();
        g.setColour (colours::panelEdge);
        for (int i = 1; i < 4; ++i)
            g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) i / 4.0f), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

        juce::Path line, fill;
        constexpr int steps = 160;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / (float) steps;
            const auto pt = toScreen (x, shape.evaluate (x));
            if (i == 0) { line.startNewSubPath (pt); fill.startNewSubPath (pt.x, plot.getBottom()); }
            else line.lineTo (pt);
            fill.lineTo (pt);
        }
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.closeSubPath();
        g.setColour (colours::accent.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (colours::accent);
        g.strokePath (line, juce::PathStrokeType (1.6f));

        for (size_t i = 0; i < shape.points.size(); ++i)
        {
            const auto pt = toScreen (shape.points[i].x, shape.points[i].y);
            g.setColour ((int) i == dragIndex ? colours::text : colours::accent);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt));
        }

        // Live value of this LFO on the right edge (shows where the shape currently is).
        const float v = processor.getModSourceValue ((ModSource) ((int) ModSource::Lfo1 + lfoIndex));
        const float y = plot.getBottom() - juce::jlimit (0.0f, 1.0f, 0.5f * (v + 1.0f)) * plot.getHeight();
        g.setColour (colours::accent2);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getRight() - 6.0f, y }));

        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("drag  /  shift-drag bends  /  double-click adds or removes", r.reduced (6.0f, 3.0f).toNearestInt(),
                    juce::Justification::bottomLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragIndex = hitPoint (e.position);
        dragStartCurve = dragIndex > 0 ? shape.points[(size_t) dragIndex].curve : 0.0f;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIndex < 0)
            return;
        auto& pt = shape.points[(size_t) dragIndex];

        if (e.mods.isShiftDown())
        {
            if (dragIndex > 0)   // bends the segment ending here: drag down sags it, drag up bulges it
                pt.curve = juce::jlimit (-1.0f, 1.0f, dragStartCurve + (float) e.getDistanceFromDragStartY() / 80.0f);
        }
        else
        {
            const auto v = fromScreen (e.position);
            pt.y = v.y;
            const bool isEnd = dragIndex == 0 || dragIndex == (int) shape.points.size() - 1;
            if (! isEnd)
            {
                const float lo = shape.points[(size_t) dragIndex - 1].x + 0.005f;
                const float hi = shape.points[(size_t) dragIndex + 1].x - 0.005f;
                pt.x = juce::jlimit (lo, hi, v.x);
            }
        }
        commit();
    }

    void mouseUp (const juce::MouseEvent&) override { dragIndex = -1; repaint(); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hit = hitPoint (e.position);
        if (hit > 0 && hit < (int) shape.points.size() - 1)
            shape.points.erase (shape.points.begin() + hit);
        else if (hit < 0 && shape.points.size() < (size_t) CurveShape::maxPoints)
        {
            const auto v = fromScreen (e.position);
            shape.points.push_back ({ v.x, v.y, 0.0f });
        }
        commit();
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (8.0f, 8.0f).withTrimmedBottom (10.0f).withTrimmedRight (6.0f); }

    juce::Point<float> toScreen (float x, float y) const
    {
        const auto a = area();
        return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
    }

    juce::Point<float> fromScreen (juce::Point<float> p) const
    {
        const auto a = area();
        return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()),
                 juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
    }

    int hitPoint (juce::Point<float> p) const
    {
        for (size_t i = 0; i < shape.points.size(); ++i)
            if (toScreen (shape.points[i].x, shape.points[i].y).getDistanceFrom (p) < 9.0f)
                return (int) i;
        return -1;
    }

    void commit()
    {
        shape.sanitise();
        processor.setCustomShape (lfoIndex, shape);
        lastSaved = shape.toString();
        repaint();
    }

    void timerCallback() override
    {
        // Pick up shapes loaded from a project or preset (not while the user is dragging).
        if (dragIndex < 0)
        {
            auto current = processor.getCustomShape (lfoIndex);
            const auto s = current.toString();
            if (s != lastSaved) { shape = std::move (current); lastSaved = s; }
        }
        repaint();   // live value marker
    }

    FluxFilterAudioProcessor& processor;
    int lfoIndex;
    CurveShape shape;
    std::string lastSaved;
    int dragIndex = -1;
    float dragStartCurve = 0.0f;
};

} // namespace flux::ui
