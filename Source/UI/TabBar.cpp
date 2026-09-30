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

    // Equal widths, as on Rackbox's selector rows. The drawn row is the content
    // height less a few points above and below, and the panel's 13 point margin
    // either side, so the row lines up with the displays above it.
    const auto drawnHeight = (float) (contentHeight > 0 ? juce::jmin (contentHeight, getHeight()) : getHeight());
    const auto row = juce::Rectangle<float> (13.0f, 0.0f, (float) getWidth() - 26.0f, drawnHeight)
                         .withSizeKeepingCentre ((float) getWidth() - 26.0f, juce::jmin (24.0f, drawnHeight - 6.0f));

    const auto width = (row.getWidth() - gap * (float) (tabs.size() - 1)) / (float) tabs.size();

    return { row.getX() + (float) index * (width + gap), row.getY(), width, row.getHeight() };
}

juce::Rectangle<float> TabBar::hitAreaForTab (int index) const
{
    auto area = areaForTab (index);

    if (area.isEmpty())
        return area;

    // Down to the last pixel, whatever the labels do. The dead space below them
    // exists because the host may not deliver a click there - not because a
    // click there should be ignored if it arrives. And up to the top, and half
    // the gap either side: the gaps between buttons are drawing, not a place
    // where a click should fall through to nothing.
    area.setTop (0.0f);
    area.setBottom ((float) getHeight());
    area = area.expanded (gap * 0.5f, 0.0f);

    if (index == 0)
        area.setLeft (0.0f);

    if (index == tabs.size() - 1)
        area.setRight ((float) getWidth());

    return area;
}

void TabBar::paint (juce::Graphics& g)
{
    // No background: the strip sits on the panel body, and the band below the
    // buttons is where the editor writes its footer line.
    const auto& t = theme();

    g.setFont (t.tabFont());

    for (int i = 0; i < tabs.size(); ++i)
    {
        const auto area = areaForTab (i);
        const auto active = i == selectedIndex;
        const auto hovered = ! active && i == hoverIndex;

        g.setColour (active ? t.accent : (hovered ? t.buttonHover : t.button));
        g.fillRoundedRectangle (area, 5.0f);

        g.setColour (active ? t.onAccent : t.text);
        g.drawText (tabs[i], area, juce::Justification::centred, false);
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
