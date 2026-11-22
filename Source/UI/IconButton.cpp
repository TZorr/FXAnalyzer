//
//  IconButton.cpp
//  FX Analyzer
//

#include "IconButton.h"
#include "Surfaces.h"

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

namespace
{
    juce::Font buttonFont() { return Theme::capsFontAt (8.5f); }
    constexpr float lampDiameter = 6.0f, lampGap = 6.0f, padding = 10.0f;
}

int IconButton::getIdealWidth() const
{
    const auto textWidth = juce::GlyphArrangement::getStringWidth (buttonFont(), text());
    return juce::roundToInt (2.0f * padding + textWidth + (showsLamp ? lampDiameter + lampGap : 0.0f) + 2.0f);
}

void IconButton::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto area = getLocalBounds().toFloat();

    // Lit in the accent while on, like every toggle on the panel - unless the
    // button has a lamp, which says it instead.
    const auto lit = on && ! showsLamp;

    const auto fill = lit      ? t.accent
                    : onBed    ? (hovering ? t.bedButtonHover : t.bedButton)
                               : (hovering ? t.buttonHover : t.button);

    Surfaces::button (g, t, area, fill, lit, onBed, 3.0f);

    g.setFont (buttonFont());
    g.setColour (lit ? t.onAccent : (onBed ? t.curveAlt : t.text));

    if (! showsLamp)
    {
        g.drawText (text(), area.reduced (3.0f, 0.0f), juce::Justification::centred, false);
        return;
    }

    // Lamp and word as one group, centred.
    const auto textWidth = juce::GlyphArrangement::getStringWidth (buttonFont(), text()) + 2.0f;
    const auto left = area.getCentreX() - (lampDiameter + lampGap + textWidth) * 0.5f;

    Surfaces::lamp (g, t, { left + lampDiameter * 0.5f, area.getCentreY() }, on, lampDiameter);

    g.setColour (onBed ? t.curveAlt : t.text);
    g.drawText (text(), juce::Rectangle<float> (left + lampDiameter + lampGap, area.getY(), textWidth, area.getHeight()),
                juce::Justification::centredLeft, false);
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
