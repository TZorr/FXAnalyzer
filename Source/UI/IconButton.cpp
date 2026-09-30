//
//  IconButton.cpp
//  FX Analyzer
//

#include "IconButton.h"

//==============================================================================
IconButton::IconButton (Icon iconToDraw) : icon (iconToDraw)
{
    setInterceptsMouseClicks (true, false);
}

IconButton::IconButton (const juce::String& word) : icon (Icon::word), customText (word)
{
    setInterceptsMouseClicks (true, false);
}

void IconButton::setToggleState (bool shouldBeOn)
{
    if (on == shouldBeOn)
        return;

    on = shouldBeOn;
    repaint();
}

//==============================================================================
juce::String IconButton::text() const
{
    // Words rather than pictures since 0.2, as on Kitbox and Rackbox: every
    // other button on the panel says what it does, and a magnifier that means
    // "read the level under the pointer" was a picture that needed its tooltip.
    switch (icon)
    {
        case Icon::magnifier: return "CURSOR";
        case Icon::snowflake: return "FREEZE";
        case Icon::bass:      return "BASS";
        case Icon::reset:     return "RESET";
        case Icon::word:      return customText;
    }

    return {};
}

void IconButton::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto area = getLocalBounds().toFloat();

    // Lit in the accent while on, like every toggle on the panel.
    const auto fill = on       ? t.accent
                    : onBed    ? (hovering ? t.bedButtonHover : t.bedButton)
                               : (hovering ? t.buttonHover : t.button);

    g.setColour (fill);
    g.fillRoundedRectangle (area, 5.0f);

    g.setColour (on ? t.onAccent : (onBed ? t.curveAlt : t.text));
    g.setFont (t.labelFont());
    g.drawText (text(), area.reduced (3.0f, 0.0f), juce::Justification::centred, false);
}

//==============================================================================
void IconButton::mouseDown (const juce::MouseEvent&)
{
    if (toggles)
        setToggleState (! on);

    if (onClick != nullptr)
        onClick (on);
}

void IconButton::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();
}

void IconButton::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}
