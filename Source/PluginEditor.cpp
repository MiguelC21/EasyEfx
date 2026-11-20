#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kCols = 6;
    constexpr int kRows = 3;
}

EasyEfxAudioProcessorEditor::EasyEfxAudioProcessorEditor(EasyEfxAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), settingsOverlay(p.getAPVTS(), knobLA)
{
    setLookAndFeel(&knobLA);

    titleLabel.setText("EASYEFX", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff2ee6ff));
    addAndMakeVisible(titleLabel);

    statusLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    statusLabel.setColour(juce::Label::textColourId, EasyUI::Colours::textDim);
    statusLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);

    resetAllButton.setTooltip("Turn every main knob to minimum (all effects off)");
    resetAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222736));
    resetAllButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc5cee0));
    resetAllButton.onClick = [this] { resetAllEffects(); };
    addAndMakeVisible(resetAllButton);

    // Global bypass: the plugin lets the sound through untouched (cross-faded, no click)
    bypassButton.setClickingTogglesState(true);
    bypassButton.setTooltip("Bypass: lets the sound through untouched (all effects off, tails cleared)");
    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222736));
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffb020));
    bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc5cee0));
    bypassButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1a1203));
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), EasyEfxAudioProcessor::getBypassParamID(), bypassButton);
    addAndMakeVisible(bypassButton);

    for (int i = 0; i < EasyDSP::kNumEffects; ++i)
    {
        auto tile = std::make_unique<EffectTile>(i, audioProcessor.getAPVTS(), knobLA);
        tile->onOpenSettings = [this](int fxIndex) { settingsOverlay.showForEffect(fxIndex); };
        addAndMakeVisible(*tile);
        tiles.push_back(std::move(tile));
    }

    globalPanel = std::make_unique<GlobalPanel>(audioProcessor);
    addAndMakeVisible(*globalPanel);

    addChildComponent(settingsOverlay);
    settingsOverlay.onClose = [this] { repaint(); };

    // While bypassed the effect cards are dimmed so it is obvious nothing is being processed
    auto updateBypassLook = [this]
    {
        const float alpha = bypassButton.getToggleState() ? 0.35f : 1.0f;
        for (auto& tile : tiles) tile->setAlpha(alpha);
        if (globalPanel != nullptr) globalPanel->setAlpha(alpha);
    };
    bypassButton.onStateChange = updateBypassLook;
    updateBypassLook();

    startTimerHz(4);
    timerCallback();

    // Size last, so the layout runs with all children already created
    setResizable(true, true);
    setResizeLimits(1000, 520, 2000, 1000);
    setSize(1320, 640);
}

EasyEfxAudioProcessorEditor::~EasyEfxAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void EasyEfxAudioProcessorEditor::resetAllEffects()
{
    auto& apvts = audioProcessor.getAPVTS();
    for (int i = 0; i < EasyDSP::kNumEffects; ++i)
    {
        if (auto* p = apvts.getParameter(EasyEfxAudioProcessor::getMacro1ParamID(i)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(EasyDSP::getEffectInfo(i).macro1Off);
            p->endChangeGesture();
        }
    }
}

void EasyEfxAudioProcessorEditor::timerCallback()
{
    statusLabel.setText("TEMPO  " + juce::String(juce::roundToInt(audioProcessor.getCurrentBpm())) + " BPM",
                        juce::dontSendNotification);
}

void EasyEfxAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bg(EasyUI::Colours::bgTop, 0.0f, 0.0f,
                            EasyUI::Colours::bgBottom, 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(bg);
    g.fillAll();

    g.setColour(juce::Colour(0xff222736));
    g.drawLine(20.0f, 56.0f, static_cast<float>(getWidth() - 20), 56.0f, 1.0f);
}

void EasyEfxAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop(56).reduced(22, 12);
    titleLabel.setBounds(header.removeFromLeft(130));
    resetAllButton.setBounds(header.removeFromRight(80));
    header.removeFromRight(8);
    bypassButton.setBounds(header.removeFromRight(86));
    header.removeFromRight(12);
    statusLabel.setBounds(header);

    auto grid = area.reduced(14, 10);
    const int cellW = grid.getWidth() / kCols;
    const int cellH = grid.getHeight() / kRows;

    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c)
        {
            const int idx = r * kCols + c;
            if (idx < static_cast<int>(tiles.size()))
                tiles[static_cast<size_t>(idx)]->setBounds(grid.getX() + c * cellW, grid.getY() + r * cellH, cellW, cellH);
        }

    // The global panel takes the free slots after the last effect tile (spanning to the end of that row)
    if (globalPanel != nullptr)
    {
        const int idx = static_cast<int>(tiles.size());
        const int r = idx / kCols, c = idx % kCols;
        if (r < kRows)
            globalPanel->setBounds(grid.getX() + c * cellW, grid.getY() + r * cellH, (kCols - c) * cellW, cellH);
        else
            globalPanel->setBounds(0, 0, 0, 0);
    }

    settingsOverlay.setBounds(getLocalBounds());
}
