//
//  HeaderBar.cpp
//  FX Analyzer
//

#include "HeaderBar.h"
#include "../TextUtf8.h"
#include "Surfaces.h"

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

void HeaderBar::setStatus (const juce::StringArray& segments, bool isFrozen)
{
    if (segments == status && isFrozen == frozen)
        return;

    status = segments;
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

void HeaderBar::setDarkLit (bool lit)
{
    if (darkLit == lit)
        return;

    darkLit = lit;
    repaint (darkArea);
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
    // The 0.4.0 design: a 72 point row, 12 points from the window's top and
    // sides (the editor is inset by 2) - the wordmark in a 232 point column,
    // the display, and a 132 point column of two buttons, 12 apart. The row
    // keeps its height while the header has room for it and gives way when the
    // window is made small; the type does not grow with it.
    auto area = getLocalBounds().withTrimmedLeft (10).withTrimmedRight (10).withTrimmedTop (10);
    area = area.withHeight (juce::jmin (72, area.getHeight() - 2));

    wordmarkArea = area.removeFromLeft (232).withTrimmedLeft (2);
    area.removeFromLeft (12);

    // Since 0.4.0 the panel's look is chosen in MENU (Colour, Dark Panel), so
    // the Dark button is gone; the area stays empty rather than removed so the
    // click and hover code needs no second path.
    darkArea = {};

    auto buttons = area.removeFromRight (132);
    area.removeFromRight (12);

    const auto buttonHeight = (buttons.getHeight() - 6) / 2;
    presetArea = buttons.removeFromTop (buttonHeight);
    menuArea   = buttons.removeFromBottom (buttonHeight);
    displayArea = area;
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto& t = theme();

    // The wordmark, as on Kitbox and Rackbox: the first word in the accent,
    // the second in ink, and a line of small print under it. Since 0.4.0
    // smaller and tighter - it names the panel, it does not need to shout.
    {
        // A 21 point name over 9 point small print, 8 apart, centred as a pair.
        const auto font = juce::Font (juce::FontOptions (21.0f, juce::Font::bold)).withExtraKerningFactor (0.15f);
        const auto fxWidth = juce::GlyphArrangement::getStringWidth (font, "FX");
        const auto spaceWidth = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (21.0f, juce::Font::bold)), " ");

        auto area = wordmarkArea.toFloat().withSizeKeepingCentre ((float) wordmarkArea.getWidth(), 21.0f + 8.0f + 11.0f);

        auto nameRow = area.removeFromTop (21.0f);
        g.setFont (font);
        g.setColour (t.accent);
        g.drawText ("FX", nameRow, juce::Justification::centredLeft, false);
        g.setColour (t.text);
        g.drawText ("ANALYZER", nameRow.withTrimmedLeft (fxWidth + spaceWidth), juce::Justification::centredLeft, false);

        area.removeFromTop (8.0f);
        g.setColour (t.label);
        g.setFont (Theme::capsFontAt (9.0f));
        g.drawText ("MEASUREMENT SUITE", area, juce::Justification::centredLeft, false);
    }

    // The display: which page, what it is for, and the state of the input,
    // with a lamp before the name - lit while live, dark while frozen.
    {
        Surfaces::recessed (g, t, displayArea.toFloat(), 6.0f);

        // Two lines, 7 apart, centred in the display: the name with its lamp
        // and the status on the right, then the description under the name.
        const auto inner = displayArea.toFloat().reduced (16.0f, 0.0f);
        const auto block = inner.withSizeKeepingCentre (inner.getWidth(), 16.0f + 7.0f + 13.0f);
        const auto top = block.withHeight (16.0f);
        const auto textLeft = 6.0f + 9.0f;

        Surfaces::lamp (g, t, { inner.getX() + 3.0f, top.getCentreY() }, ! frozen);

        // The status from the right: LIVE or FROZEN last, a hairline before
        // each segment but the first.
        const auto statusFont = Theme::numberFont (10.5f);
        g.setFont (statusFont);

        auto line = top;
        auto segments = status;
        segments.add (frozen ? "FROZEN" : "LIVE");

        for (int i = segments.size(); --i >= 0;)
        {
            const auto width = juce::GlyphArrangement::getStringWidth (statusFont, segments[i]) + 1.0f;
            g.setColour (frozen && i == segments.size() - 1 ? t.warning : t.bedDim);
            g.drawText (segments[i], line.removeFromRight (width), juce::Justification::centredRight, false);

            if (i > 0)
            {
                line.removeFromRight (9.0f);
                g.setColour (t.gridMajor());
                g.fillRect (line.removeFromRight (1.0f).withSizeKeepingCentre (1.0f, 10.0f));
                line.removeFromRight (9.0f);
            }
        }

        g.setColour (t.bedText);
        g.setFont (Theme::numberFont (13.0f, true).withExtraKerningFactor (0.04f));
        g.drawText (pageName, line.withTrimmedLeft (textLeft), juce::Justification::centredLeft, true);

        g.setColour (t.bedDim);
        g.setFont (Theme::numberFont (10.5f));
        g.drawText (pageDescription, block.withTrimmedTop (16.0f + 7.0f).withTrimmedLeft (textLeft),
                    juce::Justification::centredLeft, true);
    }

    // Preset and menu: buttons standing on the body, with smaller type than
    // the tabs - they are used once a session, the tabs all the time.
    const auto smallFont = Theme::capsFontAt (9.0f);
    const auto fillFor = [&t] (bool hovered) { return hovered ? t.buttonHover : t.button; };

    {
        const auto area = presetArea.toFloat();
        Surfaces::button (g, t, area, fillFor (hoveringPreset), false);

        auto inner = area.reduced (10.0f, 0.0f);
        g.setFont (Theme::capsFontAt (8.0f));
        g.setColour (t.label);
        g.drawText ("PRESET", inner, juce::Justification::centredLeft, false);

        // The name, then a small chevron pointing down: this opens a menu.
        const auto chevron = inner.removeFromRight (8.0f).withSizeKeepingCentre (6.5f, 3.25f);
        juce::Path arrow;
        arrow.startNewSubPath (chevron.getX(), chevron.getY());
        arrow.lineTo (chevron.getCentreX(), chevron.getBottom());
        arrow.lineTo (chevron.getRight(), chevron.getY());
        g.setColour (t.text);
        g.strokePath (arrow, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (smallFont);
        g.drawText (presetName.toUpperCase(), inner.withTrimmedRight (6.0f), juce::Justification::centredRight, true);
    }

    {
        const auto area = menuArea.toFloat();
        Surfaces::button (g, t, area, fillFor (hoveringMenu), false);

        g.setFont (smallFont);
        const auto textWidth = juce::GlyphArrangement::getStringWidth (smallFont, "MENU");
        const auto iconWidth = 10.0f, spacing = 8.0f;
        const auto left = area.getCentreX() - (iconWidth + spacing + textWidth) * 0.5f;

        g.setColour (t.text);
        for (int i = -1; i <= 1; ++i)
            g.fillRoundedRectangle (left, area.getCentreY() + (float) i * 3.5f - 0.7f, iconWidth, 1.4f, 0.7f);

        g.drawText ("MENU", juce::Rectangle<float> (left + iconWidth + spacing, area.getY(), textWidth + 2.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    // Dark, as a small switch: a track, a knob that moves, and the word.
    // Not drawn since the switch moved into MENU (darkArea is empty).
    if (! darkArea.isEmpty())
    {
        const auto area = darkArea.toFloat();
        Surfaces::button (g, t, area, fillFor (hoveringDark), false);

        const auto track = juce::Rectangle<float> (20.0f, 10.0f).withCentre ({ area.getX() + 15.0f, area.getCentreY() });
        g.setColour (darkLit ? t.accent : t.knobEdge);
        g.fillRoundedRectangle (track, 5.0f);

        const auto knob = juce::Rectangle<float> (6.0f, 6.0f)
                              .withCentre ({ darkLit ? track.getRight() - 5.0f : track.getX() + 5.0f, track.getCentreY() });
        g.setColour (t.knobFace);
        g.fillEllipse (knob);

        g.setColour (t.text);
        g.setFont (Theme::labelFontAt (7.5f));
        g.drawText ("DARK", area.withTrimmedLeft (27.0f), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void HeaderBar::mouseDown (const juce::MouseEvent& event)
{
    if (presetArea.contains (event.getPosition()) && onPresetClicked != nullptr)
        onPresetClicked();
    else if (menuArea.contains (event.getPosition()) && onMenuClicked != nullptr)
        onMenuClicked();
    else if (darkArea.contains (event.getPosition()) && onDarkClicked != nullptr)
        onDarkClicked();
}

void HeaderBar::mouseMove (const juce::MouseEvent& event)
{
    const auto preset = presetArea.contains (event.getPosition());
    const auto menu   = menuArea.contains (event.getPosition());
    const auto dark   = darkArea.contains (event.getPosition());

    if (preset != hoveringPreset || menu != hoveringMenu || dark != hoveringDark)
    {
        hoveringPreset = preset;
        hoveringMenu   = menu;
        hoveringDark   = dark;
        repaint();
    }
}

void HeaderBar::mouseExit (const juce::MouseEvent&)
{
    if (hoveringPreset || hoveringMenu || hoveringDark)
    {
        hoveringPreset = hoveringMenu = hoveringDark = false;
        repaint();
    }
}
