#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChoiceSelector.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

// The current shape, scaled by Amount, with a playhead and the gain being applied.
class ShapeDisplay : public juce::Component
{
public:
    explicit ShapeDisplay (DuckProcessor&);

    void setStatus (const juce::String&, juce::Colour);
    void paint (juce::Graphics&) override;

private:
    DuckProcessor& processor;
    juce::String status;
    juce::Colour statusColour;
};

// Clickable tiles, one per shape, each with a thumbnail.
class ShapePicker : public juce::Component
{
public:
    explicit ShapePicker (DuckProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> tile (int index) const;
    int tileAt (juce::Point<float>) const;

    DuckProcessor& processor;
    juce::ParameterAttachment attachment;
    int selected = 0;
    int hovered = -1;
};

class DuckEditor : public juce::AudioProcessorEditor,
                   private juce::Timer
{
public:
    explicit DuckEditor (DuckProcessor&);
    ~DuckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    DuckProcessor& processor;
    ContourLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    ShapeDisplay display;
    ShapePicker picker;

    juce::Slider amount { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;

    ChoiceSelector rateSelector, triggerSelector;
    juce::Slider threshold { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;

    juce::Rectangle<int> rateArea, triggerArea, thresholdArea;
    int lastTriggerCount = 0;
    float triggerFlash = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuckEditor)
};
