//
//  Theme.h
//  FX Analyzer
//
//  Every colour the panel uses, in one struct that is passed to every paint
//  method by reference.
//
//  This looks like over-engineering for a plugin that ships with one colour
//  scheme, and it is not. The requirement is that the colours become editable
//  later, and the difference between "editable later" and "rewritten later" is
//  decided now, by whether any paint method is allowed to name a colour of its
//  own. None is. A literal 0xff9adb4a anywhere in the UI is a colour the future
//  editor cannot reach, and it will not announce itself - the panel simply keeps
//  one stubbornly green element after the user has picked amber.
//
//  So: no juce::Colour literals outside this file, and no LookAndFeel colour
//  ids either. A LookAndFeel would have been the JUCE-idiomatic route and was
//  rejected because its colour ids are per-widget-class globals; two different
//  graphs that both want "the curve colour" end up borrowing an id that means
//  something else, and the mapping from id to meaning lives nowhere.
//
//  Themes serialise two ways. Into the plugin's state as a ValueTree, so a
//  session reopens looking the way it was left; and to and from JSON, so a
//  theme can be shared as a file. The JSON route goes through a FileChooser
//  rather than a fixed folder - originally because the AUv3 build was sandboxed
//  and the chooser was the only door that opened there, and still, now that the
//  AUv3 is gone, because a host may be sandboxed even when the plugin is not.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

struct Theme
{
    /** The defaults are Paper, the theme the plugin ships set to (since
        2026-09-24; Slate before that). Keeping the
        struct's own defaults equal to the default theme is what makes a
        default-constructed Theme - which every ThemedComponent holds until it
        is handed one - the same thing the panel is about to become, rather than
        a flash of some other palette.

        The other side of that arrangement is the trap: a built-in theme which
        leaves a colour unset inherits it from here, so changing the default
        theme silently restyles every theme that was relying on it. The old Ice
        and Amber did exactly that. Every built-in theme writes out all seven
        colours for that reason - see builtInThemes. */
    juce::String name { "Paper" };

    /** Seven colours, down from fifteen on 2026-09-24.

        The fifteen were never fifteen decisions. Three were the same surface
        (background, panel, header - Yutani drew all three in one blue at three
        alphas), two were the same line at two strengths, and four were a colour
        with its alpha turned down: the curve fill, the dim text, the disabled
        arrow, the minor grid. One was drawn by nothing at all. A palette with
        fifteen swatches asks fifteen questions, and eight of them only have one
        sensible answer - which is how a theme ends up with a minor grid brighter
        than its major one.

        So only the colours that carry a meaning of their own are stored, and
        the rest are derived from them by the functions below. A derived colour
        cannot drift from its base because it is not stored anywhere to drift.
        The names are the old names, so a theme exported before the change, or
        saved in a session, still loads: its extra keys are simply not read. */

    juce::Colour background { 0xfff3f2ee };   // the panel, the header, the graph beds
    juce::Colour grid       { 0x26000000 };   // gridlines and the frame round the panel
    juce::Colour curve      { 0xff2a64a8 };   // the spectrum, the scope trace
    juce::Colour curveAlt   { 0xffd06a24 };   // second trace: right channel, side, peak hold
    juce::Colour warning    { 0xffc43a2a };   // over 0 dBTP, correlation below zero
    juce::Colour text       { 0xff1f2328 };   // readouts and the active tab
    juce::Colour accent     { 0xff2a64a8 };   // labels, arrows, inactive tabs

    /** How strongly each derived colour is drawn, as a share of its base's
        alpha. Named rather than written inline, because each is a choice with a
        reason: the minor grid has to stay under the labelled one, the fill has
        to leave the grid visible through it, dim text has to be legibly quieter
        than text and still clear the tab strip's contrast floor, and a disabled
        arrow has to look absent rather than faint. */
    static constexpr float gridMinorAlpha = 0.6f;
    static constexpr float curveFillAlpha = 0.45f;
    static constexpr float dimTextAlpha   = 0.62f;
    static constexpr float accentDimAlpha = 0.4f;

