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
//  Since 0.2.1 the same panel also comes dark (Theme::make), with Kitbox's
//  dark values: the layout, the orange and the traces stay, the greys turn
//  round. The choice is the user's, not the session's (see PluginEditor.cpp).
//
//  Since 0.4.0 there are four colourways - Orange (the original), Blue, Mint
//  and Red - each light and dark, chosen under MENU > Colour and saved like
//  Dark. The values are in Theme.cpp, one row per panel; the light orange one
//  is still the defaults below. Two of the defaults moved for contrast at
//  9.5 px: label (3.5:1 -> 4.7:1 on the body) and bedDim (3.1:1 -> 4.7:1 on a
//  bed). EditorShot holds all eight panels to the same floors.
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
    juce::Colour label       { 0xff5a5650 };   // names of controls on the body

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
    juce::Colour bedDim      { 0xffb37c46 };   // axis labels and captions on a bed
    juce::Colour bedButton   { 0xff3a3631 };   // a button that sits on a bed
    juce::Colour bedButtonHover { 0xff4a453f };
    juce::Colour warning     { 0xffe04a4f };   // over the true-peak ceiling, correlation below zero
    juce::Colour below       { 0xff4fb3d1 };   // TARGET: under the curve (too quiet), as on Clearbox

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

    /** The light and shade that make the panel read as an object (Surfaces.h).
        Derived, not stored: a dark body needs far less light on its edges and
        far more shade under them than a light one, and that follows from the
        body's own brightness for every panel. */
    bool isDarkBody() const { return background.getPerceivedBrightness() < 0.5f; }
    juce::Colour bevelLight()   const { return juce::Colours::white.withAlpha (isDarkBody() ? 0.05f : 0.45f); }
    juce::Colour bevelShade()   const { return juce::Colours::black.withAlpha (isDarkBody() ? 0.55f : 0.16f); }
    juce::Colour innerShadow()  const { return juce::Colours::black.withAlpha (0.45f); }
    juce::Colour bedHighlight() const { return juce::Colours::white.withAlpha (0.06f); }
    juce::Colour litHighlight() const { return juce::Colours::white.withAlpha (0.35f); }
    juce::Colour accentGlow()   const { return accent.withAlpha (0.35f); }

    /** Grid steps on a bed: the decades (100 Hz, 1 kHz, 10 kHz) and the 0 dB
        line over the labelled lines over the rest. */
    juce::Colour gridMajor() const { return grid.brighter (0.3f); }

    //==============================================================================
    /** The colourways (since 0.4.0). Each comes light and dark; the Dark
        button picks which, the Colour submenu of MENU picks the family. The
        order is the order of the menu and of the saved setting - append, never
        reorder. Orange is the original Kitbox/Rackbox panel. */
    enum class Colourway { orange, blue, mint, red };
    static constexpr int numColourways = 4;

    static const char* colourwayName (Colourway c)
    {
        switch (c)
        {
            case Colourway::blue: return "Blue";
            case Colourway::mint: return "Mint";
            case Colourway::red:  return "Red";
            case Colourway::orange:
            default:              return "Orange";
        }
    }

    /** One of the eight panels: a colourway, light or dark. */
    static Theme make (Colourway colourway, bool dark);

    /** The original orange panel, light or dark. */
    static Theme make (bool dark) { return make (Colourway::orange, dark); }

    /** Which panel every open FX Analyzer window shows. Process-wide, as on
        Kitbox: a switch in one window reaches the others on their next
        frame. */
    static inline bool darkPanel = false;
    static inline Colourway colourway = Colourway::orange;

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
    juce::Font tabFont()   const { return capsFontAt (tabSize); }
    juce::Font axisFont()  const { return numberFont (axisSize); }
    juce::Font valueFont() const { return numberFont (valueSize); }

    static juce::Font labelFontAt (float height)
    {
        return juce::Font (juce::FontOptions (height, juce::Font::bold)).withExtraKerningFactor (0.08f);
    }

    /** The design's small capitals (0.4.0): bold and tracked wide, 0.14 em -
        the tabs, the header's buttons and small print, the buttons on a bed. */
    static juce::Font capsFontAt (float height)
    {
        return juce::Font (juce::FontOptions (height, juce::Font::bold)).withExtraKerningFactor (0.14f);
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
