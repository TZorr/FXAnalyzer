//
//  PluginProcessor.cpp
//  FX Analyzer
//

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FXAnalyzerProcessor::makeParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { FXParams::inputGainDb, FXParams::stateVersion },
        "Input Gain",
        NormalisableRange<float> (FXParams::inputGainMinDb, FXParams::inputGainMaxDb, 0.1f),
        0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { FXParams::channel, FXParams::stateVersion },
        "Channel",
        FXParams::channelNames,
        (int) FXParams::Channel::leftPlusRight));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { FXParams::reactivity, FXParams::stateVersion },
        "Reactivity",
        FXParams::reactivityNames,
        (int) FXParams::Reactivity::verySlow));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { FXParams::dcBlock, FXParams::stateVersion },
        "DC Block",
        true));

    // Freeze is a parameter rather than view state because it is the one thing
    // on this panel somebody might genuinely want on a footswitch: hold the
    // display at the moment the problem happened, with both hands still on the
    // desk.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { FXParams::freeze, FXParams::stateVersion },
        "Freeze",
        false));

    return layout;
}

//==============================================================================
FXAnalyzerProcessor::FXAnalyzerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier (FXParams::stateTreeType), makeParameterLayout())
{
    inputGainParam  = parameters.getRawParameterValue (FXParams::inputGainDb);
    channelParam    = parameters.getRawParameterValue (FXParams::channel);
    reactivityParam = parameters.getRawParameterValue (FXParams::reactivity);
    dcBlockParam    = parameters.getRawParameterValue (FXParams::dcBlock);
    freezeParam     = parameters.getRawParameterValue (FXParams::freeze);

    parameters.state.setProperty (FXParams::propStateVersion, FXParams::stateVersion, nullptr);
}

FXAnalyzerProcessor::~FXAnalyzerProcessor() = default;

//==============================================================================
void FXAnalyzerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analysis.prepare (sampleRate, samplesPerBlock);

    applyDisplaySettings();
    pullParameters();
}

void FXAnalyzerProcessor::releaseResources() {}

bool FXAnalyzerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    // Mono and stereo only, and in must equal out. An analyzer that changed the
    // channel count would be doing something to the signal, which is the one
    // thing this plugin promises not to do.
    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void FXAnalyzerProcessor::pullParameters() noexcept
{
    analysis.setInputGainDb (inputGainParam->load());
    analysis.setChannel ((FXParams::Channel) (int) channelParam->load());
    analysis.setReactivity ((FXParams::Reactivity) (int) reactivityParam->load());
    analysis.setDcBlockEnabled (dcBlockParam->load() > 0.5f);
    analysis.setFrozen (freezeParam->load() > 0.5f);
}

void FXAnalyzerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    pullParameters();

    // Read only. There is deliberately no getWritePointer here, and no
    // output-clearing loop either: with in and out being the same bus and the
    // same channel count, JUCE has already handed us the input in the output
    // buffer, and the correct thing to do with it is nothing.
    analysis.pushBlock (buffer);

    // The standalone is the single exception, and it is not a hole in the
    // transparency promise - it is the promise applied to a different
    // situation. Transparency matters because a plugin sits in a chain that
    // something downstream is listening to. The standalone *is* the end of the
    // chain: nothing is downstream but the speakers, and an analyzer that plays
    // your input back at them is not a feature, it is the acoustic path that
    // makes JUCE mute the input in the first place.
    //
    // Silencing the output here is what makes it safe to unmute the input, and
    // unmuting the input is what makes the standalone an analyzer rather than a
    // picture of one. Every other wrapper - AU and VST3 - takes the branch
    // above and never has a write pointer taken to its buffer at all.
    if (wrapperType == wrapperType_Standalone)
        buffer.clear();
}

//==============================================================================
juce::AudioProcessorEditor* FXAnalyzerProcessor::createEditor()
{
    return new FXAnalyzerEditor (*this);
}

void FXAnalyzerProcessor::applyDisplaySettings()
{
    // The view-state settings that the analysis side has to be told about.
    // They are not parameters, so nothing pushes them automatically; both the
    // places a session can arrive from - prepareToPlay and setStateInformation -
    // have to run this, and having one function is what stops the second of
    // those from being forgotten. It was: restoring a session used to apply the
    // FFT size and silently leave the smoothing at its default.
    analysis.setFftOrder ((int) getViewProperty (FXParams::propFftOrder,
                                                 FXParams::fftOrderValues[FXParams::defaultFftOrderIndex]));

    const auto smoothing = juce::jlimit (0, FXParams::spectrumSmoothingNames.size() - 1,
                                         (int) getViewProperty (FXParams::propSpectrumSmoothing,
                                                                FXParams::defaultSpectrumSmoothingIndex));

    analysis.setSpectrumSmoothing (FXParams::spectrumSmoothingFractions[smoothing]);

    const auto attack = juce::jlimit (0, FXParams::spectrumAttackNames.size() - 1,
                                      (int) getViewProperty (FXParams::propSpectrumAttack,
                                                             FXParams::defaultSpectrumAttackIndex));

    analysis.setSpectrumAttackSeconds (FXParams::spectrumAttackValues[attack]);

    const auto release = juce::jlimit (0, FXParams::spectrumReleaseNames.size() - 1,
                                       (int) getViewProperty (FXParams::propSpectrumRelease,
                                                              FXParams::defaultSpectrumReleaseIndex));

    analysis.setSpectrumReleaseSeconds (FXParams::spectrumReleaseValues[release]);

    const auto resolution = juce::jlimit (0, FXParams::spectrumResolutionNames.size() - 1,
                                          (int) getViewProperty (FXParams::propSpectrumResolution,
                                                                 FXParams::defaultSpectrumResolutionIndex));

    // Falling back to one tier when there is no smoothing is the honest answer -
    // see FXParams::multiResolutionAvailable, which is also what the Spectrum
    // page asks before telling the user why.
    const auto canSplice = FXParams::multiResolutionAvailable (smoothing);

    analysis.setSpectrumResolution (canSplice && resolution != 0
                                        ? FXParams::SpectrumResolution::multi
                                        : FXParams::SpectrumResolution::single);
}

