#pragma once

#include "FluxLookAndFeel.h"
#include "PluginProcessor.h"

namespace flux::ui
{

// Header preset bar:  <  [ preset list ]  >  SAVE
// Factory presets first, then the user's (saved as files, see PresetManager::folder()).
class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (FluxFilterAudioProcessor& p) : presets (p.presets)
    {
        for (auto* b : { &prev, &next, &save })
            addAndMakeVisible (b);
        prev.setButtonText ("<");
        next.setButtonText (">");
        save.setButtonText ("SAVE");
        prev.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        save.setTooltip ("Save the current settings as a user preset");
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        save.onClick = [this] { askForName(); };

        list.setTextWhenNothingSelected ("(edited)");
        list.onChange = [this] {
            if (const int id = list.getSelectedId(); id > 0 && id - 1 != presets.getCurrent())
                presets.load (id - 1);
        };
        addAndMakeVisible (list);
        rebuild();
        startTimerHz (4);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 3);
        prev.setBounds (r.removeFromLeft (24));
        r.removeFromLeft (4);
        save.setBounds (r.removeFromRight (52));
        r.removeFromRight (6);
        next.setBounds (r.removeFromRight (24));
        r.removeFromRight (4);
        list.setBounds (r);
    }

private:
    void step (int delta)
    {
        const int n = presets.numPresets();
        if (n == 0) return;
        const int from = presets.getCurrent() < 0 ? 0 : presets.getCurrent();
        presets.load ((from + delta + n) % n);
        showCurrent();
    }

    void askForName()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name of the user preset:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", presets.getCurrent() >= presets.numFactory() ? presets.getCurrentName() : juce::String());
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<PresetBar> (this), w] (int result) {
                if (result == 1 && safe != nullptr)
                {
                    safe->presets.save (w->getTextEditorContents ("name"));
                    safe->rebuild();
                }
            }), true);
    }

    void rebuild()
    {
        list.clear (juce::dontSendNotification);
        list.addSectionHeading ("Factory");
        for (int i = 0; i < presets.numFactory(); ++i)
            list.addItem (presets.nameOf (i), i + 1);
        if (presets.userPresets().size() > 0)
        {
            list.addSectionHeading ("User");
            for (int i = presets.numFactory(); i < presets.numPresets(); ++i)
                list.addItem (presets.nameOf (i), i + 1);
        }
        shown = -2;
        showCurrent();
    }

    void showCurrent()
    {
        const int cur = presets.getCurrent();
        if (cur == shown) return;
        shown = cur;
        if (cur >= 0) list.setSelectedId (cur + 1, juce::dontSendNotification);
        else          list.setText (presets.getCurrentName(), juce::dontSendNotification);
    }

    void timerCallback() override { showCurrent(); }   // host program changes, project reloads

    PresetManager& presets;
    juce::ComboBox list;
    juce::TextButton prev, next, save;
    int shown = -2;
};

} // namespace flux::ui
