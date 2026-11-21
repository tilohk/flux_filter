#pragma once

#include "FluxLookAndFeel.h"
#include "ModContext.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace flux::ui
{

// Rounded panel with a title, shared by every section of the editor.
inline void paintPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                        juce::Colour titleColour = colours::text, juce::Colour edge = colours::panelEdge, int titleIndent = 10)
{
    const auto r = area.toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (titleColour);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title.toUpperCase(), area.removeFromTop (28).withTrimmedLeft (titleIndent), juce::Justification::centredLeft);
}

enum class KnobSize { Small, Normal, Hero };

// One parameter as a control: knob for numbers, drop-down for choices, switch for on/off.
// Knobs are also drop targets for mod sources: their connections show as coloured rings
// (one per source, from the knob value to value + depth) plus a dot for the live position.
// Clicking a knob selects it, so the modulation panel shows its connections.
class ParamControl : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    // choiceAsKnob: show a choice parameter as a stepped knob instead of a drop-down.
    ParamControl (ModContext& context, const juce::String& id, const juce::String& labelText = {},
                  KnobSize knobSize = KnobSize::Normal, bool choiceAsKnob = false)
        : ctx (context), paramId (id), size (knobSize)
    {
        auto& state = ctx.state;
        auto* param = state.getParameter (paramId);
        jassert (param != nullptr);

        label.setText (labelText.isNotEmpty() ? labelText : param->getName (40), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, colours::dim);
        label.setFont (juce::FontOptions (size == KnobSize::Small ? 11.0f : 12.0f));
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (param);
        if (choiceParam != nullptr && ! choiceAsKnob)
        {
            kind = Kind::Choice;
            combo.addItemList (choiceParam->choices, 1);
            addAndMakeVisible (combo);
            comboAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, combo);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        {
            kind = Kind::Toggle;
            toggle.setButtonText (label.getText());
            label.setVisible (false);
            addAndMakeVisible (toggle);
            buttonAttachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, toggle);
        }
        else
        {
            kind = Kind::Knob;
            target = ModContext::targetFor (paramId);
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            if (size == KnobSize::Hero)
            {
                slider.getProperties().set (heroKnobProperty, true);
                slider.setMouseDragSensitivity (400);
                label.setVisible (false);
            }
            addAndMakeVisible (slider);
            sliderAttachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
            slider.addMouseListener (this, false);
            if (choiceParam != nullptr)
                numChoices = choiceParam->choices.size();
            if (target != ModTarget::None)
                startTimerHz (30);
        }
    }

    // For a choice shown as a knob: a marker dot for every position.
    void showStepMarkers() { slider.getProperties().set (stepsProperty, numChoices); }

    ~ParamControl() override { slider.removeMouseListener (this); }

    int preferredWidth() const noexcept
    {
        if (kind == Kind::Choice) return 112;
        if (kind == Kind::Toggle) return 104;
        return size == KnobSize::Hero ? 190 : size == KnobSize::Small ? 62 : 74;
    }
    int preferredHeight() const noexcept
    {
        if (kind != Kind::Knob) return 52;
        return size == KnobSize::Hero ? 200 : size == KnobSize::Small ? 80 : 96;
    }

    juce::Slider& getSlider() noexcept { return slider; }

    void resized() override
    {
        auto r = getLocalBounds();
        switch (kind)
        {
            case Kind::Knob:
                if (size != KnobSize::Hero)
                    label.setBounds (r.removeFromTop (size == KnobSize::Small ? 14 : 16));
                slider.setBounds (r);
                break;
            case Kind::Choice:
                label.setBounds (r.removeFromTop (16));
                combo.setBounds (r.removeFromTop (26).reduced (2, 0));
                break;
            case Kind::Toggle:
                toggle.setBounds (r.withSizeKeepingCentre (r.getWidth(), 26).withY (r.getY() + 16));
                break;
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (kind != Kind::Knob) return;

        if (dragSource != ModSource::None)
        {
            g.setColour (colours::source (dragSource).withAlpha (0.8f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f);
        }
        if (connections.empty()) return;

        const auto area = getLookAndFeel().getSliderLayout (slider).sliderBounds + slider.getPosition();
        const auto knob = FluxLookAndFeel::knobBounds (area);
        const auto centre = knob.getCentre();
        const auto rp = slider.getRotaryParameters();
        const float base = (float) slider.valueToProportionOfLength (slider.getValue());
        auto angleAt = [&] (float p) { return rp.startAngleRadians + juce::jlimit (0.0f, 1.0f, p) * (rp.endAngleRadians - rp.startAngleRadians); };
        const float r0 = knob.getWidth() * 0.5f + 2.5f;
        const auto highlight = ctx.getHighlighted();

        for (size_t i = 0; i < connections.size() && i < 3; ++i)
        {
            const auto& c = connections[i];
            const float r = r0 + 2.6f * (float) i;
            // Bipolar sources swing both ways around the knob value, unipolar ones only towards depth.
            const float a0 = angleAt (c.bipolar ? base - c.depth : base), a1 = angleAt (base + c.depth);
            if (std::abs (a1 - a0) < 0.01f) continue;
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
            const bool dimmed = highlight != ModSource::None && highlight != c.source;
            g.setColour (colours::source (c.source).withAlpha (dimmed ? 0.25f : 0.95f));
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Live position: where the modulation has the knob right now.
        const float live = angleAt (base + liveOffset);
        const float rl = r0 + 1.0f;
        g.setColour (colours::text);
        g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre ({ centre.x + rl * std::sin (live), centre.y - rl * std::cos (live) }));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (kind == Kind::Knob) ctx.select (paramId);
    }

    // Drop target ------------------------------------------------------------------------------
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return target != ModTarget::None && ModContext::sourceFromDrag (d.description) != ModSource::None;
    }
    void itemDragEnter (const SourceDetails& d) override { dragSource = ModContext::sourceFromDrag (d.description); repaint(); }
    void itemDragExit (const SourceDetails&) override { dragSource = ModSource::None; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dragSource = ModSource::None;
        ctx.connect (ModContext::sourceFromDrag (d.description), target);
        ctx.select (paramId);
        refresh (true);
    }

private:
    void timerCallback() override { refresh (false); }

    void refresh (bool force)
    {
        auto list = ctx.connectionsFor (target);
        const float live = ctx.liveOffset (target);
        bool changed = force || list.size() != connections.size() || ctx.getVersion() != seenVersion
                    || std::abs (live - liveOffset) > 0.002f;
        for (size_t i = 0; ! changed && i < list.size(); ++i)
            changed = list[i].source != connections[i].source || list[i].bipolar != connections[i].bipolar
                   || std::abs (list[i].depth - connections[i].depth) > 1.0e-4f;
        if (! changed) return;
        connections = std::move (list);
        liveOffset = live;
        seenVersion = ctx.getVersion();
        repaint();
    }

    ModContext& ctx;
    juce::String paramId;
    KnobSize size;
    ModTarget target = ModTarget::None;
    enum class Kind { Knob, Choice, Toggle } kind = Kind::Knob;
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;
    std::unique_ptr<APVTS::SliderAttachment> sliderAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> buttonAttachment;

    std::vector<ModContext::Connection> connections;
    float liveOffset = 0.0f;
    uint32_t seenVersion = 0;
    ModSource dragSource = ModSource::None;
    int numChoices = 0;
};

