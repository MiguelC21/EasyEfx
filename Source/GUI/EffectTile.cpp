#include "EffectTile.h"
#include "../PluginProcessor.h"

void EffectTile::SettingsButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat().reduced(4.0f);
    const bool lit = over || down;
    g.setColour(lit ? accent : EasyUI::Colours::textDim);

    // "Sliders" icon: three lines with knobs
    const float ys[] = { 0.18f, 0.5f, 0.82f };
    const float ks[] = { 0.65f, 0.3f, 0.55f };
    for (int i = 0; i < 3; ++i)
    {
        const float yy = b.getY() + b.getHeight() * ys[i];
        g.drawLine(b.getX(), yy, b.getRight(), yy, 1.4f);
        g.fillEllipse(b.getX() + b.getWidth() * ks[i] - 2.6f, yy - 2.6f, 5.2f, 5.2f);
    }
}

void EffectTile::MatchButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    const bool on = getToggleState();
    auto b = getLocalBounds().toFloat().reduced(2.0f);

    if (on)
    {
        g.setColour(accent.withAlpha(0.22f));
        g.fillRoundedRectangle(b, 5.0f);
        g.setColour(accent);
        g.drawRoundedRectangle(b, 5.0f, 1.3f);
    }
    else if (over || down)
    {
        g.setColour(EasyUI::Colours::textDim.withAlpha(0.25f));
        g.fillRoundedRectangle(b, 5.0f);
    }

    // "=" : in and out are equal
    g.setColour(on ? accent : (over ? accent.withAlpha(0.8f) : EasyUI::Colours::textDim));
    const float cx = b.getCentreX(), cy = b.getCentreY(), hw = b.getWidth() * 0.26f;
    g.drawLine(cx - hw, cy - 2.6f, cx + hw, cy - 2.6f, 1.8f);
    g.drawLine(cx - hw, cy + 2.6f, cx + hw, cy + 2.6f, 1.8f);
}

EffectTile::EffectTile(int index, juce::AudioProcessorValueTreeState& vts, EasyKnobLookAndFeel& la)
    : effectIndex(index), apvts(vts), knobLA(la)
{
    const auto& info = EasyDSP::getEffectInfo(effectIndex);
    accent = juce::Colour(info.accent);

    auto setupKnob = [&](HostMenuSlider& s, bool isMain, const juce::String& paramID, float defaultValue)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setLookAndFeel(&knobLA);
        s.setRotaryParameters(juce::degreesToRadians(220.0f), juce::degreesToRadians(500.0f), true);
        s.setColour(juce::Slider::rotarySliderFillColourId, accent);
        s.getProperties().set(EasyUI::mainKnobProp, isMain);
        s.setMouseDragSensitivity(200);
        s.setTooltip(info.tooltip);
        addAndMakeVisible(s);
        (void) paramID;
        (void) defaultValue;
    };

    setupKnob(macro1Knob, true, EasyEfxAudioProcessor::getMacro1ParamID(effectIndex), 0.0f);
    setupKnob(macro2Knob, false, EasyEfxAudioProcessor::getMacro2ParamID(effectIndex), info.macro2Default);

    macro1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, EasyEfxAudioProcessor::getMacro1ParamID(effectIndex), macro1Knob);
    macro2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, EasyEfxAudioProcessor::getMacro2ParamID(effectIndex), macro2Knob);

    // Effects whose "off" is the centre of the knob (pitch) draw a bipolar arc
    if (std::abs(info.macro1Off - 0.5f) < 0.001f)
        macro1Knob.getProperties().set(EasyUI::bipolarKnobProp, true);

    // Right-click opens the host's menu (FL Studio: create automation clip, link to controller...)
    macro1Knob.hostParameter = apvts.getParameter(EasyEfxAudioProcessor::getMacro1ParamID(effectIndex));
    macro2Knob.hostParameter = apvts.getParameter(EasyEfxAudioProcessor::getMacro2ParamID(effectIndex));

    // Double-click: main knob goes back to OFF, secondary to its default
    macro1Knob.setDoubleClickReturnValue(true, static_cast<double>(info.macro1Off));
    macro2Knob.setDoubleClickReturnValue(true, static_cast<double>(info.macro2Default));

    macro1Knob.onValueChange = [this] { refreshText(); repaint(); };
    macro2Knob.onValueChange = [this] { refreshText(); };

    settingsButton.accent = accent;
    settingsButton.setTooltip("Settings");
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(effectIndex); };
    addAndMakeVisible(settingsButton);

    matchButton.accent = accent;
    matchButton.setTooltip("Level Match: keeps this effect's volume equal to the volume that goes in (within 12 dB)");
    matchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, EasyEfxAudioProcessor::getFxMatchParamID(effectIndex), matchButton);
    addAndMakeVisible(matchButton);

    refreshText();
    startTimerHz(8);
}

