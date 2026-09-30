//
//  TabBar.h
//  FX Analyzer
//
//  Six page names across the bottom, as a row of flat buttons (since 0.2, the
//  Kitbox / Rackbox look): the selected page is the one lit in the accent, the
//  one under the pointer a shade darker than the rest.
//
//  The selection used to be a bar under the label, and the history of why is
//  worth one paragraph. With user themes the difference between the selected
//  tab and the others could not rest on a colour: in one theme the accent and
//  the text were the same near-white and the selected tab measured 1.00 against
//  the rest - the strip showed nothing, and it was reported as clicks not
//  working. With one fixed design the fill can carry it again, and EditorShot's
//  hit map holds it to a contrast floor so that it stays that way: a filled
//  orange button against grey ones, and dark type on the orange.
//

#pragma once

#include "PageBase.h"

class TabBar : public ThemedComponent
{
public:
    TabBar();

    void setTabs (const juce::StringArray&);
    void setSelectedIndex (int index, juce::NotificationType notify = juce::sendNotification);


    int getSelectedIndex() const noexcept { return selectedIndex; }

    /** The height the labels are centred in, measured from the top of the
        strip. Whatever is left below is drawn but never written into - see
        Layout::tabBarSafeBottom for why that dead space has to exist.

        Zero means the whole height, which is what a bare TabBar does. Note
        that this moves the *labels* only: the strip stays clickable to its
        last pixel, because a click that does arrive down there should still
        work. */
    void setContentHeight (int pixels);


    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Which tab covers a point, or -1. Public because the hit map in
        EditorShot sweeps every pixel of the strip through it: the question
        "does the click area agree with the label drawn there" is one only a
        machine has the patience to ask a hundred thousand times. */
    int indexAt (juce::Point<int>) const;

    /** The bottom of the row the labels are drawn in, measured from the top of
        the strip. What lies below is the safe margin; EditorShot checks how far
        that keeps the labels from the bottom of the window. */
    int getContentBottom() const noexcept
    {
        return contentHeight > 0 ? juce::jmin (contentHeight, getHeight()) : getHeight();
    }

private:
    static constexpr float gap = 5.0f;

    /** Where the label is drawn. */
    juce::Rectangle<float> areaForTab (int index) const;

    /** Where a click counts: the drawn rectangle everywhere except at the two
        ends, where the first and last tabs run out to the edges of the strip.

        A twelve-pixel margin at each end used to belong to no tab at all -
        clicks there did nothing, and nothing on screen said the strip had edges
        that were not part of it. The hit target may be larger than the thing
        drawn; it may never be smaller.

        Reserving room on the right for JUCE's resize grip was tried first and
        was worse, which the hit map showed and nothing else would have: the
        grip claims only a small triangle in the very corner, so holding the
        tabs off the whole 18-point column left 497 dead pixels where there had
        been 151 live ones. The grip keeps its triangle; the last tab gets the
        rest. */
    juce::Rectangle<float> hitAreaForTab (int index) const;

    juce::StringArray tabs;
    int selectedIndex = 0;
    int hoverIndex = -1;
    int contentHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabBar)
};
