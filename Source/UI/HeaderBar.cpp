//
//  HeaderBar.cpp
//  FX Analyzer
//

#include "HeaderBar.h"
#include "../TextUtf8.h"

//==============================================================================
HeaderBar::HeaderBar()
{
    setInterceptsMouseClicks (true, false);
}

void HeaderBar::setTitle (const juce::String& newTitle)
{
    title = newTitle;
    repaint();
}

void HeaderBar::setPresetName (const juce::String& newName)
{
    if (presetName == newName)
        return;

    presetName = newName;
    repaint();
}

void HeaderBar::setActivity (float level)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, level);

    // A tenth of the range is the smallest step worth a repaint. Redrawing the
    // header thirty times a second because the lamp moved by a thousandth
    // costs more than the lamp is worth.
    if (std::abs (clamped - activity) < 0.1f)
        return;

    activity = clamped;
}

//==============================================================================
void HeaderBar::resized()
{
    auto area = getLocalBounds().reduced (14, 8);

    // No lamp any more, so the title starts at the panel's own left margin
    // rather than where the lamp used to leave it.
    lampArea = {};

    menuArea   = area.removeFromRight (40);
    presetArea = area.removeFromRight (120);
    titleArea  = area;
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    g.setColour (t.text);
    g.setFont (t.titleFont());
    g.drawText (title, titleArea, juce::Justification::centredLeft, false);

    g.setColour (hoveringPreset ? t.text : t.dimText());
    g.setFont (t.labelFont().withHeight (t.labelSize + 4.0f));
    g.drawText (presetName + "  " + utf8 ("\xe2\x96\xb6"), presetArea, juce::Justification::centredRight, false);

    // Three lines, drawn at the same weight as the chevrons so the header does
    // not look like it was assembled from two different panels.
    const auto burger = menuArea.toFloat().withSizeKeepingCentre (26.0f, 18.0f);

    g.setColour (hoveringMenu ? t.accent : t.text);

    for (int line = 0; line < 3; ++line)
    {
        const auto y = burger.getY() + burger.getHeight() * 0.5f * (float) line;
        g.fillRoundedRectangle (burger.getX(), y, burger.getWidth(), 2.6f, 1.3f);
    }
}

//==============================================================================
void HeaderBar::mouseDown (const juce::MouseEvent& event)
{
    if (presetArea.contains (event.getPosition()) && onPresetClicked != nullptr)
        onPresetClicked();
    else if (menuArea.contains (event.getPosition()) && onMenuClicked != nullptr)
        onMenuClicked();
}

void HeaderBar::mouseMove (const juce::MouseEvent& event)
{
    const auto preset = presetArea.contains (event.getPosition());
    const auto menu   = menuArea.contains (event.getPosition());

    if (preset != hoveringPreset || menu != hoveringMenu)
    {
        hoveringPreset = preset;
        hoveringMenu   = menu;
        repaint();
    }
}

void HeaderBar::mouseExit (const juce::MouseEvent&)
{
    if (hoveringPreset || hoveringMenu)
    {
        hoveringPreset = hoveringMenu = false;
        repaint();
    }
}
