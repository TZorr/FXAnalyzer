//
//  Theme.h
//  FX Analyzer
//
//  Every colour the panel uses, in one struct that is passed to every paint
//  method by reference.
//
//  Since 0.2 there is one design and it is fixed: the look of Kitbox and
//  Rackbox - a warm light-grey body, darker recessed sections, flat buttons
//  that light up orange, and near-black displays with orange type for every
//  graph and reading. The three user themes (Paper, Slate, Graphite), the
//  colour editor and theme import/export were removed with it. A session or a
//  .fxapreset saved before carries a <Theme> node and a themeName property;
//  both are simply not read any more.
//
//  The rule that made the themes possible is kept, because it is also what
//  keeps one design consistent: no juce::Colour literal outside this file, and
//  no LookAndFeel colour ids. Every paint method takes its colours from here by
//  name, so "the grid on a display" is one decision made once, not a hex value
//  copied into five pages that drift apart the first time one of them is
//  touched.
//
//  The values are Rackbox's Palette.h, unchanged, so the three plugins sit
//  side by side in a host as one family.
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct Theme
{
    //==============================================================================
    // The body: the light panel and the recessed sections on it.

    juce::Colour background  { 0xffd4d0c7 };   // the panel body
    juce::Colour section     { 0xffc7c2b8 };   // recessed sections (the Settings groups)
    juce::Colour text        { 0xff2e2b27 };   // ink: values and titles on the body
    juce::Colour label       { 0xff6f6a61 };   // names of controls on the body

    //==============================================================================
    // Controls.

    juce::Colour accent      { 0xfff28a1e };   // a lit button, the selected tab, a knob's set range
    juce::Colour onAccent    { 0xff2b2926 };   // type on a lit button
    juce::Colour button      { 0xffbdb7ac };
    juce::Colour buttonHover { 0xffafa89c };
    juce::Colour knobFace    { 0xffe6e2da };
    juce::Colour knobEdge    { 0xff9a948a };
    juce::Colour knobTrack   { 0xffaaa498 };

    //==============================================================================
    // Displays: the dark beds every graph and reading sits on.

    juce::Colour bed         { 0xff1d1a17 };
    juce::Colour grid        { 0xff342e29 };   // gridlines on a bed
    juce::Colour curve       { 0xffff8c2b };   // the spectrum, the scope trace, the bars
    juce::Colour curveAlt    { 0xffe9e5dd };   // second trace: right channel, side, peak hold
    juce::Colour bedText     { 0xffff8c2b };   // readouts on a bed
    juce::Colour bedDim      { 0xff8f5f33 };   // axis labels and captions on a bed
    juce::Colour bedButton   { 0xff3a3631 };   // a button that sits on a bed
    juce::Colour bedButtonHover { 0xff4a453f };
    juce::Colour warning     { 0xffe04a4f };   // over the true-peak ceiling, correlation below zero

    /** How strongly each derived colour is drawn, as a share of its base's
        alpha. Named rather than written inline, because each is a choice with a
        reason: the minor grid has to stay under the labelled one, and the fill
        has to leave the grid visible through it. */
    static constexpr float gridMinorAlpha = 0.6f;
    static constexpr float curveFillAlpha = 0.25f;
    static constexpr float accentDimAlpha = 0.4f;

    juce::Colour gridMinor() const { return grid.withMultipliedAlpha (gridMinorAlpha); }
    juce::Colour curveFill() const { return curve.withMultipliedAlpha (curveFillAlpha); }
    juce::Colour dimText()   const { return label; }
    juce::Colour accentDim() const { return accent.withMultipliedAlpha (accentDimAlpha); }

    //==============================================================================
    /** The panel's type: Kitbox's two faces. Names are set bold, upper case and
        tracked; everything that is a number is monospaced, so that readouts
        compared row against row and frame against frame line up - a
        proportional 8 and 1 make a level meter look like it is moving when only
        the digit shape changed. */
    float labelSize  = 9.5f;
    float axisSize   = 9.5f;
    float tabSize    = 9.5f;
    float valueSize  = 10.5f;

    juce::Font labelFont() const { return labelFontAt (labelSize); }
    juce::Font tabFont()   const { return labelFontAt (tabSize); }
    juce::Font axisFont()  const { return numberFont (axisSize); }
    juce::Font valueFont() const { return numberFont (valueSize); }

    static juce::Font labelFontAt (float height)
    {
        return juce::Font (juce::FontOptions (height, juce::Font::bold)).withExtraKerningFactor (0.08f);
    }

    static juce::Font numberFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height, bold ? juce::Font::bold : juce::Font::plain));
    }

    static juce::Font font (float height, int style)
    {
        return juce::Font (juce::FontOptions (height, style));
    }

    /** WCAG relative-luminance contrast between two opaque colours, 1.0 for
        identical and 21.0 for black on white.

        Here because "is the selected tab visible" turned out to be a question
        with a number behind it, and the number was once 1.00 - a theme whose
        active tab label was exactly as bright as the inactive ones. EditorShot's
        hit map holds the fixed palette to a floor with it. */
    static float contrastRatio (juce::Colour a, juce::Colour b);
};