// A choice parameter as a row of icon buttons (LFO shapes, flux shape, direction).
class ChoiceButtonRow : public juce::Component
{
public:
    // icon (index, area) returns the path to stroke for that choice.
    using IconFn = std::function<juce::Path (int, juce::Rectangle<float>)>;

    ChoiceButtonRow (ModContext& ctx, const juce::String& paramId, juce::String labelText, IconFn iconFn, juce::Colour onColour)
        : label (std::move (labelText)), icon (std::move (iconFn)), colour (onColour)
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (ctx.state.getParameter (paramId));
        jassert (param != nullptr);
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) {
            for (size_t i = 0; i < buttons.size(); ++i)
                buttons[i]->setToggleState ((int) i == juce::roundToInt (v), juce::dontSendNotification);
        });
        for (int i = 0; i < param->choices.size(); ++i)
        {
            auto& b = *buttons.emplace_back (std::make_unique<IconButton> (*this, i, param->choices[i]));
            b.onClick = [this, i] { attachment->setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }
        attachment->sendInitialUpdate();
    }

    static constexpr int buttonH = 24, labelH = 16, gapW = 4;
    int preferredHeight() const noexcept { return (label.isNotEmpty() ? labelH : 0) + buttonH; }

    // Fixed button width (0 = share the row). The row is then centred.
    void setButtonWidth (int w) { buttonW = w; resized(); }
    int rowWidth() const noexcept { return (int) buttons.size() * (buttonW + gapW) - gapW; }

    void paint (juce::Graphics& g) override
    {
        if (label.isEmpty()) return;
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label, getLocalBounds().removeFromTop (labelH).withX (rowX).withWidth (getWidth() - rowX), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (label.isNotEmpty()) r.removeFromTop (labelH);
        r = r.removeFromTop (buttonH);
        const int n = (int) buttons.size();
        if (buttonW > 0) r = r.withSizeKeepingCentre (rowWidth(), r.getHeight());
        rowX = r.getX();
        const int bw = buttonW > 0 ? buttonW : (r.getWidth() - gapW * (n - 1)) / n;
        for (auto& b : buttons) { b->setBounds (r.removeFromLeft (bw)); r.removeFromLeft (gapW); }
    }