//==============================================================================
void FXAnalyzerProcessor::setFloatParameter (const char* id, float plainValue)
{
    if (auto* parameter = parameters.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

float FXAnalyzerProcessor::getFloatParameter (const char* id) const
{
    if (auto* value = parameters.getRawParameterValue (id))
        return value->load();

    return 0.0f;
}

void FXAnalyzerProcessor::setChoiceParameter (const char* id, int index)
{
    if (auto* parameter = parameters.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
}

int FXAnalyzerProcessor::getChoiceParameter (const char* id) const
{
    return (int) getFloatParameter (id);
}

void FXAnalyzerProcessor::setBoolParameter (const char* id, bool state)
{
    if (auto* parameter = parameters.getParameter (id))
        parameter->setValueNotifyingHost (state ? 1.0f : 0.0f);
}

bool FXAnalyzerProcessor::getBoolParameter (const char* id) const
{
    return getFloatParameter (id) > 0.5f;
}

void FXAnalyzerProcessor::beginGesture (const char* id)
{
    if (auto* parameter = parameters.getParameter (id))
        parameter->beginChangeGesture();
}

void FXAnalyzerProcessor::endGesture (const char* id)
{
    if (auto* parameter = parameters.getParameter (id))
        parameter->endChangeGesture();
}

//==============================================================================
juce::var FXAnalyzerProcessor::getViewProperty (const juce::Identifier& id, const juce::var& defaultValue) const
{
    return parameters.state.getProperty (id, defaultValue);
}

float FXAnalyzerProcessor::getSpectrumTiltDb() const
{
    const auto& state = parameters.state;

    // The new property wins whenever it is there. The legacy index is only a
    // fallback, and it is left in the tree rather than deleted: an older build
    // opening a newer session then still finds a slope it understands, the
    // nearest of its four.
    if (state.hasProperty (FXParams::propSpectrumTiltDb))
        return juce::jlimit (FXParams::spectrumTiltMinDb, FXParams::spectrumTiltMaxDb,
                             (float) state.getProperty (FXParams::propSpectrumTiltDb));

    if (state.hasProperty (FXParams::propSpectrumSlope))
        return FXParams::spectrumTiltFromLegacyIndex ((int) state.getProperty (FXParams::propSpectrumSlope));

    return FXParams::defaultSpectrumTiltDb();
}

void FXAnalyzerProcessor::setSpectrumTiltDb (float dbPerOctave)
{
    const auto clamped = juce::jlimit (FXParams::spectrumTiltMinDb, FXParams::spectrumTiltMaxDb, dbPerOctave);

    setViewProperty (FXParams::propSpectrumTiltDb, clamped);

    // Keep the legacy index pointing at the nearest of the old four, for the
    // older build described above. Nothing in this build reads it once the
    // new property exists.
    int nearest = 0;

    for (int i = 1; i < (int) std::size (FXParams::spectrumSlopeValues); ++i)
        if (std::abs (FXParams::spectrumSlopeValues[i] - clamped)
                < std::abs (FXParams::spectrumSlopeValues[nearest] - clamped))
            nearest = i;

    setViewProperty (FXParams::propSpectrumSlope, nearest);
}

float FXAnalyzerProcessor::getSpectrumTiltPivotHz() const
{
    const auto index = juce::jlimit (0, FXParams::spectrumTiltPivotNames.size() - 1,
                                     (int) getViewProperty (FXParams::propSpectrumTiltPivot,
                                                            FXParams::defaultSpectrumTiltPivotIndex));

    return FXParams::spectrumTiltPivotValues[index];
}

void FXAnalyzerProcessor::setViewProperty (const juce::Identifier& id, const juce::var& value)
{
    parameters.state.setProperty (id, value, nullptr);
}

//==============================================================================
void FXAnalyzerProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    parameters.state.setProperty (FXParams::propStateVersion, FXParams::stateVersion, nullptr);

    // copyState(), not state.createXml().
    //
    // They look interchangeable and are not. A parameter's live value lives in
    // the parameter object; the ValueTree gets it only when the APVTS flushes,
    // which it does on its own timer. Serialising the tree directly therefore
    // saves whatever the last flush happened to leave there - so a session saved
    // shortly after a knob was moved comes back with the old value, and the bug
    // depends on timing, which is the worst kind to be told about. copyState()
    // flushes first. EditorShot --state asserts the round trip.
    const auto snapshot = parameters.copyState();

    if (auto xml = snapshot.createXml())
        copyXmlToBinary (*xml, destination);
}

void FXAnalyzerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto restored = juce::ValueTree::fromXml (*xml);

    if (! restored.isValid())
        return;

    // A session saved by a build whose state shape this one does not know is
    // ignored rather than half-applied. Refusing is a panel that opens with its
    // defaults; guessing is a panel whose scope time base came from a property
    // that used to mean something else.
    const int version = restored.getProperty (FXParams::propStateVersion, FXParams::stateVersion);

    if (version > FXParams::stateVersion)
        return;

    parameters.replaceState (restored);

    applyDisplaySettings();

    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FXAnalyzerProcessor();
}
