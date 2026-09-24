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
    // Compact steppers set their value at the heading size - the same size as
    // the "Mode" and "Scale" labels above them. Larger than the heading was the
    // first attempt and it fought the graph for attention; matching it makes
    // the column read as chrome, which is what it is.
    return compact ? theme().labelSize : theme().valueSize;
}

int StepperControl::getPreferredHeight() const
{
    const auto text = valueTextHeight();

    // The compact size is tighter than a proportional shrink of the large one,
    // and deliberately so. It has to hold five steppers in the height of a
    // graph, and at the large proportions five of them fill the column edge to
    // edge with nothing between - a row of controls that touch reads as one
    // block rather than five, which is exactly what a column of separate
    // choices must not look like. The looser large proportions stay where they
    // are: the Settings page has room, and there the value is the content.
    const auto labelFactor   = compact ? 1.35f : 1.6f;
    const auto chevronFactor = compact ? 1.15f : 1.3f;
    const auto valueFactor   = compact ? 1.50f : 1.7f;

    const auto labelRow   = label.isEmpty() ? 0.0f : theme().labelSize * labelFactor;
    const auto chevronRow = text * chevronFactor;
    const auto valueRow   = text * valueFactor;

    return juce::roundToInt (labelRow + chevronRow * 2.0f + valueRow);
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

    labelArea = label.isEmpty()
                    ? juce::Rectangle<int>()
                    : area.removeFromTop (juce::roundToInt (theme().labelSize * (compact ? 1.35f : 1.6f)));

    // The chevrons take a share proportional to the type rather than an equal
    // third: the value is the thing being read, and a value squeezed between
    // two arrows of the same height reads as one of three rows rather than as
    // the answer. Tying the share to the font is what makes the compact size a
    // single switch instead of a second set of numbers to keep in step.
    const auto chevronHeight = juce::jlimit (10, area.getHeight() / 3,
                                             juce::roundToInt (valueTextHeight()
                                                                   * (compact ? 1.15f : 1.3f)));

    upArea    = area.removeFromTop (chevronHeight);
    downArea  = area.removeFromBottom (chevronHeight);
    valueArea = area;
}

void StepperControl::paint (juce::Graphics& g)
{
    const auto& t = theme();

    if (! label.isEmpty())
    {
        g.setColour (t.accent);
        g.setFont (t.labelFont());
        g.drawText (label, labelArea, juce::Justification::centred, false);
    }

    const auto enabled = isNumeric() || items.size() > 1;

    drawChevron (g, upArea.toFloat(),   true,  enabled && hoverZone == Zone::up);
    drawChevron (g, downArea.toFloat(), false, enabled && hoverZone == Zone::down);

    g.setColour (t.text);
    g.setFont (t.font (valueTextHeight(), juce::Font::plain));
    g.drawText (getSelectedText(), valueArea, juce::Justification::centred, false);
}

void StepperControl::drawChevron (juce::Graphics& g, juce::Rectangle<float> area,
                                  bool pointingUp, bool highlighted) const
{
    const auto& t = theme();

    // The chevron is drawn from the area's centre outwards so that it stays put
    // when the row it lives in changes height; anchoring it to an edge makes
    // the two arrows drift apart as the window is resized.
    //
    // Its size comes from the value's type, not from the space it happens to
    // have been given. The ratios are the ones the large size was drawn at, so
    // nothing moves there - and the compact size gets arrows that match its own
    // font rather than the full-size ones shrunk only by whatever the column
    // width happened to clip off.
    const auto text = valueTextHeight();

    const auto width  = juce::jmin (area.getWidth() * 0.42f, text * 1.05f);
    const auto height = juce::jmin (area.getHeight() * 0.5f, text * 0.52f);
    const auto centre = area.getCentre();

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

    const auto live = isNumeric() || items.size() > 1;
    g.setColour (live ? (highlighted ? t.text : t.accent) : t.accentDim());
    g.strokePath (chevron, juce::PathStrokeType (juce::jmax (1.4f, text * 0.125f),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

//==============================================================================
StepperControl::Zone StepperControl::zoneAt (juce::Point<int> position) const
{
    // The value area splits between the two arrows rather than being dead
    // space. Half the panel's hit targets are 26 pixels tall, and a dead strip
    // between them is a click that does nothing for no reason the user can see.
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
