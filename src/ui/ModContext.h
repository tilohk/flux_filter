#pragma once

#include "PluginProcessor.h"
#include "params/Parameters.h"

namespace flux::ui
{

// Shared editor state for drag-and-drop modulation: which knob is selected, which source is
// highlighted, and reading / writing the connection slots (plain automatable parameters).
class ModContext
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Connection { int slot; ModSource source; float depth; bool bipolar; };

    explicit ModContext (FluxFilterAudioProcessor& p) : processor (p), state (p.apvts)
    {
        for (int i = 0; i < ModSettings::numSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            s.source = state.getParameter (params::id::slot (i, "Source"));
            s.target = state.getParameter (params::id::slot (i, "Target"));
            s.depth  = state.getParameter (params::id::slot (i, "Depth"));
            s.sourceValue = state.getRawParameterValue (params::id::slot (i, "Source"));
            s.targetValue = state.getRawParameterValue (params::id::slot (i, "Target"));
            s.depthValue  = state.getRawParameterValue (params::id::slot (i, "Depth"));
        }
        for (int i = 0; i < ModSettings::numLfos; ++i)
            unipolar[(size_t) i] = state.getRawParameterValue (params::id::lfo (i, "Unipolar"));
    }

    // Bipolar sources move the knob both ways, unipolar ones only towards the depth.
    bool isBipolar (ModSource s) const noexcept
    {
        return ! isLfo (s) || unipolar[(size_t) s - (size_t) ModSource::Lfo1]->load() < 0.5f;
    }

    FluxFilterAudioProcessor& processor;
    APVTS& state;

    static ModTarget targetFor (const juce::String& paramId)
    {
        for (int i = 0; i < (int) spec::FP::Count; ++i)
            if (paramId == spec::floats()[(size_t) i].id)
                return targetForParam ((spec::FP) i);
        return ModTarget::None;
    }

    ModSource slotSource (int i) const noexcept { return (ModSource) (int) slots[(size_t) i].sourceValue->load(); }
    ModTarget slotTarget (int i) const noexcept { return (ModTarget) (int) slots[(size_t) i].targetValue->load(); }
    float slotDepth (int i) const noexcept { return slots[(size_t) i].depthValue->load(); }
    juce::String depthParamId (int i) const { return params::id::slot (i, "Depth"); }

    std::vector<Connection> connectionsFor (ModTarget t) const
    {
        std::vector<Connection> list;
        if (t == ModTarget::None) return list;
        for (int i = 0; i < ModSettings::numSlots; ++i)
            if (slotTarget (i) == t && slotSource (i) != ModSource::None)
                list.push_back ({ i, slotSource (i), slotDepth (i), isBipolar (slotSource (i)) });
        return list;
    }

    bool sourceIsConnectedTo (ModSource s, ModTarget t) const
    {
        for (int i = 0; i < ModSettings::numSlots; ++i)
            if (slotSource (i) == s && slotTarget (i) == t) return true;
        return false;
    }

    // Live offset (normalised) a knob gets from all its connections. Flux delay is not shown here.
    float liveOffset (ModTarget t) const
    {
        float sum = 0.0f;
        for (const auto& c : connectionsFor (t))
            sum += c.depth * processor.getModSourceValue (c.source);
        return sum;
    }

    // New connection (or the existing one for this pair). Returns false if all slots are in use.
    bool connect (ModSource s, ModTarget t, float depth = 0.25f)
    {
        if (s == ModSource::None || t == ModTarget::None) return false;
        int free = -1;
        for (int i = 0; i < ModSettings::numSlots; ++i)
        {
            if (slotSource (i) == s && slotTarget (i) == t) return true;
            if (free < 0 && (slotSource (i) == ModSource::None || slotTarget (i) == ModTarget::None)) free = i;
        }
        if (free < 0) return false;
        set (slots[(size_t) free].source, (float) (int) s);
        set (slots[(size_t) free].target, (float) (int) t);
        set (slots[(size_t) free].depth, depth);
        bump();
        return true;
    }

    void disconnect (int slot)
    {
        set (slots[(size_t) slot].depth, 0.0f);
        set (slots[(size_t) slot].source, 0.0f);
        set (slots[(size_t) slot].target, 0.0f);
        bump();
    }

    // Selection and highlight. Components poll `version` to notice changes.
    void select (const juce::String& paramId) { if (selected != paramId) { selected = paramId; bump(); } }
    const juce::String& getSelected() const noexcept { return selected; }
    void toggleHighlight (ModSource s) { highlighted = highlighted == s ? ModSource::None : s; bump(); }
    ModSource getHighlighted() const noexcept { return highlighted; }
    uint32_t getVersion() const noexcept { return version; }

    static juce::String dragDescription (ModSource s) { return "modsrc:" + juce::String ((int) s); }
    static ModSource sourceFromDrag (const juce::var& description)
    {
        const auto d = description.toString();
        return d.startsWith ("modsrc:") ? (ModSource) d.fromFirstOccurrenceOf (":", false, false).getIntValue() : ModSource::None;
    }

private:
    static void set (juce::RangedAudioParameter* p, float plainValue)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
    void bump() noexcept { ++version; }

    struct Slot
    {
        juce::RangedAudioParameter* source = nullptr; juce::RangedAudioParameter* target = nullptr; juce::RangedAudioParameter* depth = nullptr;
        std::atomic<float>* sourceValue = nullptr; std::atomic<float>* targetValue = nullptr; std::atomic<float>* depthValue = nullptr;
    };
    std::array<Slot, ModSettings::numSlots> slots {};
    std::array<std::atomic<float>*, ModSettings::numLfos> unipolar {};
    juce::String selected;
    ModSource highlighted = ModSource::None;
    uint32_t version = 1;
};

} // namespace flux::ui
