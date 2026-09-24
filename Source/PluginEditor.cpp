//
//  PluginEditor.cpp
//  FX Analyzer
//

#include "PluginEditor.h"
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
      settingsPage (processorToUse)
{
    pages = { &spectrumPage, &scopePage, &loudnessPage, &stereoPage, &pitchPage, &settingsPage };

    addAndMakeVisible (header);
    addAndMakeVisible (tabs);

    for (auto* page : pages)
        addChildComponent (*page);

    header.setTitle ("FX Analyzer");
    header.onMenuClicked   = [this] { showMenu(); };
    header.onPresetClicked = [this] { showPresetMenu(); };

    tabs.setTabs (FXParams::pageNames);
    tabs.onChange = [this] (int index) { selectPage (index, "tab click"); };

    settingsPage.onThemeSelected = [this] { applyTheme(); };

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
    colourWindow.reset();
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
    // The theme is only half of it. This fires when a host restores a session,
    // and a restore changes every view setting the panel holds - which page is
    // open, the scale, the slope, the dB range. Repainting in the new colours
    // while still showing the old settings is worse than not reloading at all,
    // because it looks like the session loaded.
    applyTheme();

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
    theme = plugin.getTheme();

    header.setTheme (theme);
    tabs.setTheme (theme);

    for (auto* page : pages)
        page->setTheme (theme);

    // The colour editor is themed like everything else, which is what makes it
    // repaint itself while a colour is being dragged - it is showing the very
    // theme it is editing.
    if (auto* content = colourEditorContent())
        content->setTheme (theme);

    resized();
    repaint();
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

    // The hairline round the panel. It is the one piece of chrome here, and it
    // exists because a plugin window in a host has no frame of its own: without
    // it the panel bleeds into whatever grey the host paints behind it.
    g.setColour (theme.grid);
    g.drawRect (getLocalBounds(), 2);
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
}

//==============================================================================
bool FXAnalyzerEditor::keyPressed (const juce::KeyPress& key)
{
    const auto character = key.getTextCharacter();

    if (character >= '1' && character <= '6')
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
    juce::PopupMenu themeMenu;

    const auto& themes = builtInThemes();
    const auto currentName = theme.name;

    for (size_t i = 0; i < themes.size(); ++i)
        themeMenu.addItem ((int) i + 1, themes[i].name, true, themes[i].name == currentName);

    menu.addSubMenu ("Theme", themeMenu);
    menu.addItem (102, "Edit Colours...");
    menu.addItem (100, "Import Theme...");
    menu.addItem (101, "Export Theme...");
    menu.addSeparator();
    menu.addItem (200, "Reset Meters");
    menu.addItem (201, "Save Diagnostics...");
    menu.addSeparator();
    menu.addItem (300, "FX Analyzer " + juce::String (JucePlugin_VersionString), false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (header.getMenuScreenArea()),
                        [this] (int result)
    {
        if (result == 0)
            return;

        if (result == 100)      importTheme();
        else if (result == 101) exportTheme();
        else if (result == 102) showColourEditor();
        else if (result == 200) plugin.getAnalysis().resetMeters();
        else if (result == 201) saveDiagnostics();
        else if (result >= 1 && result <= (int) builtInThemes().size())
            plugin.setTheme (builtInThemes()[(size_t) result - 1]);
    });
}

namespace
{
    /** The window the colour editor lives in.

        A plain DocumentWindow with the editor as its content. The only thing
        worth reading here is the close path: a window may not delete itself
        from inside its own callback, so both the title bar's button and the
        editor's Done ask the plugin editor to do it on the next message, with a
        SafePointer in case the host tears everything down in between. */
    class ColourWindow final : public juce::DocumentWindow
    {
    public:
        ColourWindow (FXAnalyzerProcessor& processor, std::function<void()> closeRequested)
            : DocumentWindow ("FX Analyzer " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) + " Colours",
                              juce::Colours::black, DocumentWindow::closeButton),
              onCloseRequested (std::move (closeRequested))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new ColourEditor (processor), false);
            setResizable (true, false);
            setResizeLimits (520, 340, 1400, 900);

            // Above the plugin window, which is the whole point: you click into
            // the panel to see the effect, and this must not disappear behind
            // it while you do.
            setAlwaysOnTop (true);
            centreWithSize (640, 430);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            if (onCloseRequested != nullptr)
                onCloseRequested();
        }

    private:
        std::function<void()> onCloseRequested;
    };
}

ColourEditor* FXAnalyzerEditor::colourEditorContent() const
{
    return colourWindow != nullptr ? dynamic_cast<ColourEditor*> (colourWindow->getContentComponent())
                                   : nullptr;
}

void FXAnalyzerEditor::showColourEditor()
{
    if (colourWindow != nullptr)
    {
        colourWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<FXAnalyzerEditor> safe (this);

    const auto requestClose = [safe]
    {
        // Never synchronously: this is called from a click inside the window
        // that is about to be destroyed.
        juce::MessageManager::callAsync ([safe]
        {
            if (auto* editor = safe.getComponent())
                editor->closeColourEditor();
        });
    };

    colourWindow = std::make_unique<ColourWindow> (plugin, requestClose);

    if (auto* content = colourEditorContent())
    {
        content->onClose = requestClose;
        content->setTheme (theme);
    }
}

void FXAnalyzerEditor::closeColourEditor()
{
    // Destroyed rather than hidden. It holds the theme the panel had when it
    // opened, for Revert, and a hidden editor reopened an hour later would
    // offer to revert to a theme from an hour ago.
    colourWindow.reset();
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
            // "Default" restores the measurement settings and leaves the theme
            // alone. A colour scheme is a preference, not part of a preset, and
            // resetting somebody's chosen colours because they asked for the
            // default channel setting would be a surprise.
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

            chooser = std::make_unique<juce::FileChooser> (
                saving ? "Save FX Analyzer preset" : "Load FX Analyzer preset",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                "*.fxapreset");

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

                    // Deleted first. juce::File::createOutputStream appends to
                    // an existing file rather than truncating it, so saving over
                    // a preset twice leaves two presets in one file and the
                    // reader silently gets the first.
                    file.deleteFile();
                    file.replaceWithData (block.getData(), block.getSize());
                }
                else
                {
                    juce::MemoryBlock block;

                    if (file.loadFileAsData (block))
                    {
                        plugin.setStateInformation (block.getData(), (int) block.getSize());
                        applyTheme();

                        if (auto* page = currentPage())
                            page->pageShown();
                    }
                }
            });
        }
    });
}

//==============================================================================
void FXAnalyzerEditor::importTheme()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Import a theme",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.json");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        Theme imported;

        if (Theme::fromJson (file.loadFileAsString(), imported))
        {
            plugin.setTheme (imported);
            applyTheme();
        }
        else
        {
            // Refused rather than half-applied - see Theme::fromJson. Saying so
            // matters: a theme that silently did not load looks exactly like a
            // theme that loaded and happened to be the same colours.
            juce::NativeMessageBox::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle ("Import Theme")
                    .withMessage (file.getFileName() + " is not an FX Analyzer theme.")
                    .withButton ("OK"),
                nullptr);
        }
    });
}

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

void FXAnalyzerEditor::exportTheme()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Export the current theme",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile (theme.name + ".json"),
        "*.json");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        // See the preset save above: this has to truncate, and only
        // replaceWithText does.
        file.replaceWithText (theme.toJson());
    });
}
