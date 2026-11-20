#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Slider property that marks a knob as the big "main" knob of a tile.
namespace EasyUI
{
    inline const juce::Identifier mainKnobProp { "easyMainKnob" };
    // Knobs whose "off" is the centre (pitch): the arc starts at 12 o'clock and goes either way
    inline const juce::Identifier bipolarKnobProp { "easyBipolarKnob" };

    namespace Colours
    {
        const juce::Colour bgTop    { 0xff0c0e13 };
        const juce::Colour bgBottom { 0xff151824 };
        const juce::Colour card     { 0xff161922 };
        const juce::Colour cardOff  { 0xff12141b };
        const juce::Colour track    { 0xff262b3a };
        const juce::Colour textDim  { 0xff6b7488 };
        const juce::Colour textMid  { 0xff9aa4b8 };
    }
}

// Slider that shows the host's own context menu on right-click (in FL Studio: "Create
// automation clip", "Link to controller", ...). Falls back to JUCE's default behaviour if
// the host doesn't provide one.
class HostMenuSlider : public juce::Slider
{
public:
    juce::AudioProcessorParameter* hostParameter = nullptr;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && hostParameter != nullptr)
        {
            if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
                if (auto* ctx = editor->getHostContext())
                    if (auto menu = ctx->getContextMenuForParameter(hostParameter))
                    {
                        menu->showNativeMenu(editor->getMouseXYRelative());
                        return;
                    }
        }
        juce::Slider::mouseDown(e);
    }
};

class EasyKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EasyKnobLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff2ee6ff));
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xf0171a24));
        setColour(juce::TooltipWindow::textColourId, juce::Colour(0xffdfe6f5));
        setColour(juce::TooltipWindow::outlineColourId, juce::Colour(0xff2b3244));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, const float rotaryStartAngle,
                          const float rotaryEndAngle, juce::Slider& slider) override
    {
        const bool isMain = static_cast<bool>(slider.getProperties().getWithDefault(EasyUI::mainKnobProp, false));
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const bool bipolar = static_cast<bool>(slider.getProperties().getWithDefault(EasyUI::bipolarKnobProp, false));
        const bool active = bipolar ? std::abs(sliderPos - 0.5f) > 0.002f : sliderPos > 0.002f;

        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                             static_cast<float>(width), static_cast<float>(height)).reduced(isMain ? 4.0f : 3.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        const float trackW = isMain ? 5.0f : 3.5f;
        const float arcR = radius - trackW * 0.5f;

        // Track
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(EasyUI::Colours::track);
        g.strokePath(track, juce::PathStrokeType(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc with soft glow
        if (active)
        {
            juce::Path arc;
            const float fromAngle = bipolar ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle) : rotaryStartAngle;
            arc.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f,
                              juce::jmin(fromAngle, toAngle), juce::jmax(fromAngle, toAngle), true);
            if (isMain)
            {
                g.setColour(accent.withAlpha(0.18f));
                g.strokePath(arc, juce::PathStrokeType(trackW + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour(accent);
            g.strokePath(arc, juce::PathStrokeType(trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Dial body
        const float bodyR = arcR - trackW * 0.5f - (isMain ? 5.0f : 3.5f);
        juce::ColourGradient body(juce::Colour(0xff2b3040), centre.x, centre.y - bodyR,
                                  juce::Colour(0xff161923), centre.x, centre.y + bodyR, false);
        g.setGradientFill(body);
        g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour(juce::Colour(0xff3a4156));
        g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // Pointer
        const float inner = bodyR * 0.35f, outer = bodyR * 0.88f;
        const float sx = std::sin(toAngle), sy = -std::cos(toAngle);
        g.setColour(active ? juce::Colours::white : juce::Colour(0xff6b7488));
        g.drawLine(centre.x + sx * inner, centre.y + sy * inner,
                   centre.x + sx * outer, centre.y + sy * outer,
                   isMain ? 3.0f : 2.2f);
    }
};
