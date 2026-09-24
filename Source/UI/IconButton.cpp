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

void IconButton::setToggleState (bool shouldBeOn)
{
    if (on == shouldBeOn)
        return;

    on = shouldBeOn;
    repaint();
}

//==============================================================================
void IconButton::paint (juce::Graphics& g)
{
    const auto& t = theme();

    auto area = getLocalBounds().toFloat().reduced (2.0f);
    const auto diameter = juce::jmin (area.getWidth(), area.getHeight());
    area = juce::Rectangle<float> (diameter, diameter).withCentre (area.getCentre());

    // An active toggle is filled, not merely brighter. On a panel where the
    // accent colour is also the colour of every curve, "slightly brighter
    // green" is not a state anybody notices.
    if (on)
    {
        g.setColour (t.accent);
        g.fillEllipse (area);
    }

    g.setColour (on ? t.accent : (hovering ? t.text : t.accent));
    g.drawEllipse (area.reduced (1.0f), 2.0f);

    const auto symbolColour = on ? t.background : (hovering ? t.text : t.accent);
    g.setColour (symbolColour);

    const auto inner = area.reduced (diameter * 0.28f);

    switch (icon)
    {
        case Icon::magnifier: drawMagnifier (g, inner); break;
        case Icon::snowflake: drawSnowflake (g, inner); break;
        case Icon::bass:      drawBass      (g, area);  break;
        case Icon::reset:     drawReset     (g, inner); break;
    }
}

void IconButton::drawMagnifier (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto lensDiameter = area.getWidth() * 0.72f;
    const auto lens = juce::Rectangle<float> (lensDiameter, lensDiameter)
                          .withCentre (area.getCentre().translated (-area.getWidth() * 0.08f,
                                                                    -area.getHeight() * 0.08f));

    g.drawEllipse (lens, 2.0f);

    juce::Path handle;
    handle.startNewSubPath (lens.getCentreX() + lensDiameter * 0.36f, lens.getCentreY() + lensDiameter * 0.36f);
    handle.lineTo (area.getRight(), area.getBottom());

    g.strokePath (handle, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
}

void IconButton::drawBass (juce::Graphics& g, juce::Rectangle<float> area) const
{
    // The whole circle, not the inset the strokes use: a letter has to be sized
    // against the ring around it or it reads as a smudge in the middle.
    g.setFont (Theme::font (area.getHeight() * 0.58f, juce::Font::bold));
    g.drawText ("B", area, juce::Justification::centred, false);
}

void IconButton::drawSnowflake (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto centre = area.getCentre();
    const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;

    juce::Path flake;

    // Three spokes at sixty degrees, each with a pair of barbs. Six separate
    // lines through the centre would draw the centre six times over and thicken
    // it into a blob at the sizes this is used at.
    for (int spoke = 0; spoke < 3; ++spoke)
    {
        const auto angle = juce::MathConstants<float>::pi * (float) spoke / 3.0f;
        const auto direction = juce::Point<float> (std::cos (angle), std::sin (angle));

        const auto tip  = centre + direction * radius;
        const auto tail = centre - direction * radius;

        flake.startNewSubPath (tail);
        flake.lineTo (tip);

        const auto barbAngleA = angle + juce::MathConstants<float>::pi * 0.75f;
        const auto barbAngleB = angle - juce::MathConstants<float>::pi * 0.75f;
        const auto barbLength = radius * 0.38f;

        for (auto end : { tip, tail })
        {
            const auto sign = end == tip ? 1.0f : -1.0f;

            flake.startNewSubPath (end);
            flake.lineTo (end + juce::Point<float> (std::cos (barbAngleA), std::sin (barbAngleA)) * barbLength * sign);
            flake.startNewSubPath (end);
            flake.lineTo (end + juce::Point<float> (std::cos (barbAngleB), std::sin (barbAngleB)) * barbLength * sign);
        }
    }

    g.strokePath (flake, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void IconButton::drawReset (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto centre = area.getCentre();
    const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.44f;

    juce::Path arrow;
    arrow.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                         -2.9f, 2.2f, true);

    g.strokePath (arrow, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    const auto head = centre.getPointOnCircumference (radius, 2.2f);

    juce::Path tip;
    tip.addTriangle (head.translated (-3.5f, -1.5f),
                     head.translated ( 3.5f, -1.5f),
                     head.translated ( 0.0f,  4.5f));
    g.fillPath (tip);
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
