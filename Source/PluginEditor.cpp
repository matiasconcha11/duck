#include "PluginEditor.h"
#include "Shapes.h"

namespace
{
    const juce::Colour accent { 0xffffb347 };
    constexpr int headerHeight = 52;
    constexpr int footerHeight = 84;
    constexpr int margin = 16;

    juce::Path curvePath (const Curve& curve, juce::Rectangle<float> area, float amount, int steps)
    {
        juce::Path path;

        for (int i = 0; i <= steps; ++i)
        {
            const auto x = (float) i / (float) steps;
            const auto y = 1.0f - amount * (1.0f - curve.evaluate (x));
            const juce::Point<float> p { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };

            if (i == 0)
                path.startNewSubPath (p);
            else
                path.lineTo (p);
        }

        return path;
    }

    float paramValue (DuckProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }
}

//==============================================================================
ShapeDisplay::ShapeDisplay (DuckProcessor& p) : processor (p) {}

void ShapeDisplay::setStatus (const juce::String& text, juce::Colour colour)
{
    status = text;
    statusColour = colour;
}

void ShapeDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff101115));
    g.fillRoundedRectangle (bounds, 6.0f);

    auto inner = bounds.reduced (12.0f, 10.0f);
    const auto caption = inner.removeFromTop (18.0f).toNearestInt();
    inner.removeFromTop (8.0f);
    const auto area = inner;

    // Beat grid: quarters of the cycle.
    for (int i = 0; i <= 4; ++i)
    {
        g.setColour (juce::Colour (i == 0 || i == 4 ? 0xff252830 : 0xff191b20));
        g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * (float) i / 4.0f), area.getY(), area.getBottom());
    }

    const auto& curve = processor.getShape ((int) paramValue (processor, Params::shape));
    const auto amount = paramValue (processor, Params::amount);
    const auto line = curvePath (curve, area, amount, (int) area.getWidth());

    juce::Path fill (line);
    fill.lineTo (area.getBottomRight());
    fill.lineTo (area.getBottomLeft());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.3f), area.getTopLeft(), accent.withAlpha (0.02f), area.getBottomLeft(), false));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Playhead at the gain actually being applied.
    const auto phase = processor.displayPhase.load();
    const auto gain = processor.displayGain.load();
    const juce::Point<float> head { area.getX() + phase * area.getWidth(), area.getBottom() - gain * area.getHeight() };
    g.setColour (Theme::text.withAlpha (0.25f));
    g.drawVerticalLine (juce::roundToInt (head.x), area.getY(), area.getBottom());
    g.setColour (accent.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (head));
    g.setColour (Theme::text);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (head));

    // Caption row: what's triggering on the left, gain reduction on the right.
    g.setFont (Theme::caps (10.5f));
    g.setColour (statusColour);
    g.drawText (status, caption, juce::Justification::centredLeft);

    g.setColour (Theme::dim);
    const auto reduction = juce::Decibels::gainToDecibels (gain, -60.0f);
    g.drawText (reduction <= -59.9f ? "-INF DB" : juce::String (reduction, 1) + " DB", caption, juce::Justification::centredRight);
}

//==============================================================================
ShapePicker::ShapePicker (DuckProcessor& p)
    : processor (p),
      attachment (*p.apvts.getParameter (Params::shape), [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> ShapePicker::tile (int index) const
{
    const auto count = Shapes::names().size();
    const auto gap = 6.0f;
    const auto width = ((float) getWidth() - gap * (float) (count - 1)) / (float) count;
    return { (float) index * (width + gap), 0.0f, width, (float) getHeight() };
}

int ShapePicker::tileAt (juce::Point<float> position) const
{
    for (int i = 0; i < Shapes::names().size(); ++i)
        if (tile (i).contains (position))
            return i;

    return -1;
}

void ShapePicker::paint (juce::Graphics& g)
{
    const auto names = Shapes::names();

    for (int i = 0; i < names.size(); ++i)
    {
        const auto area = tile (i);
        const auto isSelected = i == selected;

        g.setColour (isSelected ? accent.withAlpha (0.14f) : (i == hovered ? Theme::raised.brighter (0.05f) : Theme::raised));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (isSelected ? accent.withAlpha (0.7f) : Theme::outline);
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

        const auto thumb = area.reduced (10.0f, 8.0f).withTrimmedBottom (16.0f);
        g.setColour (isSelected ? accent : Theme::dim);
        g.strokePath (curvePath (processor.getShape (i), thumb, 1.0f, 40),
                      juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (Theme::caps (9.5f));
        g.setColour (isSelected ? Theme::text : Theme::dim);
        g.drawText (names[i].toUpperCase(), area.reduced (4.0f, 5.0f).removeFromBottom (12.0f).toNearestInt(), juce::Justification::centred);
    }
}

void ShapePicker::mouseDown (const juce::MouseEvent& e)
{
    if (const auto index = tileAt (e.position); index >= 0)
        attachment.setValueAsCompleteGesture ((float) index);
}

void ShapePicker::mouseMove (const juce::MouseEvent& e)
{
    if (const auto index = tileAt (e.position); index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void ShapePicker::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
DuckEditor::DuckEditor (DuckProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      display (p),
      picker (p),
      rateSelector (*p.apvts.getParameter (Params::rate), Params::rateNames()),
      triggerSelector (*p.apvts.getParameter (Params::trigger), Params::triggerNames())
{
    setLookAndFeel (&lookAndFeel);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff6b6b));
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, Params::bypass, bypassButton);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (display);
    addAndMakeVisible (picker);

    for (auto* slider : { &amount, &threshold })
    {
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setColour (juce::Slider::textBoxTextColourId, Theme::text);
        slider->lookAndFeelChanged();
        addAndMakeVisible (slider);
    }

    amount.setColour (juce::Slider::rotarySliderFillColourId, accent);
    amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 22);
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, Params::amount, amount);

    threshold.setColour (juce::Slider::rotarySliderFillColourId, Theme::text.withAlpha (0.85f));
    threshold.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 18);
    threshold.setTooltip ("How loud the sidechain has to get to trigger a duck");
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, Params::threshold, threshold);

    rateSelector.setTooltip ("Length of one duck");
    triggerSelector.setTooltip ("Tempo: duck on every beat of the host. Sidechain: duck on each hit of the track routed to the sidechain");
    addAndMakeVisible (rateSelector);
    addAndMakeVisible (triggerSelector);

    setSize (640, 420);
    timerCallback();
    startTimerHz (60);
}