private:
    class IconButton : public juce::Button
    {
    public:
        IconButton (ChoiceButtonRow& o, int i, const juce::String& name) : juce::Button (name), owner (o), index (i) { setTooltip (name); }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState();
            g.setColour (on ? owner.colour.withAlpha (0.22f) : (over ? colours::track.brighter (0.15f) : colours::track));
            g.fillRoundedRectangle (r, 4.0f);
            if (on)
            {
                g.setColour (owner.colour.withAlpha (0.8f));
                g.drawRoundedRectangle (r, 4.0f, 1.0f);
            }
            g.setColour (on ? owner.colour : colours::dim);
            g.strokePath (owner.icon (index, r.reduced (r.getWidth() * 0.22f, r.getHeight() * 0.28f)),
                          juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    private:
        ChoiceButtonRow& owner;
        int index;
    };

    juce::String label;
    IconFn icon;
    juce::Colour colour;
    int buttonW = 0, rowX = 0;
    std::vector<std::unique_ptr<IconButton>> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Icons ----------------------------------------------------------------------------------------
namespace icons
{
    // Points in a -1..1 box (y up) mapped into area.
    inline juce::Path polyline (juce::Rectangle<float> a, std::initializer_list<juce::Point<float>> pts)
    {
        juce::Path p;
        bool first = true;
        for (auto q : pts)
        {
            const juce::Point<float> xy (a.getCentreX() + q.x * a.getWidth() * 0.5f, a.getCentreY() - q.y * a.getHeight() * 0.5f);
            first ? p.startNewSubPath (xy) : p.lineTo (xy);
            first = false;
        }
        return p;
    }

    inline juce::Path sine (juce::Rectangle<float> a)
    {
        juce::Path p;
        for (int i = 0; i <= 24; ++i)
        {
            const float x = (float) i / 24.0f;
            const juce::Point<float> q (a.getX() + x * a.getWidth(), a.getCentreY() - std::sin (x * juce::MathConstants<float>::twoPi) * a.getHeight() * 0.5f);
            i == 0 ? p.startNewSubPath (q) : p.lineTo (q);
        }
        return p;
    }
    inline juce::Path triangle (juce::Rectangle<float> a) { return polyline (a, { { -1, 0 }, { -0.5f, 1 }, { 0.5f, -1 }, { 1, 0 } }); }
    inline juce::Path rampUp (juce::Rectangle<float> a)   { return polyline (a, { { -1, -1 }, { 1, 1 }, { 1, -1 } }); }
    inline juce::Path square (juce::Rectangle<float> a)   { return polyline (a, { { -1, 1 }, { 0, 1 }, { 0, -1 }, { 1, -1 } }); }
    inline juce::Path random (juce::Rectangle<float> a)
    {
        return polyline (a, { { -1, 0.2f }, { -0.6f, 0.2f }, { -0.6f, 0.9f }, { -0.2f, 0.9f }, { -0.2f, -0.6f }, { 0.2f, -0.6f },
                              { 0.2f, 0.4f }, { 0.6f, 0.4f }, { 0.6f, -1 }, { 1, -1 } });
    }

    // Pitch anchor: off (a flat line), scale (steps), follow (a harmonic comb).
    inline juce::Path anchorOff (juce::Rectangle<float> a) { return polyline (a, { { -0.8f, 0 }, { 0.8f, 0 } }); }
    inline juce::Path anchorScale (juce::Rectangle<float> a)
    {
        return polyline (a, { { -1, -1 }, { -0.5f, -1 }, { -0.5f, -0.33f }, { 0, -0.33f }, { 0, 0.33f }, { 0.5f, 0.33f }, { 0.5f, 1 }, { 1, 1 } });
    }
    inline juce::Path anchorFollow (juce::Rectangle<float> a)
    {
        juce::Path p;   // harmonics: bars getting shorter
        const float xs[] = { -0.9f, -0.3f, 0.3f, 0.9f }, hs[] = { 1.0f, 0.5f, 0.0f, -0.4f };
        for (int i = 0; i < 4; ++i) p.addPath (polyline (a, { { xs[i], -1 }, { xs[i], hs[i] } }));
        return p;
    }
    // Stereo life: linked (two overlapping circles), independent (two apart).
    inline juce::Path twoCircles (juce::Rectangle<float> a, float apart)
    {
        juce::Path p;
        const float d = a.getHeight(), cx = a.getCentreX(), cy = a.getCentreY(), off = apart * d * 0.5f;
        p.addEllipse (cx - off - d * 0.5f, cy - d * 0.5f, d, d);
        p.addEllipse (cx + off - d * 0.5f, cy - d * 0.5f, d, d);
        return p;
    }

    // Arrow along the x axis from x0 to x1 (in -1..1), head at x1.
    inline void arrow (juce::Path& p, juce::Rectangle<float> a, float x0, float x1)
    {
        const float dir = x1 > x0 ? 1.0f : -1.0f, h = 0.35f;
        p.addPath (polyline (a, { { x0, 0 }, { x1, 0 } }));
        p.addPath (polyline (a, { { x1 - dir * h, 0.6f }, { x1, 0 }, { x1 - dir * h, -0.6f } }));
    }
}

// A titled panel of controls that flow left to right and wrap.
class ParamSection : public juce::Component
{
public:
    explicit ParamSection (juce::String sectionTitle, int maxControlsPerRow = 99)
        : title (std::move (sectionTitle)), maxPerRow (maxControlsPerRow) {}

    ParamControl& add (ModContext& ctx, const juce::String& paramId, const juce::String& labelText = {}, bool choiceAsKnob = false)
    {
        auto& c = *controls.emplace_back (std::make_unique<ParamControl> (ctx, paramId, labelText, KnobSize::Normal, choiceAsKnob));
        addAndMakeVisible (c);
        return c;
    }

    // Width needed to fit every control in one row.
    int singleRowWidth() const
    {
        int w = 2 * pad - gap;
        for (auto& c : controls) w += c->preferredWidth() + gap;
        return w;
    }

    int heightFor (int width) const
    {
        int rows = 1, x = 0, inRow = 0;
        const int avail = width - 2 * pad;
        for (auto& c : controls)
        {
            if (x > 0 && (x + c->preferredWidth() > avail || inRow == maxPerRow)) { ++rows; x = 0; inRow = 0; }
            x += c->preferredWidth() + gap;
            ++inRow;
        }
        return titleH + rows * rowH + pad;
    }

    void paint (juce::Graphics& g) override { paintPanel (g, getLocalBounds(), title); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad, 0);
        r.removeFromTop (titleH);

        // Rows are filled greedily, then each row is centred.
        std::vector<std::vector<ParamControl*>> rows (1);
        int x = 0;
        for (auto& c : controls)
        {
            if (x > 0 && (x + c->preferredWidth() > r.getWidth() || (int) rows.back().size() == maxPerRow)) { rows.emplace_back(); x = 0; }
            rows.back().push_back (c.get());
            x += c->preferredWidth() + gap;
        }
        r.removeFromBottom (pad);
        const int h = juce::jmax (rowH, r.getHeight() / (int) rows.size());   // spread rows over the panel
        int y = r.getY();
        for (auto& row : rows)
        {
            int w = -gap;
            for (auto* c : row) w += c->preferredWidth() + gap;
            int cx = r.getX() + (r.getWidth() - w) / 2;
            for (auto* c : row)
            {
                c->setBounds (cx, y + (h - c->preferredHeight()) / 2, c->preferredWidth(), c->preferredHeight());
                cx += c->preferredWidth() + gap;
            }
            y += h;
        }
    }

private:
    static constexpr int pad = 10, gap = 6, titleH = 28, rowH = 96;
    juce::String title;
    int maxPerRow;
    std::vector<std::unique_ptr<ParamControl>> controls;
};

} // namespace flux::ui
