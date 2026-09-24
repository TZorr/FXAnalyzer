//
//  ColourEditor.h
//  FX Analyzer
//
//  The panel's colours, laid out as swatches, each opening a picker.
//
//  This is the piece the theme system was built to make cheap. Every colour on
//  every page already goes through one Theme struct - no paint method names a
//  colour of its own - so this file needs no knowledge of what any colour is
//  used for and no list of its own. It enumerates Theme::numColours() and is
//  finished. A colour added to the struct appears here without this file being
//  touched, which is the whole return on the rule Theme.h argues for.
//
//  Editing is live rather than applied on OK. The colours exist to be looked at
//  against the data they colour, and a dialog that hides the graph while you
//  choose the graph's colour is a dialog you have to close, judge, and reopen.
//  Dragging in the picker repaints the panel underneath on every step.
//
//  Right-click a swatch to copy its colour or paste one onto it. The colour
//  travels through the system clipboard as "#AARRGGBB", so it carries between
//  swatches, between themes - pick Graphite, copy, pick Slate, paste - and to
//  and from any other program that deals in hex colours. A clipboard that does
//  not hold a colour leaves Paste disabled rather than guessing.
//
//  Editing a shipped theme renames it to "Custom". Otherwise a session would
//  claim to be using "Green Slate" while showing something else, and the next
//  person to select Green Slate from the menu would see the panel change and
//  have no way to explain why.
//

#pragma once

#include "PageBase.h"

class FXAnalyzerProcessor;

class ColourEditor : public ThemedComponent
{
public:
    explicit ColourEditor (FXAnalyzerProcessor&);
    ~ColourEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Called when the close control is used. The editor removes itself. */
    std::function<void()> onClose;

private:
    class Picker;

    void openPickerFor (int colourIndex, juce::Rectangle<int> swatchArea);
    void showCopyPasteMenu (int colourIndex);
    void applyColour (int colourIndex, juce::Colour);
    int  indexAt (juce::Point<int>) const;

    FXAnalyzerProcessor& plugin;

    /** The theme as it was when the editor opened, for Revert. Kept by value:
        the live theme is being edited underneath, so a reference would revert
        to whatever it had just been changed to. */
    Theme openingTheme;

    std::vector<juce::Rectangle<int>> swatchAreas;
    juce::Rectangle<int> closeArea, revertArea;

    int hoverIndex = -1;
    bool hoverClose = false, hoverRevert = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColourEditor)
};
