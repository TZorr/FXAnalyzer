//
//  TabBar.cpp
//  FX Analyzer
//

#include "TabBar.h"
#include "Surfaces.h"

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

    // Equal widths, as on Rackbox's selector rows. The 0.4.0 design: a 30 point
    // row 8 under the display (the page's 6 and 2 here) with 14 under it for
    // the footer, and the panel's 12 point margin either side (10 here, the
    // editor is inset by 2), so the row lines up with the displays above it.
    // Scaled with the content height, as the strip itself is.
    const auto drawnHeight = (float) (contentHeight > 0 ? juce::jmin (contentHeight, getHeight()) : getHeight());
    const auto unit = drawnHeight / (float) Layout::tabBarHeight;
    const auto row = juce::Rectangle<float> (10.0f, 2.0f * unit, (float) getWidth() - 20.0f,
                                             juce::jmin (30.0f * unit, drawnHeight - 4.0f * unit));

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

    const auto digitFont = Theme::numberFont (8.5f);

    for (int i = 0; i < tabs.size(); ++i)
    {
        const auto area = areaForTab (i);
        const auto active = i == selectedIndex;
        const auto hovered = ! active && i == hoverIndex;

        Surfaces::button (g, t, area, active ? t.accent : (hovered ? t.buttonHover : t.button), active);

        // The key that opens the page, small and quiet at the left, so the
        // shortcuts are discoverable without a manual. Dropped when the tab is
        // too narrow to keep it clear of the name.
        g.setFont (t.tabFont());
        const auto nameWidth = juce::GlyphArrangement::getStringWidth (t.tabFont(), tabs[i]);

        if (area.getWidth() - nameWidth >= 40.0f)
        {
            g.setFont (digitFont);
            g.setColour (active ? t.onAccent.withAlpha (0.55f) : t.label);
            g.drawText (juce::String (i + 1), area.withTrimmedLeft (9.0f), juce::Justification::centredLeft, false);
            g.setFont (t.tabFont());
        }

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
