#include "SettingsOverlay.h"
#include "../PluginProcessor.h"

// -----------------------------------------------------------------------------------------
// Segmented choice selector
// -----------------------------------------------------------------------------------------
void SettingsOverlay::ChoiceRow::setup(juce::RangedAudioParameter& param, const juce::String& title,
                                       const juce::StringArray& options, juce::Colour accentColour)
{
    accent = accentColour;

    label.setText(title.toUpperCase(), juce::dontSendNotification);
    label.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, EasyUI::Colours::textMid);
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);

    for (int i = 0; i < options.size(); ++i)
    {
        auto* btn = new juce::TextButton(options[i]);
        btn->setClickingTogglesState(false);
        btn->onClick = [this, i]
        {
            if (attachment != nullptr)
                attachment->setValueAsCompleteGesture(static_cast<float>(i));
        };
        buttons.add(btn);
        addAndMakeVisible(btn);
    }

    attachment = std::make_unique<juce::ParameterAttachment>(param, [this](float v) { setCurrent(juce::roundToInt(v)); });
    attachment->sendInitialUpdate();
}

void SettingsOverlay::ChoiceRow::setCurrent(int index)
{
    for (int b = 0; b < buttons.size(); ++b)
    {
        const bool active = (b == index);
        buttons[b]->setColour(juce::TextButton::buttonColourId, active ? accent : juce::Colour(0xff222736));
        buttons[b]->setColour(juce::TextButton::textColourOffId, active ? juce::Colour(0xff0a0c10) : juce::Colour(0xffc5cee0));
        buttons[b]->repaint();
    }
}

void SettingsOverlay::ChoiceRow::resized()
{
    auto area = getLocalBounds();
    label.setBounds(area.removeFromLeft(90));

    const int n = buttons.size();
    if (n > 0)
    {
        const int gap = 6;
        const int btnW = (area.getWidth() - (n - 1) * gap) / n;
        for (int i = 0; i < n; ++i)
        {
            buttons[i]->setBounds(area.removeFromLeft(btnW).reduced(0, 3));
            area.removeFromLeft(gap);
        }
    }
}

// -----------------------------------------------------------------------------------------
// Knob cell
// -----------------------------------------------------------------------------------------
void SettingsOverlay::KnobCell::setup(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                                      const juce::String& name, juce::Colour accent, EasyKnobLookAndFeel& la)
{
    title.setText(name.toUpperCase(), juce::dontSendNotification);
    title.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, EasyUI::Colours::textMid);
    title.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(title);

    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setLookAndFeel(&la);
    slider.setRotaryParameters(juce::degreesToRadians(220.0f), juce::degreesToRadians(500.0f), true);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.getProperties().set(EasyUI::mainKnobProp, true);
    addAndMakeVisible(slider);

    value.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    value.setColour(juce::Label::textColourId, accent);
    value.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(value);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramID, slider);

    if (auto* param = apvts.getParameter(paramID))
    {
        slider.hostParameter = param;
        slider.setDoubleClickReturnValue(true, static_cast<double>(param->convertFrom0to1(param->getDefaultValue())));
    }

    slider.onValueChange = [this] { value.setText(slider.getTextFromValue(slider.getValue()), juce::dontSendNotification); };
    slider.onValueChange();
}

void SettingsOverlay::KnobCell::resized()
{
    auto area = getLocalBounds().reduced(4, 0);
    title.setBounds(area.removeFromTop(18));
    value.setBounds(area.removeFromBottom(22));
    const int d = juce::jmin(area.getWidth(), area.getHeight());
    slider.setBounds(area.withSizeKeepingCentre(d, d));
}

// -----------------------------------------------------------------------------------------
// Overlay
// -----------------------------------------------------------------------------------------
SettingsOverlay::SettingsOverlay(juce::AudioProcessorValueTreeState& vts, EasyKnobLookAndFeel& la)
    : apvts(vts), knobLA(la)
{
    setAlwaysOnTop(true);
    setWantsKeyboardFocus(true);

    closeButton.setButtonText("X");
    closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0x00000000));
    closeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc5cee0));
    closeButton.onClick = [this] { hide(); };
    addAndMakeVisible(closeButton);

    titleLabel.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    categoryLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    categoryLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(categoryLabel);
}

SettingsOverlay::~SettingsOverlay()
{
    clearContent();
}

