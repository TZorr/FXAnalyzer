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

void HeaderBar::setPage (const juce::String& name, const juce::String& description)
{
    if (name == pageName && description == pageDescription)
        return;

    pageName = name;
    pageDescription = description;
    repaint();
}

void HeaderBar::setStatus (const juce::String& text, bool isFrozen)
{
    if (text == status && isFrozen == frozen)
        return;

    status = text;
    frozen = isFrozen;
    repaint (displayArea);
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
    // Fixed sizes, centred in whatever height the header is given: the header
    // grows with the window, the type does not.
    auto area = getLocalBounds().reduced (13, 0).withSizeKeepingCentre (getWidth() - 26, 52);

    wordmarkArea = area.removeFromLeft (262);
    area.removeFromLeft (13);

    auto buttons = area.removeFromRight (120);
    area.removeFromRight (10);

    presetArea = buttons.removeFromTop (22);
    menuArea   = buttons.removeFromBottom (22);
    displayArea = area;
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto& t = theme();

    // The wordmark, as on Kitbox and Rackbox: the first word in the accent,
    // the second in ink, and a line of small print under it.
    {
        auto area = wordmarkArea.toFloat();
        const auto font = juce::Font (juce::FontOptions (30.0f, juce::Font::bold)).withExtraKerningFactor (0.16f);
        const auto fxWidth = juce::GlyphArrangement::getStringWidth (font, "FX");

        auto nameRow = area.removeFromTop (32.0f);
        g.setFont (font);
        g.setColour (t.accent);
        g.drawText ("FX", nameRow, juce::Justification::centredLeft, false);
        g.setColour (t.text);
        g.drawText ("ANALYZER", nameRow.withTrimmedLeft (fxWidth + 1.0f), juce::Justification::centredLeft, false);

        area.removeFromTop (6.0f);
        g.setColour (t.label);
        g.setFont (t.labelFont());
        g.drawText ("6-PAGE ANALYZER", area.removeFromTop (12.0f), juce::Justification::centredLeft, false);
    }

    // The display: which page, what it is for, and the state of the input.
    {
        g.setColour (t.bed);
        g.fillRoundedRectangle (displayArea.toFloat(), 6.0f);

        auto inner = displayArea.reduced (14, 0);
        auto top = inner.withY (displayArea.getY() + 10).withHeight (18);

        g.setColour (frozen ? t.warning : t.bedDim);
        g.setFont (t.axisFont().withHeight (10.5f));
        g.drawText (frozen ? "FROZEN  " + utf8 ("\xc2\xb7") + "  " + status : status,
                    top, juce::Justification::centredRight, false);

        g.setColour (t.bedText);
        g.setFont (Theme::numberFont (14.0f, true));
        g.drawText (pageName, top, juce::Justification::centredLeft, false);

        g.setColour (t.bedDim);
        g.setFont (t.axisFont().withHeight (10.5f));
        g.drawText (pageDescription, inner.withY (displayArea.getY() + 30).withHeight (14),
                    juce::Justification::centredLeft, true);
    }

    // Preset and menu, as flat buttons.
    const auto button = [&] (juce::Rectangle<int> area, const juce::String& text, bool hovered)
    {
        g.setColour (hovered ? t.buttonHover : t.button);
        g.fillRoundedRectangle (area.toFloat(), 5.0f);

        g.setColour (t.text);
        g.setFont (t.labelFont());
        g.drawText (text, area.reduced (6, 0), juce::Justification::centred, true);
    };

    button (presetArea, presetName.toUpperCase() + "  " + utf8 ("\xe2\x96\xb8"), hoveringPreset);
    button (menuArea, "MENU", hoveringMenu);
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
