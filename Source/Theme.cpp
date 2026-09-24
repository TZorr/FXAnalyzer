//
//  Theme.cpp
//  FX Analyzer
//

#include "Theme.h"

#include <cmath>

namespace
{
    /** Name to member, once. Serialisation, deserialisation, JSON and the
        future editor's list all read this table, so adding a colour to the
        struct and to this array is the whole job - there is no fourth place
        that has to be remembered and therefore no fourth place to forget.
        Pointer-to-member rather than offsetof: it is type-checked, and a
        mistyped entry is a compile error instead of a colour written over the
        one next to it in memory. */
    struct ColourEntry { const char* name; juce::Colour Theme::* member; const char* description; };

    const ColourEntry colourTable[]
    {
        { "background", &Theme::background, "The panel, the header and every graph bed" },
        { "grid",       &Theme::grid,       "Gridlines and the panel frame; minor lines are drawn fainter" },
        { "curve",      &Theme::curve,      "The spectrum and the scope trace; the fill under it is derived" },
        { "curveAlt",   &Theme::curveAlt,   "Second trace: right channel, side, peak hold" },
        { "warning",    &Theme::warning,    "Over 0 dBTP, correlation below zero" },
        { "text",       &Theme::text,       "Readouts and the active tab; units are drawn dimmer" },
        { "accent",     &Theme::accent,     "Labels, arrows, inactive tabs; disabled arrows are dimmer" }
    };

    struct SizeEntry { const char* name; float Theme::* member; };

    const SizeEntry sizeTable[]
    {
        { "titleSize", &Theme::titleSize },
        { "labelSize", &Theme::labelSize },
        { "valueSize", &Theme::valueSize },
        { "tabSize",   &Theme::tabSize   },
        { "axisSize",  &Theme::axisSize  }
    };
}

//==============================================================================
juce::StringArray Theme::colourNames()
{
    juce::StringArray names;

    for (const auto& entry : colourTable)
        names.add (entry.name);

    return names;
}

int Theme::numColours()
{
    return (int) std::size (colourTable);
}

juce::String Theme::colourName (int index)
{
    return juce::isPositiveAndBelow (index, numColours()) ? colourTable[index].name : juce::String();
}

juce::String Theme::colourDescription (int index)
{
    return juce::isPositiveAndBelow (index, numColours()) ? colourTable[index].description : juce::String();
}

juce::Colour Theme::getColour (int index) const
{
    return juce::isPositiveAndBelow (index, numColours()) ? this->*colourTable[index].member
                                                          : juce::Colours::transparentBlack;
}

void Theme::setColour (int index, juce::Colour colour)
{
    if (juce::isPositiveAndBelow (index, numColours()))
        this->*colourTable[index].member = colour;
}

juce::ValueTree Theme::toValueTree() const
{
    juce::ValueTree tree ("Theme");
    tree.setProperty ("name", name, nullptr);

    // Stored as ARGB hex text rather than as an integer, because a session file
    // is occasionally read by a human and ff9adb4a says "green" to anyone who
    // has ever typed a colour, while -6636470 says nothing to anybody.
    for (const auto& entry : colourTable)
        tree.setProperty (entry.name, (this->*entry.member).toDisplayString (true), nullptr);

    for (const auto& entry : sizeTable)
        tree.setProperty (entry.name, this->*entry.member, nullptr);

    return tree;
}

Theme Theme::fromValueTree (const juce::ValueTree& tree)
{
    // Starting from the default rather than from black: a tree written by an
    // older build is missing whichever colours were added since, and inheriting
    // the default for those is the only reading that leaves a usable panel.
    Theme result = builtInThemes().front();

    if (! tree.isValid())
        return result;

    if (tree.hasProperty ("name"))
        result.name = tree["name"].toString();

    for (const auto& entry : colourTable)
        if (tree.hasProperty (entry.name))
            result.*entry.member = juce::Colour::fromString (tree[entry.name].toString());

    for (const auto& entry : sizeTable)
        if (tree.hasProperty (entry.name))
            result.*entry.member = (float) tree[entry.name];

    return result;
}

//==============================================================================
juce::String Theme::toJson() const
{
    auto* object = new juce::DynamicObject();

    object->setProperty ("name", name);

    for (const auto& entry : colourTable)
        object->setProperty (entry.name, (this->*entry.member).toDisplayString (true));

    for (const auto& entry : sizeTable)
        object->setProperty (entry.name, this->*entry.member);

    return juce::JSON::toString (juce::var (object), false);
}

