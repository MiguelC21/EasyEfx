#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/Effects.h"
#include "CustomKnob.h"
#include <functional>

// Modal panel with the advanced parameters of one effect.
// Choice parameters become segmented buttons, the rest become knobs.
class SettingsOverlay : public juce::Component
{
public:
    SettingsOverlay(juce::AudioProcessorValueTreeState& apvts, EasyKnobLookAndFeel& knobLA);
    ~SettingsOverlay() override;

    void showForEffect(int effectIndex);
    void hide();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void()> onClose;

private:
    struct ChoiceRow : public juce::Component
    {
        juce::Label label;
        juce::OwnedArray<juce::TextButton> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::Colour accent;

        void setup(juce::RangedAudioParameter& param, const juce::String& title,
                   const juce::StringArray& options, juce::Colour accentColour);
        void setCurrent(int index);
        void resized() override;
    };

    struct KnobCell : public juce::Component
    {
        juce::Label title, value;
        HostMenuSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        void setup(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                   const juce::String& name, juce::Colour accent, EasyKnobLookAndFeel& la);
        ~KnobCell() override { slider.setLookAndFeel(nullptr); }
        void resized() override;
    };

    juce::AudioProcessorValueTreeState& apvts;
    EasyKnobLookAndFeel& knobLA;
    int currentEffectIndex = -1;
    juce::Colour accent { 0xff2ee6ff };

    juce::Rectangle<int> dialogBounds;
    juce::TextButton closeButton;
    juce::Label titleLabel, categoryLabel;

    juce::OwnedArray<ChoiceRow> choiceRows;
    juce::OwnedArray<KnobCell> knobCells;

    void clearContent();
    void layoutContent();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};
