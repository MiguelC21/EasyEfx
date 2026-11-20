#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "CustomKnob.h"

// Global section: "Output Match" switch + a meter showing how much gain is being applied
// to keep the output loudness equal to the input loudness.
class GlobalPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit GlobalPanel(EasyEfxAudioProcessor& p) : processor(p)
    {
        toggle.setButtonText("OUTPUT MATCH");
        toggle.setClickingTogglesState(true);
        toggle.setTooltip("Keeps the output volume equal to the input volume, whatever the effects do to the level");
        toggle.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222736));
        toggle.setColour(juce::TextButton::buttonOnColourId, accent);
        toggle.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc5cee0));
        toggle.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff08141a));
        addAndMakeVisible(toggle);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.getAPVTS(), EasyEfxAudioProcessor::getMatchParamID(), toggle);

        startTimerHz(15);
    }

    ~GlobalPanel() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        const bool on = toggle.getToggleState();
        auto bounds = getLocalBounds().toFloat().reduced(3.0f);

        g.setColour(on ? EasyUI::Colours::card : EasyUI::Colours::cardOff);
        g.fillRoundedRectangle(bounds, 10.0f);
        g.setColour(on ? accent.withAlpha(0.7f) : juce::Colour(0xff252a38));
        g.drawRoundedRectangle(bounds, 10.0f, on ? 1.6f : 1.0f);

        auto area = getLocalBounds().reduced(14, 10);

        auto header = area.removeFromTop(22);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.setColour(on ? juce::Colours::white : juce::Colour(0xffb6bfd2));
        g.drawFittedText("GLOBAL", header.removeFromTop(15), juce::Justification::centredLeft, 1);
        g.setFont(juce::FontOptions(9.0f, juce::Font::plain));
        g.setColour(on ? accent.withAlpha(0.85f) : EasyUI::Colours::textDim);
        g.drawFittedText("LEVEL", header, juce::Justification::centredLeft, 1);

        // Gain meter: centre = 0 dB, range +/-12 dB
        auto meter = meterRect;
        g.setColour(EasyUI::Colours::track);
        g.fillRoundedRectangle(meter.toFloat(), 4.0f);

        const float cx = static_cast<float>(meter.getCentreX());
        g.setColour(juce::Colour(0xff3a4156));
        g.fillRect(cx - 1.0f, static_cast<float>(meter.getY()) - 3.0f, 2.0f, static_cast<float>(meter.getHeight()) + 6.0f);

        if (on)
        {
            const float frac = juce::jlimit(-1.0f, 1.0f, gainDb / 12.0f);
            const float half = static_cast<float>(meter.getWidth()) * 0.5f;
            const float len = std::abs(frac) * half;
            g.setColour(accent);
            if (frac >= 0.0f) g.fillRoundedRectangle(cx, static_cast<float>(meter.getY()), len, static_cast<float>(meter.getHeight()), 4.0f);
            else              g.fillRoundedRectangle(cx - len, static_cast<float>(meter.getY()), len, static_cast<float>(meter.getHeight()), 4.0f);
        }

        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.setColour(EasyUI::Colours::textDim);
        g.drawFittedText("-12", meterLabelL, juce::Justification::centredLeft, 1);
        g.drawFittedText("+12 dB", meterLabelR, juce::Justification::centredRight, 1);

        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        g.setColour(on ? accent : juce::Colour(0xff4b5367));
        const juce::String text = on ? (gainDb >= 0.0f ? "+" : "") + juce::String(gainDb, 1) + " dB" : juce::String("OFF");
        g.drawFittedText(text, valueRect, juce::Justification::centred, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14, 10);
        area.removeFromTop(30);

        toggle.setBounds(area.removeFromTop(34).reduced(0, 0));
        area.removeFromTop(14);

        meterRect = area.removeFromTop(10).reduced(4, 0);
        auto labels = area.removeFromTop(14);
        meterLabelL = labels.removeFromLeft(labels.getWidth() / 2);
        meterLabelR = labels;
        valueRect = area.removeFromTop(26);
    }

private:
    void timerCallback() override
    {
        const float db = processor.getMatchGainDb();
        if (std::abs(db - gainDb) > 0.05f || (toggle.getToggleState() != lastOn))
        {
            gainDb = db;
            lastOn = toggle.getToggleState();
            repaint();
        }
    }

    EasyEfxAudioProcessor& processor;
    juce::Colour accent { 0xff2ee6ff };
    juce::TextButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    juce::Rectangle<int> meterRect, meterLabelL, meterLabelR, valueRect;
    float gainDb = 0.0f;
    bool lastOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalPanel)
};