bool Theme::fromJson (const juce::String& json, Theme& result)
{
    juce::var parsed;

    if (juce::JSON::parse (json, parsed).failed() || ! parsed.isObject())
        return false;

    auto* object = parsed.getDynamicObject();

    if (object == nullptr)
        return false;

    // A file that parses as JSON but names none of our colours is somebody
    // else's document, not a theme with a typo. Say so rather than silently
    // handing back the default wearing their filename.
    int recognised = 0;

    for (const auto& entry : colourTable)
        if (object->hasProperty (entry.name))
            ++recognised;

    if (recognised == 0)
        return false;

    Theme parsedTheme = builtInThemes().front();

    if (object->hasProperty ("name"))
        parsedTheme.name = object->getProperty ("name").toString();

    for (const auto& entry : colourTable)
        if (object->hasProperty (entry.name))
            parsedTheme.*entry.member = juce::Colour::fromString (object->getProperty (entry.name).toString());

    for (const auto& entry : sizeTable)
        if (object->hasProperty (entry.name))
            parsedTheme.*entry.member = (float) object->getProperty (entry.name);

    result = parsedTheme;
    return true;
}

//==============================================================================
float Theme::contrastRatio (juce::Colour a, juce::Colour b)
{
    const auto relativeLuminance = [] (juce::Colour c)
    {
        const auto channel = [] (float v)
        {
            return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f);
        };

        return 0.2126f * channel (c.getFloatRed())
             + 0.7152f * channel (c.getFloatGreen())
             + 0.0722f * channel (c.getFloatBlue());
    };

    const auto first  = relativeLuminance (a);
    const auto second = relativeLuminance (b);

    return (juce::jmax (first, second) + 0.05f) / (juce::jmin (first, second) + 0.05f);
}

//==============================================================================
const std::vector<Theme>& builtInThemes()
{
    static const std::vector<Theme> themes = []
    {
        std::vector<Theme> list;

        // Three themes, designed together on 2026-09-24 to replace the six
        // that had accumulated. Each writes out all seven colours - none leans
        // on the struct's defaults, for the reason given in Theme.h - and each
        // clears the tab strip's contrast floors, which EditorShot --hitmap
        // measures on the colours as composited rather than as stored.

        // Paper - the default since 2026-09-24, the theme chosen to ship
        // and the one the README screenshot shows. Light, for a room with a
        // window behind the desk and for screenshots in documents. Re-picked
        // rather than inverted: an inverted dark theme gives pastel data on
        // white, unreadable at the widths a spectrum is drawn with. The curve is
        // a deep blue at full alpha for the same reason, and the grid is black
        // at low alpha so it greys the paper rather than tinting it.
        list.push_back (Theme{});

        // Slate - the blue-grey arrived at in use (the old default.fxapreset
        // was a customised Yutani), kept as the family and tidied: one surface,
        // white type and curve carried at a little under full alpha so they sit
        // in the panel rather than on it, and a blue second trace that reads as
        // a relative of the first.
        Theme slate;
        slate.name       = "Slate";
        slate.background = juce::Colour (0xff274356);
        slate.grid       = juce::Colour (0x21ffffff);
        slate.curve      = juce::Colour (0x9effffff);
        slate.curveAlt   = juce::Colour (0xff62a8d6);
        slate.warning    = juce::Colour (0xffe8604a);
        slate.text       = juce::Colour (0xffe8ecef);
        slate.accent     = juce::Colour (0xc8ffffff);
        list.push_back (slate);

        // Graphite - for long sessions in a dark room. A near-black that is
        // still not black, so the grid has something to be lighter than; the
        // warmth is all in the data and the labels, amber, which the eye tires
        // of less than a saturated green or blue over hours.
        Theme graphite;
        graphite.name       = "Graphite";
        graphite.background = juce::Colour (0xff17191c);
        graphite.grid       = juce::Colour (0x24ffffff);
        graphite.curve      = juce::Colour (0xffe8a33d);
        graphite.curveAlt   = juce::Colour (0xff6cb4e8);
        graphite.warning    = juce::Colour (0xffff5a4a);
        graphite.text       = juce::Colour (0xffe6e6e3);
        graphite.accent     = juce::Colour (0xffd9a24e);
        list.push_back (graphite);

        return list;
    }();

    return themes;
}

bool parseColourText (const juce::String& text, juce::Colour& result)
{
    auto digits = text.trim();

    if (digits.startsWithChar ('#'))
        digits = digits.substring (1);

    if (digits.length() != 6 && digits.length() != 8)
        return false;

    if (! digits.containsOnly ("0123456789abcdefABCDEF"))
        return false;

    const auto value = (juce::uint32) digits.getHexValue64();

    result = digits.length() == 6 ? juce::Colour (0xff000000u | value)
                                  : juce::Colour (value);
    return true;
}

juce::String colourToText (juce::Colour colour)
{
    return "#" + colour.toDisplayString (true);
}

Theme themeByName (const juce::String& name)
{
    for (const auto& theme : builtInThemes())
        if (theme.name == name)
            return theme;

    return builtInThemes().front();
}
