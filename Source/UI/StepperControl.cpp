//
//  StepperControl.cpp
//  FX Analyzer
//

#include "StepperControl.h"

//==============================================================================
StepperControl::StepperControl()
{
    setWantsKeyboardFocus (false);
    setInterceptsMouseClicks (true, false);
}

void StepperControl::setLabel (const juce::String& newLabel)
{
    label = newLabel;
    resized();
    repaint();
}

void StepperControl::setCompact (bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;

    compact = shouldBeCompact;
    resized();
    repaint();
}

float StepperControl::valueTextHeight() const
{
    return theme().valueSize;
}

int StepperControl::getPreferredHeight() const
{
    // Rackbox's StepButton with its name over it: the label row, a gap, and
    // the button. Compact and full size are the same since 0.2 - the value is
    // on a button now, and a button has one height on this panel.
    return (label.isEmpty() ? 0 : labelRowHeight + labelGap) + buttonHeight;
}

void StepperControl::setItems (const juce::StringArray& newItems)
{
    items = newItems;
    selectedIndex = juce::jlimit (0, juce::jmax (0, items.size() - 1), selectedIndex);
    repaint();
}

void StepperControl::setSelectedIndex (int index, juce::NotificationType notify)
{
    if (items.isEmpty())
        return;

    const auto wrapped = ((index % items.size()) + items.size()) % items.size();

    if (wrapped == selectedIndex && notify != juce::sendNotificationSync)
        return;

    selectedIndex = wrapped;
    repaint();

    if (notify != juce::dontSendNotification && onChange != nullptr)
        onChange (selectedIndex);
}

void StepperControl::setNumericRange (float minimum, float maximum, float step, float resetValue,
                                      std::function<juce::String (float)> formatter)
{
    numericMin   = minimum;
    numericMax   = maximum;
    numericStep  = juce::jmax (1.0e-4f, step);
    numericReset = resetValue;
    numericFormatter = std::move (formatter);

    setNumericValue (juce::jlimit (numericMin, numericMax, numericValue), juce::dontSendNotification);
}

void StepperControl::setNumericValue (float newValue, juce::NotificationType notify)
{
    const auto clamped = juce::jlimit (numericMin, numericMax, newValue);

    // No snapping to the step. The step is how far a *click* moves; a value
    // arriving from automation or from a restored session keeps whatever
    // resolution it had. Rounding here would quietly turn a continuous
    // parameter into a discrete one, which is exactly what this mode exists to
    // avoid.
    if (std::abs (clamped - numericValue) < 1.0e-6f && notify != juce::sendNotificationSync)
        return;

    numericValue = clamped;
    repaint();

    if (notify != juce::dontSendNotification && onNumericChange != nullptr)
        onNumericChange (numericValue);
}

juce::String StepperControl::getSelectedText() const
{
    if (isNumeric())
        return numericFormatter != nullptr ? numericFormatter (numericValue)
                                           : juce::String (numericValue, 1);

    return juce::isPositiveAndBelow (selectedIndex, items.size()) ? items[selectedIndex] : juce::String();
}

//==============================================================================
void StepperControl::resized()
{
    auto area = getLocalBounds();

    if (label.isEmpty())
    {
        labelArea = {};
    }
    else
    {
        labelArea = area.removeFromTop (labelRowHeight);
        area.removeFromTop (labelGap);
    }

    valueArea = area.removeFromTop (juce::jmin (buttonHeight, area.getHeight()));
    upArea = downArea = {};
}

void StepperControl::paint (juce::Graphics& g)
{
    const auto& t = theme();

    if (! label.isEmpty())
    {
        g.setColour (t.label);
        g.setFont (t.labelFont());
        g.drawText (label.toUpperCase(), labelArea, juce::Justification::centred, false);
    }

    const auto live = isNumeric() || items.size() > 1;
    const auto hovered = live && hoverZone != Zone::none;
    const auto button = valueArea.toFloat();

    g.setColour (hovered ? t.buttonHover : t.button);
    g.fillRoundedRectangle (button, 5.0f);

    g.setColour (live ? t.text : t.label);
    g.setFont (t.valueFont());
    g.drawText (getSelectedText(), button.reduced (4.0f, 0.0f), juce::Justification::centred, true);

    // Which way a click will go, shown on the half the pointer is over: the
    // upper half of the control steps up, the lower half down. A small mark at
    // the button's right edge rather than two permanent arrows - the arrows
    // took 18 of the 47 points a stepper had and the value 11.
    if (hovered)
        drawChevron (g, button.withTrimmedLeft (button.getWidth() - 14.0f).reduced (0.0f, 4.0f),
                     hoverZone == Zone::up, true);
}