DuckEditor::~DuckEditor()
{
    setLookAndFeel (nullptr);
}

void DuckEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    const auto header = getLocalBounds().withHeight (headerHeight);
    g.setColour (Theme::panel);
    g.fillRect (header);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    // Logo: a duck curve.
    const auto mark = juce::Rectangle<float> ((float) margin, 15.0f, 24.0f, 22.0f);
    juce::Path logo;
    logo.startNewSubPath (mark.getX(), mark.getY());
    logo.lineTo (mark.getX(), mark.getBottom());
    logo.quadraticTo (mark.getX() + 4.0f, mark.getY(), mark.getRight(), mark.getY());
    g.setColour (accent);
    g.strokePath (logo, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto title = header.withTrimmedLeft (margin + 34);
    g.setColour (Theme::text);
    g.setFont (Theme::caps (18.0f).withExtraKerningFactor (0.3f));
    g.drawText ("DUCK", title, juce::Justification::centredLeft);
    g.setColour (Theme::faint);
    g.setFont (Theme::font (11.0f));
    g.drawText ("v" DUCK_VERSION, title.withTrimmedLeft (78), juce::Justification::centredLeft);

    // Footer with captions.
    const auto footer = getLocalBounds().withTop (getHeight() - footerHeight);
    g.setColour (Theme::panel);
    g.fillRect (footer);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (footer.getY(), 0.0f, (float) getWidth());

    g.setFont (Theme::caps (10.0f));
    g.setColour (Theme::dim);

    for (const auto& [area, caption] : { std::pair { rateArea, "RATE" }, std::pair { triggerArea, "TRIGGER" }, std::pair { thresholdArea, "THRESHOLD" } })
        g.drawText (caption, area.withHeight (16), juce::Justification::centredLeft);

    g.drawText ("AMOUNT", amount.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);

    if (triggerFlash > 0.01f)
    {
        g.setColour (accent.withAlpha (triggerFlash));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) triggerArea.getX() + 62.0f, (float) triggerArea.getY() + 8.0f }));
    }
}

void DuckEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 11);
    bypassButton.setBounds (header.removeFromRight (80));

    auto footer = bounds.removeFromBottom (footerHeight).reduced (margin, 12);
    rateArea = footer.removeFromLeft (230);
    footer.removeFromLeft (20);
    triggerArea = footer.removeFromLeft (170);
    footer.removeFromLeft (20);
    thresholdArea = footer;

    rateSelector.setBounds (rateArea.withTrimmedTop (22).withHeight (28));
    triggerSelector.setBounds (triggerArea.withTrimmedTop (22).withHeight (28));
    threshold.setBounds (thresholdArea.withTrimmedTop (18).withHeight (40).withWidth (130));

    auto body = bounds.reduced (margin, 14);
    auto knobArea = body.removeFromRight (180);
    amount.setBounds (knobArea.withSizeKeepingCentre (150, 172).translated (0, 8));
    body.removeFromRight (12);

    picker.setBounds (body.removeFromBottom (64));
    body.removeFromBottom (10);
    display.setBounds (body);
}

void DuckEditor::timerCallback()
{
    display.repaint();

    const auto sidechainMode = (int) paramValue (processor, Params::trigger) == Params::sidechainTrigger;
    threshold.setEnabled (sidechainMode);
    threshold.setAlpha (sidechainMode ? 1.0f : 0.4f);

    if (sidechainMode && ! processor.sidechainConnected.load())
        display.setStatus ("ROUTE THE KICK TO THE SIDECHAIN IN YOUR HOST", juce::Colour (0xffff6b6b));
    else
        display.setStatus (sidechainMode ? "DUCKING ON EACH SIDECHAIN HIT" : "DUCKING ON EVERY BEAT", Theme::dim);

    const auto triggers = processor.triggerCount.load();
    triggerFlash = triggers != lastTriggerCount ? 1.0f : triggerFlash * 0.85f;
    lastTriggerCount = triggers;

    if (sidechainMode)
        repaint (triggerArea.withHeight (18));
}
