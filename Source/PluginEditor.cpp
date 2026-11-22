//
//  PluginEditor.cpp
//  FX Analyzer
//

#include "PluginEditor.h"
#include "Engine/PresetFile.h"
#include "TextUtf8.h"

// StandalonePluginHolder lives in this header and nowhere else. Including it
// from the shared code is safe - it declares two classes and defines no
// application entry point - and it is the only supported way to reach the
// standalone's own audio settings from inside the plugin.
//
// The two module headers above it are not optional and not tidiness: that
// header names AudioDeviceManager, AudioProcessorPlayer and
// AudioDeviceSelectorComponent without including anything itself, so on its own
// it fails to compile with twenty errors that all point at JUCE and none at the
// missing include.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
    /** The panel's theme is the user's, not the session's: it lives in
        ~/Library/Application Support/FX Analyzer/FX Analyzer.settings, shared
        by every instance and every project, and never travels with a session
        or a .fxapreset - as on Kitbox. */
    std::unique_ptr<juce::PropertiesFile> openSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = JucePlugin_Name;
        options.filenameSuffix      = ".settings";
        options.folderName          = JucePlugin_Name;
        options.osxLibrarySubFolder = "Application Support";
        options.getDefaultFile().getParentDirectory().createDirectory();
        return std::make_unique<juce::PropertiesFile> (options);
    }

    constexpr const char* darkModeKey  = "darkMode";
    constexpr const char* colourwayKey = "colourway";   // Theme::Colourway as an int

    Theme::Colourway colourwayFromInt (int value)
    {
        return (Theme::Colourway) juce::jlimit (0, Theme::numColourways - 1, value);
    }
}

//==============================================================================
// The parameter is not called `processor`: AudioProcessorEditor already has a
// member of that name, and a constructor parameter shadowing it means the
// initialiser list reads as if it were assigning the base's member to itself.
FXAnalyzerEditor::FXAnalyzerEditor (FXAnalyzerProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse),
      plugin (processorToUse),
      spectrumPage (processorToUse),
      scopePage (processorToUse),
      loudnessPage (processorToUse),
      stereoPage (processorToUse),
      pitchPage (processorToUse),
      targetPage (processorToUse),
      settingsPage (processorToUse)
{
    pages = { &spectrumPage, &scopePage, &loudnessPage, &stereoPage, &pitchPage, &targetPage, &settingsPage };

    addAndMakeVisible (header);
    addAndMakeVisible (tabs);

    for (auto* page : pages)
        addChildComponent (*page);

    header.onMenuClicked   = [this] { showMenu(); };
    header.onPresetClicked = [this] { showPresetMenu(); };
    header.onDarkClicked   = [this] { setDarkMode (! Theme::darkPanel); };

    tabs.setTabs (FXParams::pageNames);
    tabs.onChange = [this] (int index) { selectPage (index, "tab click"); };

    // A tooltip is something to read, never something to click. JUCE's
    // TooltipWindow is always on top and does not turn off mouse interception
    // itself, so while one is showing it takes the click meant for whatever is
    // underneath. Nothing in this panel can currently place one over a control
    // - the geometry was checked - but that is a property of today's layout,
    // not a rule, and this makes it a rule.
    tooltips.setInterceptsMouseClicks (false, false);

    // Every mouse event anywhere in the editor, with the component that
    // received it. The nested flag is the whole point: without it this would
    // only hear the events that reached the editor itself, which are exactly
    // the ones that are not interesting.
    addMouseListener (this, true);

    plugin.addChangeListener (this);

    // The saved theme first, so the first paint is already the right one.
    {
        auto settings = openSettings();
        Theme::darkPanel = settings->getBoolValue (darkModeKey, false);
        Theme::colourway = colourwayFromInt (settings->getIntValue (colourwayKey, 0));
    }
    shownDark = Theme::darkPanel;
    shownColourway = Theme::colourway;
    theme = Theme::make (shownColourway, shownDark);
    header.setDarkLit (shownDark);

    applyTheme();
    unmuteStandaloneInput();

    // A fixed aspect ratio, not free resizing. Every page is laid out as a
    // graph plus a fixed-width column of controls, and letting the window get
    // tall and narrow leaves that column occupying half the panel with a
    // letterbox beside it. The alternative - reflowing the controls under the
    // graph below some width - is a second layout to design and maintain for a
    // shape nobody wants.
    setResizable (true, true);

    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio ((double) Layout::defaultWidth / (double) Layout::defaultHeight);
        constrainer->setSizeLimits (Layout::defaultWidth * 3 / 4, Layout::defaultHeight * 3 / 4,
                                    Layout::defaultWidth * 2,     Layout::defaultHeight * 2);
    }

    setSize (Layout::defaultWidth, Layout::defaultHeight);

    selectPage (juce::jlimit (0, FXParams::numPages - 1,
                              (int) plugin.getViewProperty (FXParams::propPage, 0)), "editor opened");

    // Deliberately not asking for keyboard focus - see the note in
    // CMakeLists.txt. keyPressed below is kept and simply never fires; it is
    // the other half of a shortcut set that costs a host freeze to have.
    setWantsKeyboardFocus (false);
    setRefreshRateHz (FXParams::displayFramesPerSecond);
}

