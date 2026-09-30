# ==============================================================================
#
#  FX Analyzer
#
#  A measurement plugin: spectrum, oscilloscope, loudness, stereo field and
#  pitch, on one panel with one set of controls behind them.
#
#  It never touches the audio. The buffer that arrives is the buffer that
#  leaves, sample for sample, and Input Gain scales only what the analysis sees.
#  That is not a detail - an analyzer sits on the master bus of a session that
#  is being mixed against it, and a meter that quietly changes the thing it is
#  measuring is worse than no meter. AnalyzerCheck asserts the null.
#
#  Three build products come out of this file:
#
#    AU        the classic v2 component. The deliverable for Logic and every
#              other macOS host.
#    VST3      the cross-host deliverable.
#    Standalone  the development build. Developing a plugin through a DAW means
#              fighting the AU component cache on every rebuild.
#
#  There was an AUv3 here, and removing it was not a simplification - it was the
#  fix for a deadlock that froze Logic every time the plugin was dragged to
#  another slot. Captured from a real beachball, the ring is:
#
#    Logic       -[AUAudioUnit_XPC supportedViewConfigurations:]  waits on us
#    appex XPC   JUCE MessageManager::callSync                    waits on our main thread
#    appex main  viewDidDisappear -> nextValidKeyView
#                  -> HIRunLoopSemaphore wait                     waits on Logic
#
#  An AUv3's view lives in a separate view service, so tearing it down makes
#  AppKit ask the host a synchronous question - while JUCE is synchronously
#  waiting on our message thread to answer the host's. None of the three points
#  is reachable from plugin code: the callSync is unconditional in JUCE 9.0.1
#  (juce_audio_plugin_client_AUv3.mm), and the AppKit walk belongs to the view
#  service. An AU v2 has no view service and therefore no such path.
#
#  The samples are kept next to this file as logic-hang.txt and appex-hang.txt.
#  Anyone tempted to add the AUv3 back should read them first, and should also
#  know that it cannot share aufx/Fxan/<manufacturer> with the AU: built together the AUv3
#  wins registration and the component silently never appears.
#
#  Two console targets do the verifying. AnalyzerCheck measures the DSP against
#  signals whose answer is known from the standard or from arithmetic, not
#  against this project's own output. EditorShot renders each page to a PNG, so
#  the layout can be looked at without launching a host.
#
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(FXAnalyzer VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon only, matching the other plugin bundles on this machine.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "26.5" CACHE STRING "" FORCE)

# JUCE lives outside the project tree rather than as a submodule, because it is
# shared with every other plugin on this machine.
set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to the JUCE framework")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Pass -DJUCE_PATH=/path/to/JUCE.")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

# ------------------------------------------------------------------------------
# The plugin
# ------------------------------------------------------------------------------

