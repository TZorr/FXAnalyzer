//
//  ColourEditor.cpp
//  FX Analyzer
//

#include "ColourEditor.h"
#include "../PluginProcessor.h"
#include "../TextUtf8.h"

//==============================================================================
/** A ColourSelector that reports every intermediate colour rather than only the
    final one. Live editing is the point; a picker that spoke only on close
    would leave the user choosing a curve colour against a hidden curve. */
class ColourEditor::Picker : public juce::ColourSelector,
                             private juce::ChangeListener
{
public:
    Picker (juce::Colour initial, std::function<void (juce::Colour)> callback)
        : juce::ColourSelector (showColourspace | showSliders | showAlphaChannel | editableColour),
          onColourChanged (std::move (callback))
    {
        setCurrentColour (initial, juce::dontSendNotification);
        setSize (300, 340);
        addChangeListener (this);
    }

    ~Picker() override { removeChangeListener (this); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (onColourChanged != nullptr)
            onColourChanged (getCurrentColour());
    }

    std::function<void (juce::Colour)> onColourChanged;
};

//==============================================================================
ColourEditor::ColourEditor (FXAnalyzerProcessor& processor) : plugin (processor)
{
    openingTheme = plugin.getTheme();
    swatchAreas.resize ((size_t) Theme::numColours());
    setInterceptsMouseClicks (true, false);
}

ColourEditor::~ColourEditor() = default;

//==============================================================================
void ColourEditor::resized()
{
    auto area = getLocalBounds().reduced (18, 14);

    auto bar = area.removeFromTop (34);
    closeArea  = bar.removeFromRight (90);
    bar.removeFromRight (10);
    revertArea = bar.removeFromRight (90);

    area.removeFromTop (8);

    // Two columns. Seven colours in four rows fit without scrolling - a colour
    // editor that scrolls hides half the palette from the eye trying to
    // balance it - and two columns rather than three leave each description
    // the width to say what is derived from its colour.
    area.removeFromTop (18);   // the hint line under the title
    constexpr int columns = 2;
    const auto rows = (Theme::numColours() + columns - 1) / columns;

    const auto cellWidth  = area.getWidth() / columns;
    const auto cellHeight = area.getHeight() / juce::jmax (1, rows);

    for (int i = 0; i < Theme::numColours(); ++i)
    {
        const auto row = i / columns;
        const auto col = i % columns;

        swatchAreas[(size_t) i] = juce::Rectangle<int> (area.getX() + col * cellWidth,
                                                        area.getY() + row * cellHeight,
                                                        cellWidth, cellHeight).reduced (6, 4);
    }
}

//==============================================================================
void ColourEditor::paint (juce::Graphics& g)
{
    const auto& t = theme();

    g.setColour (t.background);
    g.fillRect (getLocalBounds());

    g.setColour (t.accent);
    g.setFont (t.labelFont());
    g.drawText ("COLOURS", getLocalBounds().reduced (20, 16).removeFromTop (24),
                juce::Justification::centredLeft, false);

    const auto drawButton = [&] (juce::Rectangle<int> area, const juce::String& text, bool hovering)
    {
        g.setColour (hovering ? t.accent : t.accentDim());
        g.drawRoundedRectangle (area.toFloat().reduced (1.0f), 4.0f, 1.6f);

        g.setColour (hovering ? t.text : t.accent);
        g.setFont (t.labelFont());
        g.drawText (text, area, juce::Justification::centred, false);
    };

    g.setColour (t.dimText());
    g.setFont (t.font (t.labelSize - 1.0f, juce::Font::plain));
    g.drawText (utf8 ("Click a colour to edit it  \u00b7  right-click to copy or paste"),
                getLocalBounds().reduced (20, 16).withTrimmedTop (38).removeFromTop (18),
                juce::Justification::centredLeft, false);

    drawButton (revertArea, "Revert", hoverRevert);
    drawButton (closeArea,  "Done",   hoverClose);

    for (int i = 0; i < Theme::numColours(); ++i)
    {
        auto cell = swatchAreas[(size_t) i];

        if (cell.isEmpty())
            continue;

        const auto swatch = cell.removeFromLeft (juce::jmin (54, cell.getWidth() / 3));

        // A chequerboard behind the swatch, because two of these colours are
        // deliberately translucent and a translucent swatch drawn on the panel
        // background is indistinguishable from an opaque darker one.
        const auto square = 7;

        for (int y = swatch.getY(); y < swatch.getBottom(); y += square)
            for (int x = swatch.getX(); x < swatch.getRight(); x += square)
            {
                const auto light = ((x / square) + (y / square)) % 2 == 0;
                g.setColour (light ? t.background.brighter (0.25f) : t.background.darker (0.25f));
                g.fillRect (juce::Rectangle<int> (x, y, square, square).getIntersection (swatch));
            }

        g.setColour (theme().getColour (i));
        g.fillRect (swatch);

        g.setColour (i == hoverIndex ? t.text : t.grid);
        g.drawRect (swatch, i == hoverIndex ? 2 : 1);

        auto textArea = cell.reduced (8, 0);

        g.setColour (t.text);
        g.setFont (t.labelFont());
        g.drawText (Theme::colourName (i) + "   " + colourToText (theme().getColour (i)),
                    textArea.removeFromTop (textArea.getHeight() / 2),
                    juce::Justification::bottomLeft, false);

        g.setColour (t.dimText());
        g.setFont (t.font (t.labelSize - 2.0f, juce::Font::plain));
        g.drawFittedText (Theme::colourDescription (i), textArea,
                          juce::Justification::topLeft, 2);
    }
}

