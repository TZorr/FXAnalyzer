//
//  PresetFile.h
//  FX Analyzer (from Tonbox)
//
//  Presets on disk: the plugin's own file type (.tonbox, .fmbox, ...), plain
//  XML anyone can read in a text editor. The root names the plugin by its
//  four-letter code and carries the preset's name; inside is the state exactly
//  as getStateInformation writes it for the host, unwrapped from JUCE's binary
//  envelope:
//
//      <TZorrPreset plugin="Tnbx" product="Tonbox" formatVersion="1" name="VT-808">
//        <Tonbox ...>...</Tonbox>
//      </TZorrPreset>
//
//  Loading hands that inner element back to setStateInformation, so a preset
//  goes through the same code as a restored session - versioning included.
//
//  Kept in ~/Library/Audio/Presets/<company>/<plugin>/ and listed by the
//  plugin's preset menu (UI/PresetButton.h), the only preset menu a VST3 host
//  such as Bitwig has. Logic keeps its own .aupreset files and does not list
//  these.
//
//  The extension is TZORR_PRESET_EXTENSION from CMakeLists.txt, the identity
//  the JucePlugin_* definitions juce_add_plugin generates, so this file is the
//  same in every plugin of the family.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace PresetFile
{
    inline constexpr const char* extension = TZORR_PRESET_EXTENSION;
    constexpr int formatVersion = 1;

    /** JucePlugin_PluginCode as its four letters, "Tnbx". */
    juce::String pluginCode();

    /** The whole file, ready to write; empty if the state is not XML. */
    juce::String write (const juce::MemoryBlock& pluginState, const juce::String& presetName);

    struct Contents
    {
        juce::MemoryBlock pluginState;   // ready for setStateInformation
        juce::String name;
        juce::String error;              // empty on success

        bool ok() const noexcept { return error.isEmpty(); }
    };

    Contents read (const juce::String& fileText);
}