void StepperControl::drawChevron (juce::Graphics& g, juce::Rectangle<float> area,
                                  bool pointingUp, bool) const
{
    const auto& t = theme();

    const auto centre = area.getCentre();
    const auto width  = 3.5f;
    const auto height = 3.0f;

    juce::Path chevron;

    if (pointingUp)
    {
        chevron.startNewSubPath (centre.x - width, centre.y + height * 0.5f);
        chevron.lineTo (centre.x, centre.y - height * 0.5f);
        chevron.lineTo (centre.x + width, centre.y + height * 0.5f);
    }
    else
    {
        chevron.startNewSubPath (centre.x - width, centre.y - height * 0.5f);
        chevron.lineTo (centre.x, centre.y + height * 0.5f);
        chevron.lineTo (centre.x + width, centre.y - height * 0.5f);
    }

    g.setColour (t.text);
    g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

//==============================================================================
StepperControl::Zone StepperControl::zoneAt (juce::Point<int> position) const
{
    // The whole control splits at the button's middle: anything above steps
    // up, including the label, anything below steps down. No dead strip - a
    // click that does nothing for no reason the user can see is the worst kind.
    if (position.y < valueArea.getCentreY())
        return Zone::up;

    return Zone::down;
}

void StepperControl::mouseDown (const juce::MouseEvent& event)
{
    // Alt-click resets, and it is on alt for a reason worth keeping.
    //
    // Reset used to be on double click, carried over from the ring knob it
    // replaced. On a knob that is harmless - nobody clicks a knob twice in a
    // row. On a stepper, repeated clicking *is* the interaction: raising a gain
    // by three decibels is three clicks, and any two of them inside the
    // double-click interval reset the value to zero. Reported as "Input
    // sometimes jumps back to 0"; it was not sometimes, it was whenever you
    // clicked at a normal speed.
    if (isNumeric() && event.mods.isAltDown())
    {
        if (onGestureStart != nullptr) onGestureStart();
        setNumericValue (numericReset);
        if (onGestureEnd != nullptr) onGestureEnd();

        return;
    }

    step (zoneAt (event.getPosition()) == Zone::up ? 1 : -1);
}

void StepperControl::mouseMove (const juce::MouseEvent& event)
{
    const auto zone = zoneAt (event.getPosition());

    if (zone != hoverZone)
    {
        hoverZone = zone;
        repaint();
    }
}

void StepperControl::mouseExit (const juce::MouseEvent&)
{
    if (hoverZone != Zone::none)
    {
        hoverZone = Zone::none;
        repaint();
    }
}

void StepperControl::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY > 0.0f)
        step (1);
    else if (wheel.deltaY < 0.0f)
        step (-1);
}

void StepperControl::step (int direction)
{
    if (isNumeric())
    {
        const auto fine = juce::ModifierKeys::getCurrentModifiers().isShiftDown();
        const auto amount = numericStep * (fine ? 0.1f : 1.0f);

        if (onGestureStart != nullptr) onGestureStart();
        setNumericValue (numericValue + (float) direction * amount);
        if (onGestureEnd != nullptr) onGestureEnd();

        return;
    }

    if (items.size() > 1)
        setSelectedIndex (selectedIndex + direction);
}

void StepperControl::mouseDoubleClick (const juce::MouseEvent&)
{
    // Deliberately nothing. JUCE sends this *in addition* to the second
    // mouseDown, so anything here fires during ordinary repeated clicking.
}

//==============================================================================
void layOutStepperColumn (juce::Rectangle<int> column, const std::vector<StepperControl*>& steppers)
{
    if (steppers.empty() || column.getHeight() <= 0)
        return;

    const auto slots = (int) steppers.size();

    for (int i = 0; i < slots; ++i)
    {
        auto* stepper = steppers[(size_t) i];

        if (stepper == nullptr)
            continue;

        // Centred on an evenly spaced slot rather than packed from the top.
        // Packed, a two-stepper page leaves the bottom two thirds of the column
        // empty beside a full-height graph, which looks like something failed
        // to draw.
        const auto centreY = column.getY() + juce::roundToInt ((float) column.getHeight() * ((float) i + 0.5f)
                                                                   / (float) slots);

        const auto height = juce::jmin (stepper->getPreferredHeight(), column.getHeight() / slots);

        stepper->setBounds (column.getX(), centreY - height / 2, column.getWidth(), height);
    }
}