//==============================================================================
int ColourEditor::indexAt (juce::Point<int> position) const
{
    for (int i = 0; i < (int) swatchAreas.size(); ++i)
        if (swatchAreas[(size_t) i].contains (position))
            return i;

    return -1;
}

void ColourEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (closeArea.contains (position))
    {
        if (onClose != nullptr)
            onClose();

        return;
    }

    if (revertArea.contains (position))
    {
        plugin.setTheme (openingTheme);
        return;
    }

    const auto index = indexAt (position);

    if (index < 0)
        return;

    if (event.mods.isPopupMenu())
        showCopyPasteMenu (index);
    else
        openPickerFor (index, swatchAreas[(size_t) index]);
}

void ColourEditor::mouseMove (const juce::MouseEvent& event)
{
    const auto position = event.getPosition();
    const auto index = indexAt (position);
    const auto close = closeArea.contains (position);
    const auto revert = revertArea.contains (position);

    if (index != hoverIndex || close != hoverClose || revert != hoverRevert)
    {
        hoverIndex  = index;
        hoverClose  = close;
        hoverRevert = revert;
        repaint();
    }
}

void ColourEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex >= 0 || hoverClose || hoverRevert)
    {
        hoverIndex = -1;
        hoverClose = hoverRevert = false;
        repaint();
    }
}

//==============================================================================
void ColourEditor::openPickerFor (int colourIndex, juce::Rectangle<int> swatchArea)
{
    // A SafePointer rather than a captured this: the call-out box outlives this
    // component if the editor is closed while the picker is open, and a plugin
    // editor can be torn down by the host at any moment.
    juce::Component::SafePointer<ColourEditor> safe (this);

    auto picker = std::make_unique<Picker> (theme().getColour (colourIndex),
                                            [safe, colourIndex] (juce::Colour colour)
    {
        if (auto* editor = safe.getComponent())
            editor->applyColour (colourIndex, colour);
    });

    juce::CallOutBox::launchAsynchronously (std::move (picker),
                                            getScreenBounds().getIntersection (
                                                swatchArea.translated (getScreenX(), getScreenY())),
                                            nullptr);
}

void ColourEditor::showCopyPasteMenu (int colourIndex)
{
    const auto current = theme().getColour (colourIndex);

    juce::Colour pasted;
    const auto canPaste = parseColourText (juce::SystemClipboard::getTextFromClipboard(), pasted);

    juce::PopupMenu menu;
    menu.addItem (1, "Copy " + colourToText (current));
    menu.addItem (2, canPaste ? "Paste " + colourToText (pasted) : juce::String ("Paste"), canPaste);

    // Async, like every other menu on the panel: a modal loop stops the
    // editor's timer in some hosts, and the display would freeze while the
    // menu is open. The clipboard is read again on selection because it can
    // change while the menu is up.
    juce::Component::SafePointer<ColourEditor> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                   .withMousePosition(),
                        [safe, colourIndex, current] (int result)
    {
        auto* editor = safe.getComponent();

        if (editor == nullptr || result == 0)
            return;

        if (result == 1)
        {
            juce::SystemClipboard::copyTextToClipboard (colourToText (current));
            return;
        }

        juce::Colour colour;

        if (parseColourText (juce::SystemClipboard::getTextFromClipboard(), colour))
            editor->applyColour (colourIndex, colour);
    });
}

void ColourEditor::applyColour (int colourIndex, juce::Colour colour)
{
    auto edited = plugin.getTheme();
    edited.setColour (colourIndex, colour);

    // A shipped theme that has been edited is no longer that theme. Renaming it
    // is what stops a session claiming "Green Slate" while showing something
    // else - and what stops the next person who picks Green Slate from the menu
    // watching the panel change with no explanation.
    for (const auto& builtIn : builtInThemes())
    {
        if (edited.name == builtIn.name)
        {
            bool identical = true;

            for (int i = 0; i < Theme::numColours(); ++i)
                if (edited.getColour (i) != builtIn.getColour (i))
                    identical = false;

            if (! identical)
                edited.name = "Custom";

            break;
        }
    }

    plugin.setTheme (edited);
}
