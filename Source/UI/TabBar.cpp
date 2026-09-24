//
//  TabBar.cpp
//  FX Analyzer
//

#include "TabBar.h"

//==============================================================================
TabBar::TabBar()
{
    setInterceptsMouseClicks (true, false);
}

void TabBar::setTabs (const juce::StringArray& newTabs)
{
    tabs = newTabs;
    selectedIndex = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), selectedIndex);
    repaint();
}

void TabBar::setContentHeight (int pixels)
{
    const auto clamped = juce::jmax (0, pixels);

    if (clamped == contentHeight)
        return;

    contentHeight = clamped;
    repaint();
}

void TabBar::setSelectedIndex (int index, juce::NotificationType notify)
{
    const auto clamped = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), index);

    if (clamped == selectedIndex)
        return;

    selectedIndex = clamped;
    repaint();

    if (notify != juce::dontSendNotification && onChange != nullptr)
        onChange (selectedIndex);
}

//==============================================================================
juce::Rectangle<float> TabBar::areaForTab (int index) const
{
    if (tabs.isEmpty() || ! juce::isPositiveAndBelow (index, tabs.size()))
        return {};

    // Width follows the text, with the leftover space shared out equally. Six
    // equal columns would put SPECTRUM and PITCH in boxes of the same width,
    // which leaves the short names floating in space and the long ones touching
    // their neighbours - the mockup's spacing is text-driven and it is right.
    const auto font = theme().tabFont();

    float totalText = 0.0f;
    std::vector<float> widths ((size_t) tabs.size());

    for (int i = 0; i < tabs.size(); ++i)
    {
        widths[(size_t) i] = juce::GlyphArrangement::getStringWidth (font, tabs[i]);
        totalText += widths[(size_t) i];
    }

    const auto padding = juce::jmax (8.0f, ((float) getWidth() - 24.0f - totalText) / (float) tabs.size());

    float x = 12.0f;

    for (int i = 0; i < index; ++i)
        x += widths[(size_t) i] + padding;

    const auto drawnHeight = contentHeight > 0 ? juce::jmin (contentHeight, getHeight())
                                               : getHeight();

    return { x, 0.0f, widths[(size_t) index] + padding, (float) drawnHeight };
}

juce::Rectangle<float> TabBar::hitAreaForTab (int index) const
{
    auto area = areaForTab (index);

    if (area.isEmpty())
        return area;

    // Down to the last pixel, whatever the labels do. The dead space below them
    // exists because the host may not deliver a click there - not because a
    // click there should be ignored if it arrives.
    area.setBottom ((float) getHeight());

    if (index == 0)
        area.setLeft (0.0f);

    if (index == tabs.size() - 1)
        area.setRight ((float) getWidth());

    return area;
}

void TabBar::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    const auto font = t.tabFont();
    g.setFont (font);

    for (int i = 0; i < tabs.size(); ++i)
    {
        const auto area = areaForTab (i);
        const auto active = i == selectedIndex;

        const auto hovered = ! active && i == hoverIndex;

        g.setColour (active ? t.text
                            : t.accent.withMultipliedAlpha (hovered ? hoverAlpha : inactiveAlpha));

        g.drawText (tabs[i], area, juce::Justification::centred, false);

        if (! active && ! hovered)
            continue;

        // The bar, as wide as the label rather than as wide as the box - the
        // boxes share out the leftover space between them and are not the shape
        // of anything on screen.
        //
        // Solid under the selected tab, faint under the one the pointer is on.
        // Both states then say the same thing in the same place and differ only
        // in how firmly they say it, which is a distinction that survives any
        // palette. Brightness of the label alone did not: see the note at the
        // top of the header.
        const auto textWidth = juce::GlyphArrangement::getStringWidth (font, tabs[i]);

        const auto thickness = juce::jmax (2.0f, (float) getHeight() / 14.0f);
        const auto gap       = juce::jmax (4.0f, (float) getHeight() / 10.0f);

        g.setColour (active ? t.text : t.text.withMultipliedAlpha (hoverMarkAlpha));
        g.fillRect (juce::Rectangle<float> (textWidth, thickness)
                        .withCentre ({ area.getCentreX(), area.getBottom() - gap - thickness * 0.5f }));
    }
}

//==============================================================================
int TabBar::indexAt (juce::Point<int> position) const
{
    for (int i = 0; i < tabs.size(); ++i)
        if (hitAreaForTab (i).contains (position.toFloat()))
            return i;

    return -1;
}

void TabBar::mouseDown (const juce::MouseEvent& event)
{
    const auto index = indexAt (event.getPosition());

    if (index >= 0)
        setSelectedIndex (index);
}

void TabBar::mouseMove (const juce::MouseEvent& event)
{
    const auto index = indexAt (event.getPosition());

    if (index != hoverIndex)
    {
        hoverIndex = index;
        repaint();
    }
}

void TabBar::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex >= 0)
    {
        hoverIndex = -1;
        repaint();
    }
}