EffectTile::~EffectTile()
{
    stopTimer();
    macro1Knob.setLookAndFeel(nullptr);
    macro2Knob.setLookAndFeel(nullptr);
}

void EffectTile::refreshText()
{
    const auto& info = EasyDSP::getEffectInfo(effectIndex);

    float sub[EasyDSP::kMaxSubParams] = {};
    for (int p = 0; p < static_cast<int>(info.params.size()); ++p)
        if (auto* raw = apvts.getRawParameterValue(EasyEfxAudioProcessor::getSubParamID(effectIndex, p)))
            sub[p] = raw->load();

    const auto t1 = EasyDSP::formatMacroValue(effectIndex, 1, static_cast<float>(macro1Knob.getValue()), sub);
    const auto t2 = EasyDSP::formatMacroValue(effectIndex, 2, static_cast<float>(macro2Knob.getValue()), sub);

    if (t1 != text1 || t2 != text2)
    {
        text1 = t1;
        text2 = t2;
        repaint();
    }
}

void EffectTile::timerCallback()
{
    refreshText(); // picks up sync/range changes made in the settings panel
}

void EffectTile::paint(juce::Graphics& g)
{
    const auto& info = EasyDSP::getEffectInfo(effectIndex);
    // Distance from the "off" position (min for most effects, max for the low-pass)
    const float amount = std::abs(static_cast<float>(macro1Knob.getValue()) - info.macro1Off);
    const bool active = amount > 0.002f;

    auto bounds = getLocalBounds().toFloat().reduced(3.0f);

    // Card
    g.setColour(active ? EasyUI::Colours::card : EasyUI::Colours::cardOff);
    g.fillRoundedRectangle(bounds, 10.0f);

    // Accent glow that follows the main knob, so automation is visible at a glance
    if (active)
    {
        g.setColour(accent.withAlpha(0.06f + 0.10f * amount));
        g.fillRoundedRectangle(bounds, 10.0f);
        g.setColour(accent.withAlpha(0.35f + 0.55f * amount));
        g.drawRoundedRectangle(bounds, 10.0f, 1.6f);
    }
    else
    {
        g.setColour(juce::Colour(0xff252a38));
        g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
    }

    // Header: status dot, name, category
    auto header = getLocalBounds().reduced(14, 10).removeFromTop(22);
    auto dot = header.removeFromLeft(14).withSizeKeepingCentre(7, 7).toFloat();
    g.setColour(active ? accent : juce::Colour(0xff343a4b));
    g.fillEllipse(dot);
    header.removeFromLeft(4);
    header.removeFromRight(52);   // room for the "=" and settings buttons

    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.setColour(active ? juce::Colours::white : juce::Colour(0xffb6bfd2));
    g.drawFittedText(info.name, header.removeFromTop(15), juce::Justification::centredLeft, 1);
    g.setFont(juce::FontOptions(9.0f, juce::Font::plain));
    g.setColour(active ? accent.withAlpha(0.85f) : EasyUI::Colours::textDim);
    g.drawFittedText(info.category, header, juce::Justification::centredLeft, 1);

    // Labels + values
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.setColour(EasyUI::Colours::textDim);
    g.drawFittedText(info.macro1Label.toUpperCase(), label1Rect, juce::Justification::centred, 1);
    g.drawFittedText(info.macro2Label.toUpperCase(), label2Rect, juce::Justification::centred, 1);

    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.setColour(active ? accent : juce::Colour(0xff4b5367));
    g.drawFittedText(text1, value1Rect, juce::Justification::centred, 1);

    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.setColour(EasyUI::Colours::textMid);
    g.drawFittedText(text2, value2Rect, juce::Justification::centred, 1);
}

void EffectTile::resized()
{
    auto area = getLocalBounds().reduced(12, 8);

    settingsButton.setBounds(area.getRight() - 24, area.getY() + 4, 22, 22);
    matchButton.setBounds(area.getRight() - 48, area.getY() + 4, 22, 22);

    area.removeFromTop(30); // header

    auto bigCol = area.removeFromLeft(static_cast<int>(area.getWidth() * 0.60f));
    auto smallCol = area;

    // Main column
    auto bigText = bigCol.removeFromBottom(36);
    label1Rect = bigText.removeFromTop(14);
    value1Rect = bigText;
    const int d1 = juce::jmin(bigCol.getWidth(), bigCol.getHeight());
    macro1Knob.setBounds(bigCol.withSizeKeepingCentre(d1, d1));

    // Secondary column
    auto smallText = smallCol.removeFromBottom(36);
    label2Rect = smallText.removeFromTop(14);
    value2Rect = smallText;
    const int d2 = juce::jmin(smallCol.getWidth() - 6, static_cast<int>(smallCol.getHeight() * 0.8f));
    macro2Knob.setBounds(smallCol.withSizeKeepingCentre(d2, d2));
}
