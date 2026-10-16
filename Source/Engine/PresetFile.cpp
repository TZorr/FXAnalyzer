//
//  PresetFile.cpp
//  FX Analyzer (from Tonbox)
//

#include "PresetFile.h"

namespace
{
    const juce::Identifier rootTag { "TZorrPreset" };
}

juce::String PresetFile::pluginCode()
{
    const auto code = (juce::uint32) JucePlugin_PluginCode;
    juce::String letters;

    for (int shift = 24; shift >= 0; shift -= 8)
        letters << (juce::juce_wchar) ((code >> shift) & 0xff);

    return letters;
}

juce::String PresetFile::write (const juce::MemoryBlock& pluginState, const juce::String& presetName)
{
    auto state = juce::AudioProcessor::getXmlFromBinary (pluginState.getData(), (int) pluginState.getSize());

    if (state == nullptr)
        return {};

    juce::XmlElement root (rootTag);
    root.setAttribute ("plugin", pluginCode());
    root.setAttribute ("product", JucePlugin_Name);
    root.setAttribute ("formatVersion", formatVersion);
    root.setAttribute ("name", presetName);
    root.addChildElement (state.release());

    return root.toString();
}

PresetFile::Contents PresetFile::read (const juce::String& fileText)
{
    Contents contents;

    const auto root = juce::XmlDocument::parse (fileText);

    if (root == nullptr || ! root->hasTagName (rootTag.toString()))
    {
        contents.error = "Not a " JucePlugin_Name " preset.";
        return contents;
    }

    if (root->getStringAttribute ("plugin") != pluginCode())
    {
        contents.error = "This preset belongs to another plugin.";
        return contents;
    }

    if (root->getIntAttribute ("formatVersion") > formatVersion)
    {
        contents.error = "This preset was saved by a newer version of " JucePlugin_Name ".";
        return contents;
    }

    const auto* state = root->getFirstChildElement();

    if (state == nullptr)
    {
        contents.error = "The preset holds no settings.";
        return contents;
    }

    contents.name = root->getStringAttribute ("name");
    juce::AudioProcessor::copyXmlToBinary (*state, contents.pluginState);
    return contents;
}