FXAnalyzerEditor::~FXAnalyzerEditor()
{
    // Both before anything else is torn down. The timer fires on the message
    // thread and so does this destructor, so they cannot interleave - but the
    // Timer base class only stops itself after every member here has already
    // been destroyed, and a listener registration outliving its listener is the
    // whole class of bug this change is about.
    stopTimer();
    plugin.removeChangeListener (this);
}

void FXAnalyzerEditor::record (const juce::String& what)
{
    diagnostics[(size_t) (diagnosticsWritten % diagnosticCapacity)] =
        juce::String (juce::Time::getMillisecondCounter()) + " ms  f" + juce::String (frameCount)
            + " p" + juce::String (paintCount) + "  " + what;

    ++diagnosticsWritten;
}

juce::StringArray FXAnalyzerEditor::getDiagnostics() const
{
    juce::StringArray lines;

    // Oldest first. Until the ring has wrapped, the entries before the write
    // position are the whole history and the rest are empty.
    const auto total = juce::jmin (diagnosticsWritten, diagnosticCapacity);
    const auto first = diagnosticsWritten > diagnosticCapacity
                           ? diagnosticsWritten % diagnosticCapacity : 0;

    for (int i = 0; i < total; ++i)
        lines.add (diagnostics[(size_t) ((first + i) % diagnosticCapacity)]);

    return lines;
}

void FXAnalyzerEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto inEditor = event.getEventRelativeTo (this).getPosition();

    auto* target = event.eventComponent;

    const auto name = target == nullptr ? juce::String ("none")
                    : target == this    ? juce::String ("editor")
                    : target == &tabs   ? juce::String ("tab strip")
                                        : (target->getName().isNotEmpty() ? target->getName()
                                                                          : juce::String (typeid (*target).name()));

    // The index the strip would compute, whether or not the strip got the
    // click. If the click landed elsewhere while the pointer was over the
    // strip, that difference is the fault in one line.
    const auto overStrip = tabs.getBounds().contains (inEditor);
    const auto index = overStrip ? tabs.indexAt (inEditor - tabs.getPosition()) : -1;

    record ("down x" + juce::String (event.getNumberOfClicks())
                + " at " + inEditor.toString() + " -> " + name
                + (overStrip ? "  strip would say tab " + juce::String (index) : juce::String())
                + "  page " + juce::String (getVisiblePageIndex()));
}

void FXAnalyzerEditor::mouseUp (const juce::MouseEvent& event)
{
    const auto inEditor = event.getEventRelativeTo (this).getPosition();

    record ("up   at " + inEditor.toString()
                + "  page " + juce::String (getVisiblePageIndex()));
}

void FXAnalyzerEditor::mouseMove (const juce::MouseEvent& event)
{
    const auto inEditor = event.getEventRelativeTo (this).getPosition();
    const auto onStrip  = tabs.getBounds().contains (inEditor);

    if (onStrip == pointerOnStrip)
        return;

    pointerOnStrip = onStrip;

    record (juce::String (onStrip ? "enter strip at " : "leave strip at ") + inEditor.toString());
}

void FXAnalyzerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Always the message thread, whatever thread the change came from.
    //
    // This fires when a host restores a session, and a restore changes every
    // view setting the panel holds - which page is open, the scale, the slope,
    // the dB range.
    selectPage ((int) plugin.getViewProperty (FXParams::propPage, 0), "host state");

    if (auto* page = currentPage())
        page->pageShown();
}

//==============================================================================
void FXAnalyzerEditor::unmuteStandaloneInput()
{
    // JUCE mutes the standalone's input by default, and it is right to: a
    // plugin with an input and an output running on a laptop is a microphone
    // wired to the speakers. That reasoning stops applying the moment the
    // output is silent, which is what PluginProcessor::processBlock arranges
    // for this wrapper - so the mute is lifted here rather than left for the
    // user to find in a settings dialog they have no reason to open.
    //
    // Lifted once, and not held down. The toggle stays where JUCE put it, in
    // Options -> Audio Settings, and whatever the user sets there is what
    // persists: this changes the default, it does not overrule a decision.
    if (plugin.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
        return;

    auto* holder = juce::StandalonePluginHolder::getInstance();

    if (holder == nullptr)
        return;

    if (plugin.getViewProperty (FXParams::propStandaloneInputChosen, false))
        return;

    holder->getMuteInputValue().setValue (false);
    plugin.setViewProperty (FXParams::propStandaloneInputChosen, true);
}

void FXAnalyzerEditor::applyTheme()
{
    // One design since 0.2, light or dark since 0.2.1, handed down whole.
    // ThemedComponent passes it on to every child, so no widget can hold a
    // colour of its own - and a switch reaches every one of them.
    header.setTheme (theme);
    tabs.setTheme (theme);

    for (auto* page : pages)
        page->setTheme (theme);

    resized();
    repaint();
}

void FXAnalyzerEditor::setDarkMode (bool shouldBeDark)
{
    Theme::darkPanel = shouldBeDark;

    if (auto settings = openSettings())
    {
        settings->setValue (darkModeKey, shouldBeDark);
        settings->saveIfNeeded();
    }

    record (shouldBeDark ? "panel dark" : "panel light");

    shownDark = shouldBeDark;
    theme = Theme::make (shownColourway, shouldBeDark);
    header.setDarkLit (shouldBeDark);
    applyTheme();
}

void FXAnalyzerEditor::setColourway (Theme::Colourway newColourway)
{
    Theme::colourway = newColourway;

    if (auto settings = openSettings())
    {
        settings->setValue (colourwayKey, (int) newColourway);
        settings->saveIfNeeded();
    }

    record (juce::String ("colourway ") + Theme::colourwayName (newColourway));

    shownColourway = newColourway;
    theme = Theme::make (newColourway, shownDark);
    applyTheme();
}

void FXAnalyzerEditor::setRefreshRateHz (int hz)
{
    startTimerHz (juce::jlimit (10, 120, hz));
}

int FXAnalyzerEditor::getVisiblePageIndex() const
{
    return tabs.getSelectedIndex();
}

PageBase* FXAnalyzerEditor::currentPage() const
{
    const auto index = juce::jlimit (0, FXParams::numPages - 1, tabs.getSelectedIndex());
    return pages[(size_t) index];
}

void FXAnalyzerEditor::selectPage (int index)
{
    selectPage (index, "api");
}

void FXAnalyzerEditor::selectPage (int index, const char* reason)
{
    const auto clamped = juce::jlimit (0, FXParams::numPages - 1, index);

    // Every call, not only the ones that change something. A host re-applying
    // its state onto the page that is already open leaves no trace in the
    // result and is exactly the thing worth seeing.
    record ("page " + juce::String (lastPage) + " -> " + juce::String (clamped)
                + "  (" + reason + ")");

    lastPage = clamped;

    tabs.setSelectedIndex (clamped, juce::dontSendNotification);
    plugin.setViewProperty (FXParams::propPage, clamped);

    // What each page is for, in one line under its name in the header display.
    static const char* const descriptions[FXParams::numPages]
    {
        "Level over frequency. Every setting is on the Settings page.",
        "The waveform over time, left and right as two traces.",
        "EBU R128 loudness to BS.1770-4, and true peak.",
        "Goniometer, correlation, balance and width.",
        "The note being played, and how far off it is.",
        "How far the spectrum is from Clearbox's 4.5 dB/oct curve, per octave.",
        "Every display setting, over a live preview of the spectrum."
    };

    header.setPage (FXParams::pageNames[clamped], descriptions[clamped]);

    for (int i = 0; i < FXParams::numPages; ++i)
        pages[(size_t) i]->setVisible (i == clamped);

    resized();

    if (auto* page = currentPage())
    {
        page->pageShown();
        page->refresh();
    }

    repaint();
}

//==============================================================================
void FXAnalyzerEditor::paint (juce::Graphics& g)
{
    ++paintCount;

    g.fillAll (theme.background);

    // The footer line, in the band along the bottom edge where Logic does not
    // reliably deliver a click (Layout::tabBarSafeBottom): a place nothing can
    // be operated in is a place for text that is only read.
    // The claims on the left, between small dots; the two keys on the right
    // drawn as keycaps.
    const auto font = Theme::capsFontAt (8.0f);
    auto line = footerArea.toFloat();

    {
        const auto keyFont = Theme::numberFont (7.5f, true);
        const auto key = [&] (const juce::String& cap, const juce::String& word)
        {
            const auto wordWidth = juce::GlyphArrangement::getStringWidth (font, word) + 2.0f;
            g.setColour (theme.label);
            g.setFont (font);
            g.drawText (word, line.removeFromRight (wordWidth), juce::Justification::centredRight, false);

            line.removeFromRight (5.0f);
            const auto box = line.removeFromRight (11.0f).withSizeKeepingCentre (11.0f, 12.0f);
            g.drawRoundedRectangle (box.reduced (0.5f), 2.0f, 1.0f);
            g.setFont (keyFont);
            g.drawText (cap, box, juce::Justification::centred, false);
            line.removeFromRight (10.0f);
        };

        key ("R", "RESET");
        key ("F", "FREEZE");
    }

    g.setFont (font);
    g.setColour (theme.text.withAlpha (0.8f));

    const juce::StringArray claims { juce::String ("FX ANALYZER " JucePlugin_VersionString),
                                     "NEVER WRITES TO THE SIGNAL",
                                     "INPUT GAIN SCALES ONLY WHAT IS MEASURED" };

    for (int i = 0; i < claims.size() && line.getWidth() > 0.0f; ++i)
    {
        if (i > 0)
        {
            line.removeFromLeft (10.0f);
            g.setColour (theme.label.withMultipliedAlpha (0.6f));
            g.fillEllipse (line.removeFromLeft (3.0f).withSizeKeepingCentre (3.0f, 3.0f));
            line.removeFromLeft (10.0f);
            g.setColour (theme.label);
        }

        const auto width = juce::GlyphArrangement::getStringWidth (font, claims[i]) + 2.0f;

        // A claim that does not fit whole is left out rather than cut short.
        if (width > line.getWidth())
            break;

        g.drawText (claims[i], line.removeFromLeft (width), juce::Justification::centredLeft, false);
    }
}

void FXAnalyzerEditor::resized()
{
    auto area = getLocalBounds().reduced (2);

    // The header and tab strip keep their proportion of the window rather than
    // a fixed pixel height, so that scaling the panel scales the type with it.
    const auto scale = (float) getHeight() / (float) Layout::defaultHeight;

    header.setBounds (area.removeFromTop (juce::roundToInt (Layout::headerHeight * scale)));

    // The strip is taller than the row it shows, and the extra is at the
    // bottom, against the window edge, where Logic does not reliably deliver a
    // click - see Layout::tabBarSafeBottom. The labels are laid out above it.
    // Not scaled with the panel, unlike everything else here. The band the host
    // swallows is a fixed number of screen points at the window's edge; it does
    // not get smaller because the user made the window smaller - which is
    // exactly when the margin matters most.
    const auto safeBottom  = Layout::tabBarSafeBottom;
    const auto stripHeight = juce::roundToInt (Layout::tabBarHeight * scale) + safeBottom;

    tabs.setBounds (area.removeFromBottom (stripHeight));
    tabs.setContentHeight (stripHeight - safeBottom);

    // The footer's line, 14 points from the window's left (2 inset + 12) and
    // clear of the resize grip on the right.
    footerArea = tabs.getBounds().removeFromBottom (safeBottom).withTrimmedLeft (12).withTrimmedRight (22)
                     .withTrimmedBottom (4);


    for (auto* page : pages)
        page->setBounds (area);
}

//==============================================================================
void FXAnalyzerEditor::timerCallback()
{
    advanceOneFrame();
}

void FXAnalyzerEditor::advanceOneFrame()
{
    ++frameCount;

    // Another FX Analyzer window may have switched the theme: it is process-wide.
    if (shownDark != Theme::darkPanel || shownColourway != Theme::colourway)
    {
        shownDark = Theme::darkPanel;
        shownColourway = Theme::colourway;
        theme = Theme::make (shownColourway, shownDark);
        header.setDarkLit (shownDark);
        applyTheme();
    }

    plugin.getAnalysis().updateDisplays();

    if (auto* page = currentPage())
        page->refresh();

    // Input level, still measured and still handed to the header, though the
    // lamp that displayed it has been removed. Kept because the cost is one
    // atomic read per frame and the value is what anything indicating presence
    // would need next.
    const auto& stereo = plugin.getAnalysis().getStereo();
    const auto loudest = juce::jmax (stereo.getRmsLeftDb(), stereo.getRmsRightDb());

    header.setActivity (juce::jlimit (0.0f, 1.0f, (loudest + 60.0f) / 60.0f));

    const auto channel = juce::jlimit (0, FXParams::channelNames.size() - 1,
                                       plugin.getChoiceParameter (FXParams::channel));

    header.setStatus ({ juce::String (plugin.getAnalysis().getSampleRate() / 1000.0, 1) + " kHz",
                        FXParams::channelNames[channel].removeCharacters (" ").toUpperCase() },
                      plugin.getBoolParameter (FXParams::freeze));
}

//==============================================================================
bool FXAnalyzerEditor::keyPressed (const juce::KeyPress& key)
{
    const auto character = key.getTextCharacter();

    if (character >= '1' && character < '1' + FXParams::numPages)
    {
        selectPage (character - '1', "key");
        return true;
    }

    if (character == 'f' || character == 'F')
    {
        plugin.setBoolParameter (FXParams::freeze, ! plugin.getBoolParameter (FXParams::freeze));
        return true;
    }

    if (character == 'r' || character == 'R')
    {
        plugin.getAnalysis().resetMeters();
        return true;
    }

    return false;
}

//==============================================================================
void FXAnalyzerEditor::showMenu()
{
    juce::PopupMenu menu;

    // The panel's look: the colour family, and light or dark (the same switch
    // as the Dark button). Saved for the user, not the session.
    juce::PopupMenu colours;

    for (int i = 0; i < Theme::numColourways; ++i)
    {
        const auto c = (Theme::Colourway) i;
        colours.addItem (400 + i, Theme::colourwayName (c), true, c == shownColourway);
    }

    menu.addSectionHeader ("Panel");
    menu.addSubMenu ("Colour", colours);
    menu.addItem (210, "Dark Panel", true, shownDark);
    menu.addSeparator();
    menu.addSectionHeader ("Meters");
    menu.addItem (200, "Reset Meters  (R)");
    menu.addItem (211, "Freeze  (F)", true, plugin.getBoolParameter (FXParams::freeze));
    menu.addSeparator();
    menu.addItem (201, "Save Diagnostics...");
    menu.addItem (300, "FX Analyzer " + juce::String (JucePlugin_VersionString), false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (header.getMenuScreenArea()),
                        [this] (int result)
    {
        if (result == 200)      plugin.getAnalysis().resetMeters();
        else if (result == 201) saveDiagnostics();
        else if (result == 210) setDarkMode (! Theme::darkPanel);
        else if (result == 211) plugin.setBoolParameter (FXParams::freeze, ! plugin.getBoolParameter (FXParams::freeze));
        else if (result >= 400 && result < 400 + Theme::numColourways)
            setColourway ((Theme::Colourway) (result - 400));
    });
}

void FXAnalyzerEditor::showPresetMenu()
{
    juce::PopupMenu menu;

    menu.addItem (1, "Default");
    menu.addSeparator();
    menu.addItem (2, "Save Preset...");
    menu.addItem (3, "Load Preset...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (header.getPresetScreenArea()),
                        [this] (int result)
    {
        if (result == 1)
        {
            // "Default" restores the measurement settings.
            plugin.setFloatParameter (FXParams::inputGainDb, 0.0f);
            plugin.setChoiceParameter (FXParams::channel, (int) FXParams::Channel::leftPlusRight);
            plugin.setChoiceParameter (FXParams::reactivity, (int) FXParams::Reactivity::medium);
            plugin.setBoolParameter (FXParams::dcBlock, true);
            plugin.setBoolParameter (FXParams::freeze, false);

            if (auto* page = currentPage())
                page->pageShown();
        }
        else if (result == 2 || result == 3)
        {
            const auto saving = result == 2;

            // Beside the family's other presets: ~/Library/Audio/Presets/T'Zorr/FX Analyzer.
            const auto folder = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                    .getChildFile ("Library/Audio/Presets")
                                    .getChildFile (JucePlugin_Manufacturer)
                                    .getChildFile (JucePlugin_Name);
            folder.createDirectory();

            chooser = std::make_unique<juce::FileChooser> (
                saving ? "Save FX Analyzer preset" : "Load FX Analyzer preset",
                folder, juce::String ("*") + PresetFile::extension);

            const auto flags = saving ? (juce::FileBrowserComponent::saveMode
                                             | juce::FileBrowserComponent::warnAboutOverwriting)
                                      : (juce::FileBrowserComponent::openMode
                                             | juce::FileBrowserComponent::canSelectFiles);

            chooser->launchAsync (flags, [this, saving] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();

                if (file == juce::File())
                    return;

                if (saving)
                {
                    juce::MemoryBlock block;
                    plugin.getStateInformation (block);

                    // Readable XML (Engine/PresetFile.h). replaceWithText writes
                    // a temporary file and moves it into place; createOutputStream
                    // would append to a preset already there.
                    const auto target = file.withFileExtension (PresetFile::extension);
                    const auto text = PresetFile::write (block, target.getFileNameWithoutExtension());

                    if (text.isEmpty() || ! target.replaceWithText (text, false, false, "\n"))
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Preset",
                                                                "Could not write " + target.getFullPathName());
                }
                else
                {
                    const auto preset = PresetFile::read (file.loadFileAsString());

                    if (! preset.ok())
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load Preset", preset.error);
                        return;
                    }

                    plugin.setStateInformation (preset.pluginState.getData(), (int) preset.pluginState.getSize());

                    if (auto* page = currentPage())
                        page->pageShown();
                }
            });
        }
    });
}

//==============================================================================
void FXAnalyzerEditor::saveDiagnostics()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Save what the panel has been asked to do",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("FX Analyzer diagnostics.txt"),
        "*.txt");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        juce::StringArray lines;

        lines.add ("FX Analyzer " + juce::String (JucePlugin_VersionString)
                       + "  " + juce::String (juce::Time::getCurrentTime().toString (true, true)));
        lines.add ("host: " + juce::String (juce::PluginHostType().getHostDescription()));
        lines.add ("editor: " + juce::String (getWidth()) + " x " + juce::String (getHeight())
                       + "   tab strip: " + tabs.getBounds().toString());
        lines.add ("");

        lines.addArray (getDiagnostics());

        // Truncated first: createOutputStream opens an existing file at its end,
        // so saving twice over one name would otherwise append the second run
        // to the first and leave a file that reads as one impossible session.
        file.deleteFile();
        file.replaceWithText (lines.joinIntoString ("\n") + "\n");
    });
}
