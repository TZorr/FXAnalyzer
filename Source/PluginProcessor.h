//
//  PluginProcessor.h
//  FX Analyzer
//
//  The processor's entire job on the audio thread is to hand the buffer to the
//  AnalysisHub and then leave it alone.
//
//  processBlock does not write to the buffer. Not "writes it back unchanged",
//  not "applies unity gain" - it never takes a write pointer at all, because
//  the difference between those two matters. A unity multiply is unity until
//  somebody smooths it, and a smoothed unity is a fade nobody asked for. The
//  only defensible implementation of a transparent plugin is one that has no
//  code path to the output, and AnalyzerCheck asserts the null against a
//  reference copy of the input.
//
//  Input Gain is not an exception to that. It scales what the analysis sees and
//  stops there, which is the whole reason it can be a wide +/-24 dB control
//  without anybody having to think about where the plugin sits in the chain.
//
//  State splits in two. The five things that change what is measured are
//  parameters, so a host can automate them and a preset can carry them. The
//  page you were on, the scale, the time base and the colours are properties on
//  the same tree - saved and restored, invisible to automation. The reasoning
//  is in ParameterIds.h.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "AnalysisHub.h"
#include "ParameterIds.h"
#include "Theme.h"

/*  A ChangeBroadcaster as well as a processor, and that is a bug fix rather
    than a flourish. The theme change used to be a std::function the editor
    assigned in its constructor and cleared in its destructor - and the host
    calls setStateInformation from whatever thread it likes, so it could be
    reading and calling that function object at the moment the message thread
    was assigning nullptr over it. Torn std::function, crash, and the trigger is
    a host restoring state while an editor is going away: exactly what happens
    when a plugin is dragged to another slot.

    ChangeBroadcaster is built for this. sendChangeMessage() is safe from any
    thread, delivery always lands on the message thread, and a listener that is
    destroyed removes itself. */
class FXAnalyzerProcessor : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster
{
public:
    FXAnalyzerProcessor();
    ~FXAnalyzerProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }

    AnalysisHub& getAnalysis() noexcept { return analysis; }

    //==============================================================================
    /** One parameter write, done the way a host expects to see it: a gesture
        around a normalised value. Both halves of that are easy to get wrong -
        writing 12.0 into setValueNotifyingHost on a -24..+24 range sets the
        parameter to its maximum and does it silently - so there is one place
        that does it, and the pages never touch a parameter object directly. */
    void  setFloatParameter (const char* id, float plainValue);
    float getFloatParameter (const char* id) const;

    void setChoiceParameter (const char* id, int index);
    int  getChoiceParameter (const char* id) const;

    void setBoolParameter (const char* id, bool state);
    bool getBoolParameter (const char* id) const;

    void beginGesture (const char* id);
    void endGesture (const char* id);

    /** View state: saved with the session, never seen by automation. */
    juce::var getViewProperty (const juce::Identifier& id, const juce::var& defaultValue) const;
    void      setViewProperty (const juce::Identifier& id, const juce::var& value);

    /** Pushes the view-state display settings - FFT size, octave smoothing -
        into the analysis. Called wherever state arrives, and public because
        EditorShot sets those properties directly in order to render the states
        the defaults do not show. Message thread only; both settings
        reallocate. */
    void applyDisplaySettings();

    /** The spectrum tilt in dB per octave, wherever it was stored.

        Sessions saved before 2026-09-24 hold it as an index into four slopes
        under `spectrumSlope`; newer ones hold the value itself under
        `spectrumTiltDb`. This is the one place that knows both, so the Spectrum
        page, the Settings page and EditorShot cannot each come to their own
        conclusion about an old session. */
    float getSpectrumTiltDb() const;
    void  setSpectrumTiltDb (float dbPerOctave);

    float getSpectrumTiltPivotHz() const;

    Theme getTheme() const;
    void  setTheme (const Theme&);

    /* The theme change is broadcast, not called back: see the note on the
       class. Listen with addChangeListener. */

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

    /** Copies the five measurement parameters into the hub. Called once per
        block from the audio thread; every one of them is an atomic load from
        the host's own storage, so this is cheaper than the listener machinery
        that would otherwise be needed to push them. */
    void pullParameters() noexcept;

    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* inputGainParam  = nullptr;
    std::atomic<float>* channelParam    = nullptr;
    std::atomic<float>* reactivityParam = nullptr;
    std::atomic<float>* dcBlockParam    = nullptr;
    std::atomic<float>* freezeParam     = nullptr;

    AnalysisHub analysis;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXAnalyzerProcessor)
};
