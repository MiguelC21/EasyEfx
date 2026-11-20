#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "GUI/CustomKnob.h"
#include "GUI/EffectTile.h"
#include "GUI/SettingsOverlay.h"
#include "GUI/GlobalPanel.h"
#include <vector>
#include <memory>

class EasyEfxAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::Timer
{
public:
    EasyEfxAudioProcessorEditor(EasyEfxAudioProcessor&);
    ~EasyEfxAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    void resetAllEffects();

    EasyEfxAudioProcessor& audioProcessor;
    EasyKnobLookAndFeel knobLA;
    juce::TooltipWindow tooltipWindow { this, 500 };

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton resetAllButton { "ALL OFF" };
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    std::vector<std::unique_ptr<EffectTile>> tiles;
    std::unique_ptr<GlobalPanel> globalPanel;
    SettingsOverlay settingsOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EasyEfxAudioProcessorEditor)
};
