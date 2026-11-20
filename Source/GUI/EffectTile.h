#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/Effects.h"
#include "CustomKnob.h"
#include <functional>

// One effect card: big main knob (0 = off), small secondary knob, settings button.
class EffectTile : public juce::Component,
                   private juce::Timer
{
public:
    EffectTile(int effectIndex, juce::AudioProcessorValueTreeState& apvts, EasyKnobLookAndFeel& knobLA);
    ~EffectTile() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(int)> onOpenSettings;

private:
    struct SettingsButton : public juce::Button
    {
        SettingsButton() : juce::Button("settings") {}
        juce::Colour accent;
        void paintButton(juce::Graphics& g, bool over, bool down) override;
    };

    // "=" toggle: level match for this effect
    struct MatchButton : public juce::Button
    {
        MatchButton() : juce::Button("match") { setClickingTogglesState(true); }
        juce::Colour accent;
        void paintButton(juce::Graphics& g, bool over, bool down) override;
    };

    void timerCallback() override;
    void refreshText();

    int effectIndex;
    juce::AudioProcessorValueTreeState& apvts;
    EasyKnobLookAndFeel& knobLA;
    juce::Colour accent;

    HostMenuSlider macro1Knob, macro2Knob;
    SettingsButton settingsButton;
    MatchButton matchButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAttachment;

    juce::String text1, text2;
    juce::Rectangle<int> label1Rect, value1Rect, label2Rect, value2Rect;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> macro1Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> macro2Attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectTile)
};