    juce::Colour gridMinor() const { return grid.withMultipliedAlpha (gridMinorAlpha); }
    juce::Colour curveFill() const { return curve.withMultipliedAlpha (curveFillAlpha); }
    juce::Colour dimText()   const { return text.withMultipliedAlpha (dimTextAlpha); }
    juce::Colour accentDim() const { return accent.withMultipliedAlpha (accentDimAlpha); }

    //==============================================================================
    /** The panel's type. Sizes live with the colours because the future editor
        will be asked for a bigger font long before it is asked for a new grey,
        and a theme that cannot answer that is only half a theme. */
    float titleSize  = 25.0f;
    float labelSize  = 13.0f;
    float valueSize  = 21.0f;
    float tabSize    = 15.0f;
    float axisSize   = 12.0f;

    juce::Font titleFont() const { return font (titleSize, juce::Font::plain); }
    juce::Font labelFont() const { return font (labelSize, juce::Font::plain); }
    juce::Font valueFont() const { return font (valueSize, juce::Font::plain); }
    juce::Font tabFont()   const { return font (tabSize,   juce::Font::plain); }
    juce::Font axisFont()  const { return font (axisSize,  juce::Font::plain); }

    /** Readouts are compared row against row and frame against frame, so their
        digits have to line up; a proportional 8 and 1 make a level meter look
        like it is moving when only the digit shape changed. */
    juce::Font numberFont (float height) const
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height, juce::Font::plain));
    }

    /** WCAG relative-luminance contrast between two opaque colours, 1.0 for
        identical and 21.0 for black on white.

        Here because "is the selected tab visible" turned out to be a question
        with a number behind it, and the number was 1.00 - in Onyx (one of the
        themes before 2026-09-24) the active
        tab label was *exactly* as bright as the inactive ones, so the strip
        showed nothing at all about where you were. That is not something an
        eye catches while designing a palette one swatch at a time, and it is
        not something an assertion can catch either unless the assertion knows
        how to measure contrast. */
    static float contrastRatio (juce::Colour a, juce::Colour b);

    static juce::Font font (float height, int style)
    {
        return juce::Font (juce::FontOptions (height, style));
    }

    //==============================================================================
    juce::ValueTree toValueTree() const;
    static Theme fromValueTree (const juce::ValueTree&);

    juce::String toJson() const;
    /** Returns false and leaves the theme untouched if the text is not a theme.
        A half-applied theme is worse than a rejected one. */
    static bool fromJson (const juce::String& json, Theme& result);

    /** Names of every colour, in the order the editor should show them. Kept
        next to the struct so a new colour is added in one place and appears in
        the editor, the ValueTree and the JSON at once. */
    static juce::StringArray colourNames();

    //==============================================================================
    /** Indexed access, which is what the colour editor needs and what keeps it
        from having to know a single colour by name. A colour added to the
        struct and to the table in Theme.cpp appears in the editor by itself;
        there is no third list to keep in step. */
    static int numColours();
    static juce::String colourName (int index);

    juce::Colour getColour (int index) const;
    void setColour (int index, juce::Colour);

    /** A one-line description of what the colour is for, shown under its name.
        Swatches called "curveAlt" and "grid" are a puzzle; the same swatches
        with a sentence each - including what is derived from them - are a
        control panel. */
    static juce::String colourDescription (int index);
};

/** Reads a colour typed or pasted as text: "#AARRGGBB", "AARRGGBB", "#RRGGBB"
    or "RRGGBB", surrounding space ignored, six digits meaning opaque.

    Here rather than in the colour editor so AnalyzerCheck can hold it to its
    contract without a window. It returns false and leaves `result` alone for
    anything else - juce::Colour::fromString reads any junk as *some* colour,
    usually transparent black, and pasting a sentence from a mail should not
    make the curve disappear. */
bool parseColourText (const juce::String& text, juce::Colour& result);

/** "#AARRGGBB", the form parseColourText reads back. */
juce::String colourToText (juce::Colour);

/** The themes that ship with the plugin. The first is the default and is the
    one the screenshots were drawn from. */
const std::vector<Theme>& builtInThemes();

/** Looks a theme up by name, falling back to the first built-in one. Used when
    a session names a theme this build does not have. */
Theme themeByName (const juce::String& name);