void SettingsOverlay::clearContent()
{
    knobCells.clear();
    choiceRows.clear();
}

void SettingsOverlay::showForEffect(int effectIndex)
{
    clearContent();
    currentEffectIndex = effectIndex;

    const auto& info = EasyDSP::getEffectInfo(effectIndex);
    accent = juce::Colour(info.accent);

    titleLabel.setText(info.name + " SETTINGS", juce::dontSendNotification);
    categoryLabel.setText(info.category + "  -  " + info.tooltip, juce::dontSendNotification);
    categoryLabel.setColour(juce::Label::textColourId, accent);

    for (int p = 0; p < static_cast<int>(info.params.size()); ++p)
    {
        const auto& cfg = info.params[static_cast<size_t>(p)];
        const auto pid = EasyEfxAudioProcessor::getSubParamID(effectIndex, p);

        if (! cfg.options.isEmpty())
        {
            if (auto* param = apvts.getParameter(pid))
            {
                auto* row = new ChoiceRow();
                row->setup(*param, cfg.name, cfg.options, accent);
                choiceRows.add(row);
                addAndMakeVisible(row);
            }
        }
        else
        {
            auto* cell = new KnobCell();
            cell->setup(apvts, pid, cfg.name, accent, knobLA);
            knobCells.add(cell);
            addAndMakeVisible(cell);
        }
    }

    setVisible(true);
    toFront(true);
    grabKeyboardFocus();
    resized();
    repaint();
}

void SettingsOverlay::hide()
{
    clearContent();
    setVisible(false);
    if (onClose)
        onClose();
}

void SettingsOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (! dialogBounds.contains(e.getPosition()))
        hide();
}

bool SettingsOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        hide();
        return true;
    }
    return false;
}

void SettingsOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xd0080a10));

    auto card = dialogBounds.toFloat();

    g.setColour(accent.withAlpha(0.12f));
    g.fillRoundedRectangle(card.expanded(6.0f), 18.0f);

    juce::ColourGradient grad(juce::Colour(0xff1e222e), 0, card.getY(),
                              juce::Colour(0xff12141a), 0, card.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(card, 14.0f);

    g.setColour(accent.withAlpha(0.8f));
    g.drawRoundedRectangle(card, 14.0f, 1.6f);

    const float lineY = card.getY() + 70.0f;
    g.setColour(juce::Colour(0xff2b3244));
    g.drawLine(card.getX() + 24.0f, lineY, card.getRight() - 24.0f, lineY, 1.0f);
}

void SettingsOverlay::layoutContent()
{
    auto content = dialogBounds.reduced(28, 0);
    content.removeFromTop(70 + 14);

    for (auto* row : choiceRows)
    {
        row->setBounds(content.removeFromTop(36));
        content.removeFromTop(6);
    }

    const int n = knobCells.size();
    if (n > 0)
    {
        content.removeFromTop(6);
        const int rows = (n + 4) / 5;                 // at most 5 knobs per row
        const int perRow = (n + rows - 1) / rows;     // balanced rows (e.g. 6 -> 3 + 3)
        const int cellW = content.getWidth() / juce::jmax(perRow, 3);
        int idx = 0;
        for (int r = 0; r < rows; ++r)
        {
            auto rowArea = content.removeFromTop(150);
            content.removeFromTop(6);
            const int cnt = juce::jmin(perRow, n - idx);
            auto centred = rowArea.withSizeKeepingCentre(cellW * cnt, rowArea.getHeight());
            for (int k = 0; k < cnt; ++k)
                knobCells[idx++]->setBounds(centred.removeFromLeft(cellW));
        }
    }
}

void SettingsOverlay::resized()
{
    const int cardW = juce::jmin(580, getWidth() - 40);
    const int rows = choiceRows.size();
    const int knobRows = (knobCells.size() + 4) / 5;
    const int knobH = knobCells.isEmpty() ? 0 : 6 + knobRows * 156;
    const int cardH = 70 + 14 + rows * 42 + knobH + 24;

    dialogBounds = juce::Rectangle<int>((getWidth() - cardW) / 2, (getHeight() - cardH) / 2, cardW, cardH);

    auto header = dialogBounds.withHeight(70).reduced(28, 12);
    closeButton.setBounds(header.removeFromRight(30).withSizeKeepingCentre(28, 28));
    titleLabel.setBounds(header.removeFromTop(26));
    categoryLabel.setBounds(header);

    layoutContent();
}