juce_add_plugin(FXAnalyzer
    # The vendor a host lists this under. Logic reads the AudioComponents "name"
    # key and splits it at ": ", so this string alone decides whether the plugin
    # appears under T'Zorr or under someone else - and VST3 takes its vendor
    # from the same place.
    COMPANY_NAME                "T'Zorr"
    BUNDLE_ID                   TZorr.FXAnalyzer
    # Changed from NePy to Clde on 2026-08-29, and from Clde to Tzor on
    # 2026-09-24 for the first public release, alongside AB Reference, which
    # made the same move - both deliberately and with the cost known. This code is not displayed anywhere - it is the identity: the AU is
    # aufx/Fxan/<this>, and the VST3's class id is a hash of exactly this and
    # PLUGIN_CODE (VST3Interface::jucePluginId). So this *is* a different plugin
    # now, and any instance saved by an earlier build comes back missing.
    #
    # An earlier version of this comment argued for keeping NePy on exactly that
    # compatibility ground. It was overruled because nothing has shipped, so the
    # sessions being protected were only local test projects - and NePy was an
    # abbreviation of a name that has nothing to do with these plugins, which
    # left the old identity buried in the one field nobody looks at. The same
    # reasoning applied again before publishing: the sessions at risk are local
    # ones, and a public plugin should carry its author's code from day one.
    PLUGIN_MANUFACTURER_CODE    Tzor
    PLUGIN_CODE                 Fxan
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "FX Analyzer"
    DESCRIPTION                 "Spectrum, scope, loudness, stereo and pitch analysis"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    # Back on. It was turned off chasing the AUv3 deadlock, on the theory that
    # the editor's place in AppKit's key-view loop was what dragged the view
    # service into a synchronous host query. It was not - the flag is read only
    # by the VST2 and AU v2 wrappers and never by the AUv3 one, so the change
    # could not have had any effect on the thing it was aimed at.
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE     # the page keys 1-6 and F for freeze
    COPY_PLUGIN_AFTER_BUILD     FALSE    # Scripts/install.sh does this, cache flush and all
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect
    # The standalone takes live audio, so it needs the microphone entitlement
    # and the usage string - and they are two separate things, which is the
    # trap: MICROPHONE_PERMISSION_* writes only the Info.plist string, so the
    # build looks configured and the app still cannot record. The entitlement
    # comes from HARDENED_RUNTIME_OPTIONS.
    #
    # The app sandbox was here only because an AUv3 extension must be sandboxed.
    # With the AUv3 gone it buys nothing and costs file access, so it is gone
    # too.
    HARDENED_RUNTIME_ENABLED    TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "FX Analyzer needs an audio input to measure. Choose the device carrying the signal you want to analyse - usually a loopback device, not the microphone."
    HARDENED_RUNTIME_OPTIONS    com.apple.security.device.audio-input
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

# The analysis sources are listed separately so the verification target links
# exactly the objects the plugin runs. Testing a reimplementation tests nothing.
set(FXANALYZER_CORE_SOURCES
    Source/LoudnessMeter.cpp
    Source/TruePeakMeter.cpp
    Source/SpectrumAnalyser.cpp
    Source/Oscilloscope.cpp
    Source/StereoAnalyser.cpp
    Source/PitchDetector.cpp
    Source/AnalysisHub.cpp)

set(FXANALYZER_UI_SOURCES
    Source/Theme.cpp
    Source/UI/HeaderBar.cpp
    Source/UI/TabBar.cpp
    Source/UI/StepperControl.cpp
    Source/UI/IconButton.cpp
    Source/UI/KnobControl.cpp
    Source/UI/GraphAxes.cpp
    Source/UI/SpectrumPage.cpp
    Source/UI/ScopePage.cpp
    Source/UI/LoudnessPage.cpp
    Source/UI/StereoPage.cpp
    Source/UI/PitchPage.cpp
    Source/UI/SettingsPage.cpp)

target_sources(FXAnalyzer PRIVATE
    ${FXANALYZER_CORE_SOURCES}
    ${FXANALYZER_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(FXAnalyzer PRIVATE Source)

target_compile_definitions(FXAnalyzer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    # The standalone build must not open an input device on launch. This plugin
    # is fed by the host, and a live microphone in front of the monitors during
    # a measurement is a feedback loop waiting to happen.
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

target_link_libraries(FXAnalyzer PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# AnalyzerCheck - the measurement harness
# ------------------------------------------------------------------------------

juce_add_console_app(AnalyzerCheck PRODUCT_NAME "AnalyzerCheck")

target_sources(AnalyzerCheck PRIVATE
    Verification/AnalyzerCheck.cpp
    ${FXANALYZER_CORE_SOURCES})

target_include_directories(AnalyzerCheck PRIVATE Source)

target_compile_definitions(AnalyzerCheck PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(AnalyzerCheck PRIVATE
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_events
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# EditorShot - renders each page to a PNG without needing a screen
# ------------------------------------------------------------------------------

juce_add_console_app(EditorShot PRODUCT_NAME "EditorShot")

target_sources(EditorShot PRIVATE Verification/EditorShot.cpp)

target_compile_definitions(EditorShot PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1)

# Linking the plugin's own shared-code library rather than recompiling its
# sources: that library already carries the JucePlugin_* definitions that
# juce_add_plugin generates, and a second copy compiled without them would be a
# different plugin wearing the same name.
target_link_libraries(EditorShot PRIVATE
    FXAnalyzer
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_include_directories(EditorShot PRIVATE Source)

# Both sides have an opinion about JUCE_STANDALONE_APPLICATION and the console
# app's is the one that should win. Suppressing a warning is usually the wrong
# answer; here the redefinition is the intended arrangement.
target_compile_options(EditorShot PRIVATE -Wno-macro-redefined)

# ------------------------------------------------------------------------------
# HostNullTest - the null test against the *installed* component
# ------------------------------------------------------------------------------
#
# It links no project source on purpose. AnalyzerCheck and EditorShot test the
# code; this one loads aufx/Fxan/Tzor the way a host does, renders through it and
# compares the output to the input bit pattern for bit pattern. Between
# processBlock and the host lie JUCE's AU wrapper, the layout negotiation and the
# render callback, and none of those are covered by testing our own objects.
#
# Run from Scripts/install.sh, because it needs the component installed.

add_executable(HostNullTest Verification/HostNullTest.cpp)

target_link_libraries(HostNullTest PRIVATE
    "-framework AudioToolbox"
    "-framework CoreFoundation")

set_target_properties(HostNullTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/HostNullTest_artefacts/Release")
